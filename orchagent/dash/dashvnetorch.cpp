#include <cassert>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <exception>
#include <inttypes.h>
#include <algorithm>
#include <numeric>

#include "converter.h"
#include "dashvnetorch.h"
#include "ipaddress.h"
#include "macaddress.h"
#include "orch.h"
#include "sai.h"
#include "saiextensions.h"
#include "swssnet.h"
#include "tokenize.h"
#include "dashorch.h"
#include "crmorch.h"
#include "saihelper.h"

#include "taskworker.h"
#include "pbutils.h"

using namespace std;
using namespace swss;

std::unordered_map<std::string, sai_object_id_t> gVnetNameToId;
extern sai_dash_vnet_api_t* sai_dash_vnet_api;
extern sai_dash_outbound_ca_to_pa_api_t* sai_dash_outbound_ca_to_pa_api;
extern sai_dash_pa_validation_api_t* sai_dash_pa_validation_api;
extern sai_object_id_t gSwitchId;
extern size_t gMaxBulkSize;
extern CrmOrch *gCrmOrch;

DashVnetOrch::DashVnetOrch(DBConnector *db, vector<string> &tables, ZmqServer *zmqServer) :
    vnet_bulker_(sai_dash_vnet_api, gSwitchId, gMaxBulkSize),
    outbound_ca_to_pa_bulker_(sai_dash_outbound_ca_to_pa_api, gMaxBulkSize),
    pa_validation_bulker_(sai_dash_pa_validation_api, gMaxBulkSize),
    ZmqOrch(db, tables, zmqServer)
{
    SWSS_LOG_ENTER();
}

bool DashVnetOrch::addVnet(const string& vnet_name, DashVnetBulkContext& ctxt)
{
    SWSS_LOG_ENTER();

    bool exists = (vnet_table_.find(vnet_name) != vnet_table_.end());
    if (exists)
    {
        SWSS_LOG_WARN("Vnet already exists for %s", vnet_name.c_str());
        return true;
    }

    uint32_t attr_count = 1;
    auto& object_ids = ctxt.object_ids;
    sai_attribute_t dash_vnet_attr;
    dash_vnet_attr.id = SAI_VNET_ATTR_VNI;
    dash_vnet_attr.value.u32 = ctxt.metadata.vni();
    object_ids.emplace_back();
    vnet_bulker_.create_entry(&object_ids.back(), attr_count, &dash_vnet_attr);

    return false;
}

bool DashVnetOrch::addVnetPost(const string& vnet_name, const DashVnetBulkContext& ctxt)
{
    SWSS_LOG_ENTER();

    const auto& object_ids = ctxt.object_ids;
    if (object_ids.empty())
    {
        return false;
    }

    auto it_id = object_ids.begin();
    sai_object_id_t id = *it_id++;
    if (id == SAI_NULL_OBJECT_ID)
    {
        SWSS_LOG_ERROR("Failed to create vnet entry for %s", vnet_name.c_str());
        return false;
    }

    VnetEntry entry = { id, ctxt.metadata };
    vnet_table_[vnet_name] = entry;
    gVnetNameToId[vnet_name] = id;

    gCrmOrch->incCrmResUsedCounter(CrmResourceType::CRM_DASH_VNET);

    SWSS_LOG_INFO("Vnet entry added for %s", vnet_name.c_str());

    return true;
}

bool DashVnetOrch::removeVnet(const string& vnet_name, DashVnetBulkContext& ctxt)
{
    SWSS_LOG_ENTER();

    bool exists = (vnet_table_.find(vnet_name) != vnet_table_.end());
    if (!exists)
    {
        SWSS_LOG_WARN("Failed to find vnet entry %s to remove", vnet_name.c_str());
        return true;
    }

    auto& object_statuses = ctxt.object_statuses;
    sai_object_id_t vni;
    VnetEntry entry = vnet_table_[vnet_name];
    vni = entry.vni;
    object_statuses.emplace_back();
    vnet_bulker_.remove_entry(&object_statuses.back(), vni);

    return false;
}

