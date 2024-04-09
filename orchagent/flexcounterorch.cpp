#include <unordered_map>
#include "portsorch.h"
#include "fabricportsorch.h"
#include "select.h"
#include "notifier.h"
#include "sai_serialize.h"
#include "pfcwdorch.h"
#include "bufferorch.h"
#include "flexcounterorch.h"
#include "debugcounterorch.h"
#include "directory.h"
#include "copporch.h"
#include <swss/tokenize.h>
#include "routeorch.h"
#include "macsecorch.h"
#include "flowcounterrouteorch.h"

extern sai_port_api_t *sai_port_api;

extern PortsOrch *gPortsOrch;
extern FabricPortsOrch *gFabricPortsOrch;
extern IntfsOrch *gIntfsOrch;
extern BufferOrch *gBufferOrch;
extern Directory<Orch*> gDirectory;
extern CoppOrch *gCoppOrch;
extern FlowCounterRouteOrch *gFlowCounterRouteOrch;

#define BUFFER_POOL_WATERMARK_KEY   "BUFFER_POOL_WATERMARK"
#define PORT_KEY                    "PORT"
#define PORT_BUFFER_DROP_KEY        "PORT_BUFFER_DROP"
#define QUEUE_KEY                   "QUEUE"
#define QUEUE_WATERMARK             "QUEUE_WATERMARK"
#define PG_WATERMARK_KEY            "PG_WATERMARK"
#define PG_DROP_KEY                 "PG_DROP"
#define RIF_KEY                     "RIF"
#define ACL_KEY                     "ACL"
#define TUNNEL_KEY                  "TUNNEL"
#define FLOW_CNT_TRAP_KEY           "FLOW_CNT_TRAP"
#define FLOW_CNT_ROUTE_KEY          "FLOW_CNT_ROUTE"

unordered_map<string, string> flexCounterGroupMap =
{
    {"PORT", PORT_STAT_COUNTER_FLEX_COUNTER_GROUP},
    {"PORT_RATES", PORT_RATE_COUNTER_FLEX_COUNTER_GROUP},
    {"PORT_BUFFER_DROP", PORT_BUFFER_DROP_STAT_FLEX_COUNTER_GROUP},
    {"QUEUE", QUEUE_STAT_COUNTER_FLEX_COUNTER_GROUP},
    {"PFCWD", PFC_WD_FLEX_COUNTER_GROUP},
    {"QUEUE_WATERMARK", QUEUE_WATERMARK_STAT_COUNTER_FLEX_COUNTER_GROUP},
    {"PG_WATERMARK", PG_WATERMARK_STAT_COUNTER_FLEX_COUNTER_GROUP},
    {"PG_DROP", PG_DROP_STAT_COUNTER_FLEX_COUNTER_GROUP},
    {BUFFER_POOL_WATERMARK_KEY, BUFFER_POOL_WATERMARK_STAT_COUNTER_FLEX_COUNTER_GROUP},
    {"RIF", RIF_STAT_COUNTER_FLEX_COUNTER_GROUP},
    {"RIF_RATES", RIF_RATE_COUNTER_FLEX_COUNTER_GROUP},
    {"DEBUG_COUNTER", DEBUG_COUNTER_FLEX_COUNTER_GROUP},
    {"ACL", ACL_COUNTER_FLEX_COUNTER_GROUP},
    {"TUNNEL", TUNNEL_STAT_COUNTER_FLEX_COUNTER_GROUP},
    {FLOW_CNT_TRAP_KEY, HOSTIF_TRAP_COUNTER_FLEX_COUNTER_GROUP},
    {FLOW_CNT_ROUTE_KEY, ROUTE_FLOW_COUNTER_FLEX_COUNTER_GROUP},
    {"MACSEC_SA", COUNTERS_MACSEC_SA_GROUP},
    {"MACSEC_SA_ATTR", COUNTERS_MACSEC_SA_ATTR_GROUP},
    {"MACSEC_FLOW", COUNTERS_MACSEC_FLOW_GROUP},
};


