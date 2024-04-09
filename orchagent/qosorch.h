#ifndef SWSS_QOSORCH_H
#define SWSS_QOSORCH_H

#include <map>
#include <unordered_map>
#include <unordered_set>
#include "orch.h"
#include "switchorch.h"
#include "portsorch.h"

const string dscp_to_tc_field_name              = "dscp_to_tc_map";
const string mpls_tc_to_tc_field_name           = "mpls_tc_to_tc_map";
const string dot1p_to_tc_field_name             = "dot1p_to_tc_map";
const string pfc_to_pg_map_name                 = "pfc_to_pg_map";
const string pfc_to_queue_map_name              = "pfc_to_queue_map";
const string pfc_enable_name                    = "pfc_enable";
const string pfcwd_sw_enable_name               = "pfcwd_sw_enable";
const string tc_to_pg_map_field_name            = "tc_to_pg_map";
const string tc_to_queue_field_name             = "tc_to_queue_map";
const string tc_to_dot1p_field_name             = "tc_to_dot1p_map";
const string tc_to_dscp_field_name              = "tc_to_dscp_map";
const string scheduler_field_name               = "scheduler";
const string red_max_threshold_field_name       = "red_max_threshold";
const string red_min_threshold_field_name       = "red_min_threshold";
const string yellow_max_threshold_field_name    = "yellow_max_threshold";
const string yellow_min_threshold_field_name    = "yellow_min_threshold";
const string green_max_threshold_field_name     = "green_max_threshold";
const string green_min_threshold_field_name     = "green_min_threshold";
const string red_drop_probability_field_name    = "red_drop_probability";
const string yellow_drop_probability_field_name = "yellow_drop_probability";
const string green_drop_probability_field_name  = "green_drop_probability";
const string dscp_to_fc_field_name              = "dscp_to_fc_map";
const string exp_to_fc_field_name               = "exp_to_fc_map";
const string decap_dscp_to_tc_field_name        = "decap_dscp_to_tc_map";
const string decap_tc_to_pg_field_name          = "decap_tc_to_pg_map";
const string encap_tc_to_queue_field_name       = "encap_tc_to_queue_map";
const string encap_tc_to_dscp_field_name        = "encap_tc_to_dscp_map";

const string wred_profile_field_name            = "wred_profile";
const string wred_red_enable_field_name         = "wred_red_enable";
const string wred_yellow_enable_field_name      = "wred_yellow_enable";
const string wred_green_enable_field_name       = "wred_green_enable";

const string scheduler_algo_type_field_name     = "type";
const string scheduler_algo_DWRR                = "DWRR";
const string scheduler_algo_WRR                 = "WRR";
const string scheduler_algo_STRICT              = "STRICT";
const string scheduler_weight_field_name        = "weight";
const string scheduler_meter_type_field_name    = "meter_type";
const string scheduler_min_bandwidth_rate_field_name       = "cir";//Committed Information Rate
const string scheduler_min_bandwidth_burst_rate_field_name = "cbs";//Committed Burst Size
const string scheduler_max_bandwidth_rate_field_name       = "pir";//Peak Information Rate
const string scheduler_max_bandwidth_burst_rate_field_name = "pbs";//Peak Burst Size

const string ecn_field_name                     = "ecn";
const string ecn_none                           = "ecn_none";
const string ecn_red                            = "ecn_red";
const string ecn_yellow                         = "ecn_yellow";
const string ecn_yellow_red                     = "ecn_yellow_red";
const string ecn_green                          = "ecn_green";
const string ecn_green_red                      = "ecn_green_red";
const string ecn_green_yellow                   = "ecn_green_yellow";
const string ecn_all                            = "ecn_all";

class QosMapHandler
{
public:
    task_process_status processWorkItem(Consumer& consumer, KeyOpFieldsValuesTuple &tuple);
    virtual bool convertFieldValuesToAttributes(KeyOpFieldsValuesTuple &tuple, vector<sai_attribute_t> &attributes) = 0;
    virtual void freeAttribResources(vector<sai_attribute_t> &attributes);
    virtual bool modifyQosItem(sai_object_id_t, vector<sai_attribute_t> &attributes);
    virtual sai_object_id_t addQosItem(const vector<sai_attribute_t> &attributes) = 0;//different for sub-classes
    virtual bool removeQosItem(sai_object_id_t sai_object);
};