bool DashVnetOrch::removeVnetPost(const string& vnet_name, const DashVnetBulkContext& ctxt)
{
    SWSS_LOG_ENTER();

    const auto& object_statuses = ctxt.object_statuses;

    if (object_statuses.empty())
    {
        return false;
    }

    auto it_status = object_statuses.begin();
    sai_status_t status = *it_status++;
    if (status != SAI_STATUS_SUCCESS)
    {
        // Retry later if object has non-zero reference to it
        if (status == SAI_STATUS_NOT_EXECUTED)
        {
            return false;
        }
        SWSS_LOG_ERROR("Failed to remove vnet entry for %s", vnet_name.c_str());
        task_process_status handle_status = handleSaiRemoveStatus((sai_api_t) SAI_API_DASH_VNET, status);
        if (handle_status != task_success)
        {
            return parseHandleSaiStatusFailure(handle_status);
        }
    }

    gCrmOrch->decCrmResUsedCounter(CrmResourceType::CRM_DASH_VNET);

    vnet_table_.erase(vnet_name);
    gVnetNameToId.erase(vnet_name);
    SWSS_LOG_INFO("Vnet entry removed for %s", vnet_name.c_str());

    return true;
}

void DashVnetOrch::doTaskVnetTable(ConsumerBase& consumer)
{
    SWSS_LOG_ENTER();

    auto it = consumer.m_toSync.begin();

    while (it != consumer.m_toSync.end())
    {
        // Map to store vnet bulk op results
        std::map<std::pair<std::string, std::string>,
            DashVnetBulkContext> toBulk;

        while (it != consumer.m_toSync.end())
        {
            KeyOpFieldsValuesTuple tuple = it->second;
            const string& key = kfvKey(tuple);
            auto op = kfvOp(tuple);
            auto rc = toBulk.emplace(std::piecewise_construct,
                    std::forward_as_tuple(key, op),
                    std::forward_as_tuple());
            bool inserted = rc.second;
            auto& vnet_ctxt = rc.first->second;

            if (!inserted)
            {
                vnet_ctxt.clear();
            }

            if (op == SET_COMMAND)
            {
                if (!parsePbMessage(kfvFieldsValues(tuple), vnet_ctxt.metadata))
                {
                    SWSS_LOG_WARN("Requires protobuff at Vnet :%s", key.c_str());
                    it = consumer.m_toSync.erase(it);
                    continue;
                }
                if (addVnet(key, vnet_ctxt))
                {
                    it = consumer.m_toSync.erase(it);
                }
                else
                {
                    it++;
                }
            }
            else if (op == DEL_COMMAND)
            {
                if (removeVnet(key, vnet_ctxt))
                {
                    it = consumer.m_toSync.erase(it);
                }
                else
                {
                    it++;
                }
            }
            else
            {
                SWSS_LOG_ERROR("Invalid command %s", op.c_str());
                it = consumer.m_toSync.erase(it);
            }
        }

        vnet_bulker_.flush();

        auto it_prev = consumer.m_toSync.begin();
        while (it_prev != it)
        {
            KeyOpFieldsValuesTuple t = it_prev->second;

            string key = kfvKey(t);
            string op = kfvOp(t);
            auto found = toBulk.find(make_pair(key, op));
            if (found == toBulk.end())
            {
                it_prev++;
                continue;
            }

            const auto& vnet_ctxt = found->second;
            const auto& object_statuses = vnet_ctxt.object_statuses;
            const auto& object_ids = vnet_ctxt.object_ids;

            if (op == SET_COMMAND)
            {
                if (object_ids.empty())
                {
                    it_prev++;
                    continue;
                }
                if (addVnetPost(key, vnet_ctxt))
                {
                    it_prev = consumer.m_toSync.erase(it_prev);
                }
                else
                {
                    it_prev++;
                }
            }
            else if (op == DEL_COMMAND)
            {
                if (object_statuses.empty())
                {
                    it_prev++;
                    continue;
                }
                if (removeVnetPost(key, vnet_ctxt))
                {
                    it_prev = consumer.m_toSync.erase(it_prev);
                }
                else
                {
                    it_prev++;
                }
            }
        }
    }
}