FlexCounterOrch::FlexCounterOrch(DBConnector *db, vector<string> &tableNames):
    Orch(db, tableNames),
    m_flexCounterConfigTable(db, CFG_FLEX_COUNTER_TABLE_NAME),
    m_bufferQueueConfigTable(db, CFG_BUFFER_QUEUE_TABLE_NAME),
    m_bufferPgConfigTable(db, CFG_BUFFER_PG_TABLE_NAME),
    m_deviceMetadataConfigTable(db, CFG_DEVICE_METADATA_TABLE_NAME),
    m_flexCounterDb(new DBConnector("FLEX_COUNTER_DB", 0)),
    m_flexCounterGroupTable(new ProducerTable(m_flexCounterDb.get(), FLEX_COUNTER_GROUP_TABLE)),
    m_gbflexCounterDb(new DBConnector("GB_FLEX_COUNTER_DB", 0)),
    m_gbflexCounterGroupTable(new ProducerTable(m_gbflexCounterDb.get(), FLEX_COUNTER_GROUP_TABLE))
{
    SWSS_LOG_ENTER();
}

FlexCounterOrch::~FlexCounterOrch(void)
{
    SWSS_LOG_ENTER();
}

void FlexCounterOrch::doTask(Consumer &consumer)
{
    SWSS_LOG_ENTER();

    VxlanTunnelOrch* vxlan_tunnel_orch = gDirectory.get<VxlanTunnelOrch*>();
    if (gPortsOrch && !gPortsOrch->allPortsReady())
    {
        return;
    }

    if (gFabricPortsOrch && !gFabricPortsOrch->allPortsReady())
    {
        return;
    }

    auto it = consumer.m_toSync.begin();
    while (it != consumer.m_toSync.end())
    {
        KeyOpFieldsValuesTuple t = it->second;

        string key =  kfvKey(t);
        string op = kfvOp(t);
        auto data = kfvFieldsValues(t);

        if (!flexCounterGroupMap.count(key))
        {
            SWSS_LOG_NOTICE("Invalid flex counter group input, %s", key.c_str());
            consumer.m_toSync.erase(it++);
            continue;
        }

        if (op == SET_COMMAND)
        {
            auto itDelay = std::find(std::begin(data), std::end(data), FieldValueTuple(FLEX_COUNTER_DELAY_STATUS_FIELD, "true"));

            if (itDelay != data.end())
            {
                consumer.m_toSync.erase(it++);
                continue;
            }
            for (auto valuePair:data)
            {
                const auto &field = fvField(valuePair);
                const auto &value = fvValue(valuePair);

                if (field == POLL_INTERVAL_FIELD)
                {
                    vector<FieldValueTuple> fieldValues;
                    fieldValues.emplace_back(POLL_INTERVAL_FIELD, value);
                    m_flexCounterGroupTable->set(flexCounterGroupMap[key], fieldValues);
                    if (gPortsOrch && gPortsOrch->isGearboxEnabled())
                    {
                        if (key == PORT_KEY || key.rfind("MACSEC", 0) == 0)
                        {
                            m_gbflexCounterGroupTable->set(flexCounterGroupMap[key], fieldValues);
                        }
                    }
                }
                else if(field == FLEX_COUNTER_STATUS_FIELD)
                {
                    // Currently, the counters are disabled for polling by default
                    // The queue maps will be generated as soon as counters are enabled for polling
                    // Counter polling is enabled by pushing the COUNTER_ID_LIST/ATTR_ID_LIST, which contains
                    // the list of SAI stats/attributes of polling interest, to the FLEX_COUNTER_DB under the
                    // additional condition that the polling interval at that time is set nonzero positive,
                    // which is automatically satisfied upon the creation of the orch object that requires
                    // the syncd flex counter polling service
                    // This postponement is introduced by design to accelerate the initialization process
                    if(gPortsOrch && (value == "enable"))
                    {
                        if(key == PORT_KEY)
                        {
                            gPortsOrch->generatePortCounterMap();
                            m_port_counter_enabled = true;
                        }
                        else if(key == PORT_BUFFER_DROP_KEY)
                        {
                            gPortsOrch->generatePortBufferDropCounterMap();
                            m_port_buffer_drop_counter_enabled = true;
                        }
                        else if(key == QUEUE_KEY)
                        {
                            gPortsOrch->generateQueueMap(getQueueConfigurations());
                            m_queue_enabled = true;
                            gPortsOrch->addQueueFlexCounters(getQueueConfigurations());
                        }
                        else if(key == QUEUE_WATERMARK)
                        {
                            gPortsOrch->generateQueueMap(getQueueConfigurations());
                            m_queue_watermark_enabled = true;
                            gPortsOrch->addQueueWatermarkFlexCounters(getQueueConfigurations());
                        }
                        else if(key == PG_DROP_KEY)
                        {
                            gPortsOrch->generatePriorityGroupMap(getPgConfigurations());
                            m_pg_enabled = true;
                            gPortsOrch->addPriorityGroupFlexCounters(getPgConfigurations());
                        }
                        else if(key == PG_WATERMARK_KEY)
                        {
                            gPortsOrch->generatePriorityGroupMap(getPgConfigurations());
                            m_pg_watermark_enabled = true;
                            gPortsOrch->addPriorityGroupWatermarkFlexCounters(getPgConfigurations());
                        }
                    }
                    if(gIntfsOrch && (key == RIF_KEY) && (value == "enable"))
                    {
                        gIntfsOrch->generateInterfaceMap();
                    }
                    if (gBufferOrch && (key == BUFFER_POOL_WATERMARK_KEY) && (value == "enable"))
                    {
                        gBufferOrch->generateBufferPoolWatermarkCounterIdList();
                    }
                    if (gFabricPortsOrch)
                    {
                        gFabricPortsOrch->generateQueueStats();
                    }
                    if (vxlan_tunnel_orch && (key== TUNNEL_KEY) && (value == "enable"))
                    {
                        vxlan_tunnel_orch->generateTunnelCounterMap();
                    }
                    if (gCoppOrch && (key == FLOW_CNT_TRAP_KEY))
                    {
                        if (value == "enable")
                        {
                            m_hostif_trap_counter_enabled = true;
                            gCoppOrch->generateHostIfTrapCounterIdList();
                        }
                        else if (value == "disable")
                        {
                            gCoppOrch->clearHostIfTrapCounterIdList();
                            m_hostif_trap_counter_enabled = false;
                        }
                    }
                    if (gFlowCounterRouteOrch && gFlowCounterRouteOrch->getRouteFlowCounterSupported() && key == FLOW_CNT_ROUTE_KEY)
                    {
                        if (value == "enable" && !m_route_flow_counter_enabled)
                        {
                            m_route_flow_counter_enabled = true;
                            gFlowCounterRouteOrch->generateRouteFlowStats();
                        }
                        else if (value == "disable" && m_route_flow_counter_enabled)
                        {
                            gFlowCounterRouteOrch->clearRouteFlowStats();
                            m_route_flow_counter_enabled = false;
                        }
                    }
                    vector<FieldValueTuple> fieldValues;
                    fieldValues.emplace_back(FLEX_COUNTER_STATUS_FIELD, value);
                    m_flexCounterGroupTable->set(flexCounterGroupMap[key], fieldValues);

                    if (gPortsOrch && gPortsOrch->isGearboxEnabled())
                    {
                        if (key == PORT_KEY || key.rfind("MACSEC", 0) == 0)
                        {
                            m_gbflexCounterGroupTable->set(flexCounterGroupMap[key], fieldValues);
                        }
                    }
                }
                else if(field == FLEX_COUNTER_DELAY_STATUS_FIELD)
                {
                    // This field is ignored since it is being used before getting into this loop.
                    // If it is exist and the value is 'true' we need to skip the iteration in order to delay the counter creation.
                    // The field will clear out and counter will be created when enable_counters script is called.
                }
                else
                {
                    SWSS_LOG_NOTICE("Unsupported field %s", field.c_str());
                }
            }
        }

        consumer.m_toSync.erase(it++);
    }
}