class DscpToTcMapHandler : public QosMapHandler
{
public:
    bool convertFieldValuesToAttributes(KeyOpFieldsValuesTuple &tuple, vector<sai_attribute_t> &attributes) override;
    sai_object_id_t addQosItem(const vector<sai_attribute_t> &attributes) override;
    bool removeQosItem(sai_object_id_t sai_object);
};

class MplsTcToTcMapHandler : public QosMapHandler
{
public:
    bool convertFieldValuesToAttributes(KeyOpFieldsValuesTuple &tuple, vector<sai_attribute_t> &attributes) override;
    sai_object_id_t addQosItem(const vector<sai_attribute_t> &attributes) override;
};

class Dot1pToTcMapHandler : public QosMapHandler
{
public:
    bool convertFieldValuesToAttributes(KeyOpFieldsValuesTuple &tuple, vector<sai_attribute_t> &attributes) override;
    sai_object_id_t addQosItem(const vector<sai_attribute_t> &attributes) override;
};

class TcToQueueMapHandler : public QosMapHandler
{
public:
    bool convertFieldValuesToAttributes(KeyOpFieldsValuesTuple &tuple, vector<sai_attribute_t> &attributes);
    sai_object_id_t addQosItem(const vector<sai_attribute_t> &attributes);
};

class WredMapHandler : public QosMapHandler
{
public:
    bool convertFieldValuesToAttributes(KeyOpFieldsValuesTuple &tuple, vector<sai_attribute_t> &attributes);
    void freeAttribResources(vector<sai_attribute_t> &attributes);
    sai_object_id_t addQosItem(const vector<sai_attribute_t> &attributes);
    bool modifyQosItem(sai_object_id_t sai_object, vector<sai_attribute_t> &attribs);
    bool removeQosItem(sai_object_id_t sai_object);
protected:
    bool convertEcnMode(string str, sai_ecn_mark_mode_t &ecn_val);
    bool convertBool(string str, bool &val);
private:
    void appendThresholdToAttributeList(sai_attr_id_t type,
                                        sai_uint32_t threshold,
                                        bool needDefer,
                                        vector<sai_attribute_t> &normalQueue,
                                        vector<sai_attribute_t> &deferredQueue,
                                        sai_uint32_t &newThreshold);
    typedef struct {
        sai_uint32_t green_max_threshold;
        sai_uint32_t green_min_threshold;
        sai_uint32_t yellow_max_threshold;
        sai_uint32_t yellow_min_threshold;
        sai_uint32_t red_max_threshold;
        sai_uint32_t red_min_threshold;
    } qos_wred_thresholds_t;
    typedef map<string, qos_wred_thresholds_t> qos_wred_thresholds_store_t;

    static qos_wred_thresholds_store_t m_wredProfiles;
};


class TcToPgHandler : public QosMapHandler
{
public:
    bool convertFieldValuesToAttributes(KeyOpFieldsValuesTuple &tuple, vector<sai_attribute_t> &attributes);
    sai_object_id_t addQosItem(const vector<sai_attribute_t> &attributes);
};

class PfcPrioToPgHandler : public QosMapHandler
{
public:
    bool convertFieldValuesToAttributes(KeyOpFieldsValuesTuple &tuple, vector<sai_attribute_t> &attributes);
    sai_object_id_t addQosItem(const vector<sai_attribute_t> &attributes);
};

class PfcToQueueHandler : public QosMapHandler
{
public:
    bool convertFieldValuesToAttributes(KeyOpFieldsValuesTuple &tuple, vector<sai_attribute_t> &attributes);
    sai_object_id_t addQosItem(const vector<sai_attribute_t> &attributes);
};

class DscpToFcMapHandler : public QosMapHandler
{
public:
    bool convertFieldValuesToAttributes(KeyOpFieldsValuesTuple &tuple, vector<sai_attribute_t> &attributes) override;
    sai_object_id_t addQosItem(const vector<sai_attribute_t> &attributes) override;
};

class ExpToFcMapHandler : public QosMapHandler
{
public:
    bool convertFieldValuesToAttributes(KeyOpFieldsValuesTuple &tuple, vector<sai_attribute_t> &attributes) override;
    sai_object_id_t addQosItem(const vector<sai_attribute_t> &attributes) override;
};