void DashVnetOrch::addOutboundCaToPa(const string& key, VnetMapBulkContext& ctxt)
{
    SWSS_LOG_ENTER();

    sai_outbound_ca_to_pa_entry_t outbound_ca_to_pa_entry;
    outbound_ca_to_pa_entry.dst_vnet_id = gVnetNameToId[ctxt.vnet_name];
    outbound_ca_to_pa_entry.switch_id = gSwitchId;
    swss::copy(outbound_ca_to_pa_entry.dip, ctxt.dip);
    auto& object_statuses = ctxt.outbound_ca_to_pa_object_statuses;
    sai_attribute_t outbound_ca_to_pa_attr;
    vector<sai_attribute_t> outbound_ca_to_pa_attrs;

    outbound_ca_to_pa_attr.id = SAI_OUTBOUND_CA_TO_PA_ENTRY_ATTR_UNDERLAY_DIP;
    to_sai(ctxt.metadata.underlay_ip(), outbound_ca_to_pa_attr.value.ipaddr);
    outbound_ca_to_pa_attrs.push_back(outbound_ca_to_pa_attr);

    outbound_ca_to_pa_attr.id = SAI_OUTBOUND_CA_TO_PA_ENTRY_ATTR_OVERLAY_DMAC;
    memcpy(outbound_ca_to_pa_attr.value.mac, ctxt.metadata.mac_address().c_str(), sizeof(sai_mac_t));
    outbound_ca_to_pa_attrs.push_back(outbound_ca_to_pa_attr);

    outbound_ca_to_pa_attr.id = SAI_OUTBOUND_CA_TO_PA_ENTRY_ATTR_USE_DST_VNET_VNI;
    outbound_ca_to_pa_attr.value.booldata = ctxt.metadata.use_dst_vni();
    outbound_ca_to_pa_attrs.push_back(outbound_ca_to_pa_attr);

    object_statuses.emplace_back();
    outbound_ca_to_pa_bulker_.create_entry(&object_statuses.back(), &outbound_ca_to_pa_entry,
            (uint32_t)outbound_ca_to_pa_attrs.size(), outbound_ca_to_pa_attrs.data());
}

void DashVnetOrch::addPaValidation(const string& key, VnetMapBulkContext& ctxt)
{
    SWSS_LOG_ENTER();

    auto& object_statuses = ctxt.pa_validation_object_statuses;
    string underlay_ip_str = to_string(ctxt.metadata.underlay_ip());
    string pa_ref_key = ctxt.vnet_name + ":" + underlay_ip_str;
    auto it = pa_refcount_table_.find(pa_ref_key);
    if (it != pa_refcount_table_.end())
    {
        /*
         * PA validation entry already exisits. Just increment refcount and add
         * a dummy success status to satisfy postop
         */
        object_statuses.emplace_back(SAI_STATUS_SUCCESS);
        pa_refcount_table_[pa_ref_key]++;
        SWSS_LOG_INFO("Increment PA refcount to %u for PA IP %s",
                        pa_refcount_table_[pa_ref_key],
                        underlay_ip_str.c_str());
        return;
    }

    uint32_t attr_count = 1;
    sai_pa_validation_entry_t pa_validation_entry;
    pa_validation_entry.vnet_id = gVnetNameToId[ctxt.vnet_name];
    pa_validation_entry.switch_id = gSwitchId;
    to_sai(ctxt.metadata.underlay_ip(), pa_validation_entry.sip);
    sai_attribute_t pa_validation_attr;

    pa_validation_attr.id = SAI_PA_VALIDATION_ENTRY_ATTR_ACTION;
    pa_validation_attr.value.u32 = SAI_PA_VALIDATION_ENTRY_ACTION_PERMIT;

    object_statuses.emplace_back();
    pa_validation_bulker_.create_entry(&object_statuses.back(), &pa_validation_entry,
            attr_count, &pa_validation_attr);
    pa_refcount_table_[pa_ref_key] = 1;
    SWSS_LOG_INFO("Initialize PA refcount to 1 for PA IP %s",
                    underlay_ip_str.c_str());
}

bool DashVnetOrch::addVnetMap(const string& key, VnetMapBulkContext& ctxt)
{
    SWSS_LOG_ENTER();

    bool exists = (vnet_map_table_.find(key) != vnet_map_table_.end());
    if (!exists)
    {
        bool vnet_exists = (gVnetNameToId.find(ctxt.vnet_name) != gVnetNameToId.end());
        if (vnet_exists)
        {
            addOutboundCaToPa(key, ctxt);
            addPaValidation(key, ctxt);
        }
        else
        {
            SWSS_LOG_INFO("Not creating VNET map for %s since VNET %s doesn't exist", key.c_str(), ctxt.vnet_name.c_str());
        }
        return false;
    }
    /*
     * If the VNET map is already added, don't add it to the bulker and
     * return true so it's removed from the consumer
     */
    return true;
}