bool FlexCounterOrch::getPortCountersState() const
{
    return m_port_counter_enabled;
}

bool FlexCounterOrch::getPortBufferDropCountersState() const
{
    return m_port_buffer_drop_counter_enabled;
}

bool FlexCounterOrch::getQueueCountersState() const
{
    return m_queue_enabled;
}

bool FlexCounterOrch::getQueueWatermarkCountersState() const
{
    return m_queue_watermark_enabled;
}

bool FlexCounterOrch::getPgCountersState() const
{
    return m_pg_enabled;
}

bool FlexCounterOrch::getPgWatermarkCountersState() const
{
    return m_pg_watermark_enabled;
}

bool FlexCounterOrch::bake()
{
    /*
     * bake is called during warmreboot reconciling procedure.
     * By default, it should fetch items from the tables the sub agents listen to,
     * and then push them into m_toSync of each sub agent.
     * The motivation is to make sub agents handle the saved entries first and then handle the upcoming entries.
     */

    std::deque<KeyOpFieldsValuesTuple> entries;
    vector<string> keys;
    m_flexCounterConfigTable.getKeys(keys);
    for (const auto &key: keys)
    {
        if (!flexCounterGroupMap.count(key))
        {
            SWSS_LOG_NOTICE("FlexCounterOrch: Invalid flex counter group intput %s is skipped during reconciling", key.c_str());
            continue;
        }

        if (key == BUFFER_POOL_WATERMARK_KEY)
        {
            SWSS_LOG_NOTICE("FlexCounterOrch: Do not handle any FLEX_COUNTER table for %s update during reconciling",
                            BUFFER_POOL_WATERMARK_KEY);
            continue;
        }

        KeyOpFieldsValuesTuple kco;

        kfvKey(kco) = key;
        kfvOp(kco) = SET_COMMAND;

        if (!m_flexCounterConfigTable.get(key, kfvFieldsValues(kco)))
        {
            continue;
        }
        entries.push_back(kco);
    }
    Consumer* consumer = dynamic_cast<Consumer *>(getExecutor(CFG_FLEX_COUNTER_TABLE_NAME));
    return consumer->addToSync(entries);
}