// Handler for TC_TO_DSCP_MAP
class TcToDscpMapHandler : public QosMapHandler
{
public:
    bool convertFieldValuesToAttributes(KeyOpFieldsValuesTuple &tuple, vector<sai_attribute_t> &attributes) override;
    sai_object_id_t addQosItem(const vector<sai_attribute_t> &attributes) override;
};

class TcToDot1pMapHandler : public QosMapHandler
{
public:
    bool convertFieldValuesToAttributes(KeyOpFieldsValuesTuple &tuple, vector<sai_attribute_t> &attributes);
    sai_object_id_t addQosItem(const vector<sai_attribute_t> &attributes);
};

class QosOrch : public Orch
{
public:
    QosOrch(DBConnector *db, vector<string> &tableNames);

    static type_map& getTypeMap();
    static type_map m_qos_maps;

    sai_object_id_t resolveTunnelQosMap(std::string referencing_table_name, std::string tunnel_name, std::string map_type_name, KeyOpFieldsValuesTuple& tuple);
    void removeTunnelReference(std::string referencing_table_name, std::string tunnel_name);
private:
    void doTask() override;
    virtual void doTask(Consumer& consumer);

    typedef task_process_status (QosOrch::*qos_table_handler)(Consumer& consumer, KeyOpFieldsValuesTuple &tuple);
    typedef map<string, qos_table_handler> qos_table_handler_map;
    typedef pair<string, qos_table_handler> qos_handler_pair;

    void initTableHandlers();

    task_process_status handleDscpToTcTable(Consumer& consumer, KeyOpFieldsValuesTuple &tuple);
    task_process_status handleMplsTcToTcTable(Consumer& consumer, KeyOpFieldsValuesTuple &tuple);
    task_process_status handleDot1pToTcTable(Consumer& consumer, KeyOpFieldsValuesTuple &tuple);
    task_process_status handlePfcPrioToPgTable(Consumer& consumer, KeyOpFieldsValuesTuple &tuple);
    task_process_status handlePfcToQueueTable(Consumer& consumer, KeyOpFieldsValuesTuple &tuple);
    task_process_status handlePortQosMapTable(Consumer& consumer, KeyOpFieldsValuesTuple &tuple);
    task_process_status handleTcToPgTable(Consumer& consumer, KeyOpFieldsValuesTuple &tuple);
    task_process_status handleTcToQueueTable(Consumer& consumer, KeyOpFieldsValuesTuple &tuple);
    task_process_status handleSchedulerTable(Consumer& consumer, KeyOpFieldsValuesTuple &tuple);
    task_process_status handleQueueTable(Consumer& consumer, KeyOpFieldsValuesTuple &tuple);
    task_process_status handleWredProfileTable(Consumer& consumer, KeyOpFieldsValuesTuple &tuple);
    task_process_status handleDscpToFcTable(Consumer& consumer, KeyOpFieldsValuesTuple &tuple);
    task_process_status handleExpToFcTable(Consumer& consumer, KeyOpFieldsValuesTuple &tuple);
    task_process_status handleTcToDscpTable(Consumer& consumer, KeyOpFieldsValuesTuple &tuple);
    task_process_status handleTcToDot1pTable(Consumer& consumer, KeyOpFieldsValuesTuple &tuple);

    task_process_status handleGlobalQosMap(const string &op, KeyOpFieldsValuesTuple &tuple);

    sai_object_id_t getSchedulerGroup(const Port &port, const sai_object_id_t queue_id);

    bool applySchedulerToQueueSchedulerGroup(Port &port, size_t queue_ind, sai_object_id_t scheduler_profile_id);
    bool applyWredProfileToQueue(Port &port, size_t queue_ind, sai_object_id_t sai_wred_profile);
    bool applyDscpToTcMapToSwitch(sai_attr_id_t attr_id, sai_object_id_t sai_dscp_to_tc_map);
private:
    qos_table_handler_map m_qos_handler_map;

    struct SchedulerGroupPortInfo_t
    {
        std::vector<sai_object_id_t> groups;
        std::vector<std::vector<sai_object_id_t>> child_groups;
        std::vector<bool> group_has_been_initialized;
    };

    std::unordered_map<sai_object_id_t, SchedulerGroupPortInfo_t> m_scheduler_group_port_info;

    friend QosMapHandler;
    friend DscpToTcMapHandler;
};
#endif /* SWSS_QOSORCH_H */