bool DashVnetOrch::addOutboundCaToPaPost(const string& key, const VnetMapBulkContext& ctxt)
{
    SWSS_LOG_ENTER();

    const auto& object_statuses = ctxt.outbound_ca_to_pa_object_statuses;
    if (object_statuses.empty())
    {
        return false;
    }

    auto it_status = object_statuses.begin();
    sai_status_t status = *it_status++;
    if (status != SAI_STATUS_SUCCESS)
    {
        if (status == SAI_STATUS_ITEM_ALREADY_EXISTS)
        {
            // Retry if item exists in the bulker
            return false;
        }

        SWSS_LOG_ERROR("Failed to create CA to PA entry for %s", key.c_str());
        task_process_status handle_status = handleSaiCreateStatus((sai_api_t) SAI_API_DASH_OUTBOUND_CA_TO_PA, status);
        if (handle_status != task_success)
        {
            return parseHandleSaiStatusFailure(handle_status);
        }
    }

    gCrmOrch->incCrmResUsedCounter(ctxt.dip.isV4() ? CrmResourceType::CRM_DASH_IPV4_OUTBOUND_CA_TO_PA : CrmResourceType::CRM_DASH_IPV6_OUTBOUND_CA_TO_PA);

    SWSS_LOG_INFO("Outbound CA to PA  map entry for %s added", key.c_str());

    return true;
}

bool DashVnetOrch::addPaValidationPost(const string& key, const VnetMapBulkContext& ctxt)
{
    SWSS_LOG_ENTER();

    const auto& object_statuses = ctxt.pa_validation_object_statuses;
    if (object_statuses.empty())
    {
        return false;
    }

    auto it_status = object_statuses.begin();
    string underlay_ip_str = to_string(ctxt.metadata.underlay_ip());
    string pa_ref_key = ctxt.vnet_name + ":" + underlay_ip_str;
    sai_status_t status = *it_status++;
    if (status != SAI_STATUS_SUCCESS)
    {
        /* PA validation entry add failed. Remove PA refcount entry */
        pa_refcount_table_.erase(pa_ref_key);
        if (status == SAI_STATUS_ITEM_ALREADY_EXISTS)
        {
            // Retry if item exists in the bulker
            return false;
        }

        SWSS_LOG_ERROR("Failed to create PA validation entry for %s", key.c_str());
        task_process_status handle_status = handleSaiCreateStatus((sai_api_t) SAI_API_DASH_PA_VALIDATION, status);
        if (handle_status != task_success)
        {
            return parseHandleSaiStatusFailure(handle_status);
        }
    }

    gCrmOrch->incCrmResUsedCounter(ctxt.metadata.underlay_ip().has_ipv4() ? CrmResourceType::CRM_DASH_IPV4_PA_VALIDATION : CrmResourceType::CRM_DASH_IPV6_PA_VALIDATION);

    SWSS_LOG_INFO("PA validation entry for %s added", key.c_str());

    return true;
}

bool DashVnetOrch::addVnetMapPost(const string& key, const VnetMapBulkContext& ctxt)
{
    SWSS_LOG_ENTER();

    bool status = addOutboundCaToPaPost(key, ctxt) && addPaValidationPost(key, ctxt);
    if (!status)
    {
        SWSS_LOG_ERROR("addVnetMapPost failed for %s ", key.c_str());
        return false;
    }

    string vnet_name = ctxt.vnet_name;
    VnetMapEntry entry = {  gVnetNameToId[vnet_name], ctxt.dip, ctxt.metadata };
    vnet_map_table_[key] = entry;
    SWSS_LOG_INFO("Vnet map added for %s", key.c_str());

    return true;
}

void DashVnetOrch::removeOutboundCaToPa(const string& key, VnetMapBulkContext& ctxt)
{
    SWSS_LOG_ENTER();

    auto& object_statuses = ctxt.outbound_ca_to_pa_object_statuses;
    sai_outbound_ca_to_pa_entry_t outbound_ca_to_pa_entry;
    outbound_ca_to_pa_entry.dst_vnet_id = vnet_map_table_[key].dst_vnet_id;
    outbound_ca_to_pa_entry.switch_id = gSwitchId;
    swss::copy(outbound_ca_to_pa_entry.dip, vnet_map_table_[key].dip);

    object_statuses.emplace_back();
    outbound_ca_to_pa_bulker_.remove_entry(&object_statuses.back(), &outbound_ca_to_pa_entry);
}