static bool isCreateOnlyConfigDbBuffers(Table& deviceMetadataConfigTable)
{
    std::string createOnlyConfigDbBuffersValue;

    try
    {
        if (deviceMetadataConfigTable.hget("localhost", "create_only_config_db_buffers", createOnlyConfigDbBuffersValue))
        {
            if (createOnlyConfigDbBuffersValue == "true")
            {
                return true;
            }
        }
    }
    catch(const std::system_error& e)
    {
        SWSS_LOG_ERROR("System error: %s", e.what());
    }

    return false;
}

map<string, FlexCounterQueueStates> FlexCounterOrch::getQueueConfigurations()
{
    SWSS_LOG_ENTER();

    map<string, FlexCounterQueueStates> queuesStateVector;

    if (!isCreateOnlyConfigDbBuffers(m_deviceMetadataConfigTable))
    {
        FlexCounterQueueStates flexCounterQueueState(0);
        queuesStateVector.insert(make_pair(createAllAvailableBuffersStr, flexCounterQueueState));
        return queuesStateVector;
    }

    std::vector<std::string> portQueueKeys;
    m_bufferQueueConfigTable.getKeys(portQueueKeys);

    for (const auto& portQueueKey : portQueueKeys)
    {
        auto toks = tokenize(portQueueKey, '|');
        if (toks.size() != 2)
        {
            SWSS_LOG_ERROR("Invalid BUFFER_QUEUE key: [%s]", portQueueKey.c_str());
            continue;
        }

        auto configPortNames = tokenize(toks[0], ',');
        auto configPortQueues = toks[1];
        toks = tokenize(configPortQueues, '-');

        for (const auto& configPortName : configPortNames)
        {
            uint32_t maxQueueNumber = gPortsOrch->getNumberOfPortSupportedQueueCounters(configPortName);
            uint32_t maxQueueIndex = maxQueueNumber - 1;
            uint32_t minQueueIndex = 0;

            if (!queuesStateVector.count(configPortName))
            {
                FlexCounterQueueStates flexCounterQueueState(maxQueueNumber);
                queuesStateVector.insert(make_pair(configPortName, flexCounterQueueState));
            }

            try {
                auto startIndex = to_uint<uint32_t>(toks[0], minQueueIndex, maxQueueIndex);
                if (toks.size() > 1)
                {
                    auto endIndex = to_uint<uint32_t>(toks[1], minQueueIndex, maxQueueIndex);
                    queuesStateVector.at(configPortName).enableQueueCounters(startIndex, endIndex);
                }
                else
                {
                    queuesStateVector.at(configPortName).enableQueueCounter(startIndex);
                }
            } catch (std::invalid_argument const& e) {
                    SWSS_LOG_ERROR("Invalid queue index [%s] for port [%s]", configPortQueues.c_str(), configPortName.c_str());
                    continue;
            }
        }
    }

    return queuesStateVector;
}