void DashVnetOrch::removePaValidation(const string& key, VnetMapBulkContext& ctxt)
{
    SWSS_LOG_ENTER();

    auto& object_statuses = ctxt.pa_validation_object_statuses;
    string underlay_ip = to_string(vnet_map_table_[key].metadata.underlay_ip());
    string pa_ref_key = ctxt.vnet_name + ":" + underlay_ip;
    auto it = pa_refcount_table_.find(pa_ref_key);
    if (it == pa_refcount_table_.end())
    {
        return;
    }
    else
    {
        if (--pa_refcount_table_[pa_ref_key] > 0)
        {
            /*
             * PA validation entry already exisits. Just decrement refcount and add
             * a dummy success status to satisfy postop
             */
            object_statuses.emplace_back(SAI_STATUS_SUCCESS);
            SWSS_LOG_INFO("Decrement PA refcount to %u for PA IP %s",
                            pa_refcount_table_[pa_ref_key],
                            underlay_ip.c_str());
            return;
        }
        else
        {
            sai_pa_validation_entry_t pa_validation_entry;
            pa_validation_entry.vnet_id = vnet_map_table_[key].dst_vnet_id;
            pa_validation_entry.switch_id = gSwitchId;
            to_sai(vnet_map_table_[key].metadata.underlay_ip(), pa_validation_entry.sip);

            object_statuses.emplace_back();
            pa_validation_bulker_.remove_entry(&object_statuses.back(), &pa_validation_entry);
            SWSS_LOG_INFO("PA refcount refcount is zero for PA IP %s, removing refcount table entry",
                            underlay_ip.c_str());
            pa_refcount_table_.erase(pa_ref_key);
        }
    }
}

bool DashVnetOrch::removeVnetMap(const string& key, VnetMapBulkContext& ctxt)
{
    SWSS_LOG_ENTER();

    bool exists = (vnet_map_table_.find(key) != vnet_map_table_.end());
    if (!exists)
    {
        SWSS_LOG_INFO("Failed to find vnet mapping %s to remove", key.c_str());
        return true;
    }

    removePaValidation(key, ctxt);
    removeOutboundCaToPa(key, ctxt);

    return false;
}

bool DashVnetOrch::removeOutboundCaToPaPost(const string& key, const VnetMapBulkContext& ctxt)
{
    SWSS_LOG_ENTER();

    const auto& object_statuses = ctxt.outbound_ca_to_pa_object_statuses;
    if (object_statuses.empty())
    {
        return false;
    }

    auto it_status = object_statuses.begin();
    sai_status_t status = *it_status++;
    if (status != SAI_STATUS_SUCCESS)
    {
        // Retry later if object has non-zero reference to it
        if (status == SAI_STATUS_NOT_EXECUTED)
        {
            return false;
        }

        SWSS_LOG_ERROR("Failed to remove outbound routing entry for %s", key.c_str());
        task_process_status handle_status = handleSaiRemoveStatus((sai_api_t) SAI_API_DASH_OUTBOUND_CA_TO_PA, status);
        if (handle_status != task_success)
        {
            return parseHandleSaiStatusFailure(handle_status);
        }
    }

    gCrmOrch->decCrmResUsedCounter(vnet_map_table_[key].dip.isV4() ? CrmResourceType::CRM_DASH_IPV4_OUTBOUND_CA_TO_PA : CrmResourceType::CRM_DASH_IPV6_OUTBOUND_CA_TO_PA);

    SWSS_LOG_INFO("Outbound CA to PA map entry for %s removed", key.c_str());

    return true;
}

bool DashVnetOrch::removePaValidationPost(const string& key, const VnetMapBulkContext& ctxt)
{
    SWSS_LOG_ENTER();

    string underlay_ip = to_string(vnet_map_table_[key].metadata.underlay_ip());
    string pa_ref_key = ctxt.vnet_name + ":" + underlay_ip;
    const auto& object_statuses = ctxt.pa_validation_object_statuses;
    if (object_statuses.empty())
    {
        return false;
    }

    auto it_status = object_statuses.begin();
    sai_status_t status = *it_status++;
    if (status != SAI_STATUS_SUCCESS)
    {
        // Retry later if object has non-zero reference to it
        if (status == SAI_STATUS_NOT_EXECUTED)
        {
            return false;
        }

        SWSS_LOG_ERROR("Failed to remove PA validation entry for %s", key.c_str());
        task_process_status handle_status = handleSaiRemoveStatus((sai_api_t) SAI_API_DASH_PA_VALIDATION, status);
        if (handle_status != task_success)
        {
            return parseHandleSaiStatusFailure(handle_status);
        }
    }

    gCrmOrch->decCrmResUsedCounter(vnet_map_table_[key].metadata.underlay_ip().has_ipv4() ? CrmResourceType::CRM_DASH_IPV4_PA_VALIDATION : CrmResourceType::CRM_DASH_IPV6_PA_VALIDATION);

    SWSS_LOG_INFO("PA validation entry for %s removed", key.c_str());

    return true;
}

bool DashVnetOrch::removeVnetMapPost(const string& key, const VnetMapBulkContext& ctxt)
{
    SWSS_LOG_ENTER();

    bool status = removeOutboundCaToPaPost(key, ctxt) && removePaValidationPost(key, ctxt);
    if (!status)
    {
        return false;
    }
    vnet_map_table_.erase(key);
    SWSS_LOG_INFO("Vnet map removed for %s", key.c_str());

    return true;
}

void DashVnetOrch::doTaskVnetMapTable(ConsumerBase& consumer)
{
    SWSS_LOG_ENTER();

    auto it = consumer.m_toSync.begin();

    while (it != consumer.m_toSync.end())
    {
        std::map<std::pair<std::string, std::string>,
            VnetMapBulkContext> toBulk;

        while (it != consumer.m_toSync.end())
        {
            KeyOpFieldsValuesTuple tuple = it->second;
            const string& key = kfvKey(tuple);
            auto op = kfvOp(tuple);
            auto rc = toBulk.emplace(std::piecewise_construct,
                    std::forward_as_tuple(key, op),
                    std::forward_as_tuple());
            bool inserted = rc.second;
            auto& ctxt = rc.first->second;

            if (!inserted)
            {
                ctxt.clear();
            }

            string& vnet_name = ctxt.vnet_name;
            IpAddress& dip = ctxt.dip;

            vector<string> keys = tokenize(key, ':');
            vnet_name = keys[0];
            size_t pos = key.find(":", vnet_name.length());
            string ip_str = key.substr(pos + 1);
            dip = IpAddress(ip_str);

            if (op == SET_COMMAND)
            {
                if (!parsePbMessage(kfvFieldsValues(tuple), ctxt.metadata))
                {
                    SWSS_LOG_WARN("Requires protobuff at VnetMap :%s", key.c_str());
                    it = consumer.m_toSync.erase(it);
                    continue;
                }
                if (addVnetMap(key, ctxt))
                {
                    it = consumer.m_toSync.erase(it);
                }
                else
                {
                    it++;
                }
            }
            else if (op == DEL_COMMAND)
            {
                if (removeVnetMap(key, ctxt))
                {
                    it = consumer.m_toSync.erase(it);
                }
                else
                {
                    it++;
                }
            }
            else
            {
                SWSS_LOG_ERROR("Invalid command %s", op.c_str());
                it = consumer.m_toSync.erase(it);
            }
        }

        outbound_ca_to_pa_bulker_.flush();
        pa_validation_bulker_.flush();

        auto it_prev = consumer.m_toSync.begin();
        while (it_prev != it)
        {
            KeyOpFieldsValuesTuple t = it_prev->second;
            string key = kfvKey(t);
            string op = kfvOp(t);
            auto found = toBulk.find(make_pair(key, op));
            if (found == toBulk.end())
            {
                it_prev++;
                continue;
            }

            const auto& ctxt = found->second;
            const auto& outbound_ca_to_pa_object_statuses = ctxt.outbound_ca_to_pa_object_statuses;
            const auto& pa_validation_object_statuses = ctxt.pa_validation_object_statuses;
            if (outbound_ca_to_pa_object_statuses.empty() || pa_validation_object_statuses.empty())
            {
                it_prev++;
                continue;
            }

            if (op == SET_COMMAND)
            {
                if (addVnetMapPost(key, ctxt))
                {
                    it_prev = consumer.m_toSync.erase(it_prev);
                }
                else
                {
                    it_prev++;
                }
            }
            else if (op == DEL_COMMAND)
            {
                if (removeVnetMapPost(key, ctxt))
                {
                    it_prev = consumer.m_toSync.erase(it_prev);
                }
                else
                {
                    it_prev++;
                }
            }
        }
    }
}

void DashVnetOrch::doTask(ConsumerBase &consumer)
{
    SWSS_LOG_ENTER();

    const auto& tn = consumer.getTableName();

    SWSS_LOG_INFO("Table name: %s", tn.c_str());

    if (tn == APP_DASH_VNET_TABLE_NAME)
    {
        doTaskVnetTable(consumer);
    }
    else if (tn == APP_DASH_VNET_MAPPING_TABLE_NAME)
    {
        doTaskVnetMapTable(consumer);
    }
    else
    {
        SWSS_LOG_ERROR("Unknown table: %s", tn.c_str());
    }
}