map<string, FlexCounterPgStates> FlexCounterOrch::getPgConfigurations()
{
    SWSS_LOG_ENTER();

    map<string, FlexCounterPgStates> pgsStateVector;

    if (!isCreateOnlyConfigDbBuffers(m_deviceMetadataConfigTable))
    {
        FlexCounterPgStates flexCounterPgState(0);
        pgsStateVector.insert(make_pair(createAllAvailableBuffersStr, flexCounterPgState));
        return pgsStateVector;
    }

    std::vector<std::string> portPgKeys;
    m_bufferPgConfigTable.getKeys(portPgKeys);

    for (const auto& portPgKey : portPgKeys)
    {
        auto toks = tokenize(portPgKey, '|');
        if (toks.size() != 2)
        {
            SWSS_LOG_ERROR("Invalid BUFFER_PG key: [%s]", portPgKey.c_str());
            continue;
        }

        auto configPortNames = tokenize(toks[0], ',');
        auto configPortPgs = toks[1];
        toks = tokenize(configPortPgs, '-');

        for (const auto& configPortName : configPortNames)
        {
            uint32_t maxPgNumber = gPortsOrch->getNumberOfPortSupportedPgCounters(configPortName);
            uint32_t maxPgIndex = maxPgNumber - 1;
            uint32_t minPgIndex = 0;

            if (!pgsStateVector.count(configPortName))
            {
                FlexCounterPgStates flexCounterPgState(maxPgNumber);
                pgsStateVector.insert(make_pair(configPortName, flexCounterPgState));
            }

            try {
                auto startIndex = to_uint<uint32_t>(toks[0], minPgIndex, maxPgIndex);
                if (toks.size() > 1)
                {
                    auto endIndex = to_uint<uint32_t>(toks[1], minPgIndex, maxPgIndex);
                    pgsStateVector.at(configPortName).enablePgCounters(startIndex, endIndex);
                }
                else
                {
                    pgsStateVector.at(configPortName).enablePgCounter(startIndex);
                }
            } catch (std::invalid_argument const& e) {
                    SWSS_LOG_ERROR("Invalid pg index [%s] for port [%s]", configPortPgs.c_str(), configPortName.c_str());
                    continue;
            }
        }
    }

    return pgsStateVector;
}

FlexCounterQueueStates::FlexCounterQueueStates(uint32_t maxQueueNumber)
{
    SWSS_LOG_ENTER();
    m_queueStates.resize(maxQueueNumber, false);
}

bool FlexCounterQueueStates::isQueueCounterEnabled(uint32_t index) const
{
    SWSS_LOG_ENTER();
    return m_queueStates[index];
}

void FlexCounterQueueStates::enableQueueCounters(uint32_t startIndex, uint32_t endIndex)
{
    SWSS_LOG_ENTER();
    for (uint32_t queueIndex = startIndex; queueIndex <= endIndex; queueIndex++)
    {
        enableQueueCounter(queueIndex);
    }
}

void FlexCounterQueueStates::enableQueueCounter(uint32_t queueIndex)
{
    SWSS_LOG_ENTER();
    m_queueStates[queueIndex] = true;
}

FlexCounterPgStates::FlexCounterPgStates(uint32_t maxPgNumber)
{
    SWSS_LOG_ENTER();
    m_pgStates.resize(maxPgNumber, false);
}

bool FlexCounterPgStates::isPgCounterEnabled(uint32_t index) const
{
    SWSS_LOG_ENTER();
    return m_pgStates[index];
}

void FlexCounterPgStates::enablePgCounters(uint32_t startIndex, uint32_t endIndex)
{
    SWSS_LOG_ENTER();
    for (uint32_t pgIndex = startIndex; pgIndex <= endIndex; pgIndex++)
    {
        enablePgCounter(pgIndex);
    }
}

void FlexCounterPgStates::enablePgCounter(uint32_t pgIndex)
{
    SWSS_LOG_ENTER();
    m_pgStates[pgIndex] = true;
}
