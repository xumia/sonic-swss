#include <sstream>
#include <inttypes.h>

#include "crmorch.h"
#include "converter.h"
#include "timer.h"
#include "saihelper.h"

#define CRM_POLLING_INTERVAL "polling_interval"
#define CRM_COUNTERS_TABLE_KEY "STATS"

#define CRM_POLLING_INTERVAL_DEFAULT (5 * 60)
#define CRM_THRESHOLD_TYPE_DEFAULT CrmThresholdType::CRM_PERCENTAGE
#define CRM_THRESHOLD_LOW_DEFAULT 70
#define CRM_THRESHOLD_HIGH_DEFAULT 85
#define CRM_EXCEEDED_MSG_MAX 10
#define CRM_ACL_RESOURCE_COUNT 256

extern sai_object_id_t gSwitchId;
extern sai_switch_api_t *sai_switch_api;
extern sai_acl_api_t *sai_acl_api;
extern event_handle_t g_events_handle;

using namespace std;
using namespace swss;


const map<CrmResourceType, string> crmResTypeNameMap =
{
    { CrmResourceType::CRM_IPV4_ROUTE, "IPV4_ROUTE" },
    { CrmResourceType::CRM_IPV6_ROUTE, "IPV6_ROUTE" },
    { CrmResourceType::CRM_IPV4_NEXTHOP, "IPV4_NEXTHOP" },
    { CrmResourceType::CRM_IPV6_NEXTHOP, "IPV6_NEXTHOP" },
    { CrmResourceType::CRM_IPV4_NEIGHBOR, "IPV4_NEIGHBOR" },
    { CrmResourceType::CRM_IPV6_NEIGHBOR, "IPV6_NEIGHBOR" },
    { CrmResourceType::CRM_NEXTHOP_GROUP_MEMBER, "NEXTHOP_GROUP_MEMBER" },
    { CrmResourceType::CRM_NEXTHOP_GROUP, "NEXTHOP_GROUP" },
    { CrmResourceType::CRM_ACL_TABLE, "ACL_TABLE" },
    { CrmResourceType::CRM_ACL_GROUP, "ACL_GROUP" },
    { CrmResourceType::CRM_ACL_ENTRY, "ACL_ENTRY" },
    { CrmResourceType::CRM_ACL_COUNTER, "ACL_COUNTER" },
    { CrmResourceType::CRM_FDB_ENTRY, "FDB_ENTRY" },
    { CrmResourceType::CRM_IPMC_ENTRY, "IPMC_ENTRY" },
    { CrmResourceType::CRM_SNAT_ENTRY, "SNAT_ENTRY" },
    { CrmResourceType::CRM_DNAT_ENTRY, "DNAT_ENTRY" },
    { CrmResourceType::CRM_MPLS_INSEG, "MPLS_INSEG" },
    { CrmResourceType::CRM_MPLS_NEXTHOP, "MPLS_NEXTHOP" },
    { CrmResourceType::CRM_SRV6_MY_SID_ENTRY, "SRV6_MY_SID_ENTRY" },
    { CrmResourceType::CRM_SRV6_NEXTHOP, "SRV6_NEXTHOP" },
    { CrmResourceType::CRM_NEXTHOP_GROUP_MAP, "NEXTHOP_GROUP_MAP" },
    { CrmResourceType::CRM_EXT_TABLE, "EXTENSION_TABLE" },
    { CrmResourceType::CRM_DASH_VNET, "DASH_VNET" },
    { CrmResourceType::CRM_DASH_ENI, "DASH_ENI" },
    { CrmResourceType::CRM_DASH_ENI_ETHER_ADDRESS_MAP, "DASH_ENI_ETHER_ADDRESS_MAP" },
    { CrmResourceType::CRM_DASH_IPV4_INBOUND_ROUTING, "DASH_IPV4_INBOUND_ROUTING" },
    { CrmResourceType::CRM_DASH_IPV6_INBOUND_ROUTING, "DASH_IPV6_INBOUND_ROUTING" },
    { CrmResourceType::CRM_DASH_IPV4_OUTBOUND_ROUTING, "DASH_IPV4_OUTBOUND_ROUTING" },
    { CrmResourceType::CRM_DASH_IPV6_OUTBOUND_ROUTING, "DASH_IPV6_OUTBOUND_ROUTING" },
    { CrmResourceType::CRM_DASH_IPV4_PA_VALIDATION, "DASH_IPV4_PA_VALIDATION" },
    { CrmResourceType::CRM_DASH_IPV6_PA_VALIDATION, "DASH_IPV6_PA_VALIDATION" },
    { CrmResourceType::CRM_DASH_IPV4_OUTBOUND_CA_TO_PA, "DASH_IPV4_OUTBOUND_CA_TO_PA" },
    { CrmResourceType::CRM_DASH_IPV6_OUTBOUND_CA_TO_PA, "DASH_IPV6_OUTBOUND_CA_TO_PA" },
    { CrmResourceType::CRM_DASH_IPV4_ACL_GROUP, "DASH_IPV4_ACL_GROUP" },
    { CrmResourceType::CRM_DASH_IPV6_ACL_GROUP, "DASH_IPV6_ACL_GROUP" },
    { CrmResourceType::CRM_DASH_IPV4_ACL_RULE, "DASH_IPV4_ACL_RULE" },
    { CrmResourceType::CRM_DASH_IPV6_ACL_RULE, "DASH_IPV6_ACL_RULE" },
    { CrmResourceType::CRM_TWAMP_ENTRY, "TWAMP_ENTRY" }
};

const map<CrmResourceType, uint32_t> crmResSaiAvailAttrMap =
{
    { CrmResourceType::CRM_IPV4_ROUTE, SAI_SWITCH_ATTR_AVAILABLE_IPV4_ROUTE_ENTRY },
    { CrmResourceType::CRM_IPV6_ROUTE, SAI_SWITCH_ATTR_AVAILABLE_IPV6_ROUTE_ENTRY },
    { CrmResourceType::CRM_IPV4_NEXTHOP, SAI_SWITCH_ATTR_AVAILABLE_IPV4_NEXTHOP_ENTRY },
    { CrmResourceType::CRM_IPV6_NEXTHOP, SAI_SWITCH_ATTR_AVAILABLE_IPV6_NEXTHOP_ENTRY },
    { CrmResourceType::CRM_IPV4_NEIGHBOR, SAI_SWITCH_ATTR_AVAILABLE_IPV4_NEIGHBOR_ENTRY },
    { CrmResourceType::CRM_IPV6_NEIGHBOR, SAI_SWITCH_ATTR_AVAILABLE_IPV6_NEIGHBOR_ENTRY },
    { CrmResourceType::CRM_NEXTHOP_GROUP_MEMBER, SAI_SWITCH_ATTR_AVAILABLE_NEXT_HOP_GROUP_MEMBER_ENTRY },
    { CrmResourceType::CRM_NEXTHOP_GROUP, SAI_SWITCH_ATTR_AVAILABLE_NEXT_HOP_GROUP_ENTRY },
    { CrmResourceType::CRM_ACL_TABLE, SAI_SWITCH_ATTR_AVAILABLE_ACL_TABLE },
    { CrmResourceType::CRM_ACL_GROUP, SAI_SWITCH_ATTR_AVAILABLE_ACL_TABLE_GROUP },
    { CrmResourceType::CRM_ACL_ENTRY, SAI_ACL_TABLE_ATTR_AVAILABLE_ACL_ENTRY },
    { CrmResourceType::CRM_ACL_COUNTER, SAI_ACL_TABLE_ATTR_AVAILABLE_ACL_COUNTER },
    { CrmResourceType::CRM_FDB_ENTRY, SAI_SWITCH_ATTR_AVAILABLE_FDB_ENTRY },
    { CrmResourceType::CRM_IPMC_ENTRY, SAI_SWITCH_ATTR_AVAILABLE_IPMC_ENTRY},
    { CrmResourceType::CRM_SNAT_ENTRY, SAI_SWITCH_ATTR_AVAILABLE_SNAT_ENTRY },
    { CrmResourceType::CRM_DNAT_ENTRY, SAI_SWITCH_ATTR_AVAILABLE_DNAT_ENTRY },
    { CrmResourceType::CRM_TWAMP_ENTRY, SAI_SWITCH_ATTR_AVAILABLE_TWAMP_SESSION }
};

const map<CrmResourceType, sai_object_type_t> crmResSaiObjAttrMap =
{
    { CrmResourceType::CRM_IPV4_ROUTE, SAI_OBJECT_TYPE_ROUTE_ENTRY },
    { CrmResourceType::CRM_IPV6_ROUTE, SAI_OBJECT_TYPE_ROUTE_ENTRY },
    { CrmResourceType::CRM_IPV4_NEXTHOP, SAI_OBJECT_TYPE_NULL },
    { CrmResourceType::CRM_IPV6_NEXTHOP, SAI_OBJECT_TYPE_NULL },
    { CrmResourceType::CRM_IPV4_NEIGHBOR, SAI_OBJECT_TYPE_NEIGHBOR_ENTRY },
    { CrmResourceType::CRM_IPV6_NEIGHBOR, SAI_OBJECT_TYPE_NEIGHBOR_ENTRY },
    { CrmResourceType::CRM_NEXTHOP_GROUP_MEMBER, SAI_OBJECT_TYPE_NULL },
    { CrmResourceType::CRM_NEXTHOP_GROUP, SAI_OBJECT_TYPE_NEXT_HOP_GROUP },
    { CrmResourceType::CRM_ACL_TABLE, SAI_OBJECT_TYPE_NULL },
    { CrmResourceType::CRM_ACL_GROUP, SAI_OBJECT_TYPE_NULL },
    { CrmResourceType::CRM_ACL_ENTRY, SAI_OBJECT_TYPE_NULL },
    { CrmResourceType::CRM_ACL_COUNTER, SAI_OBJECT_TYPE_NULL },
    { CrmResourceType::CRM_FDB_ENTRY, SAI_OBJECT_TYPE_FDB_ENTRY },
    { CrmResourceType::CRM_IPMC_ENTRY, SAI_OBJECT_TYPE_NULL},
    { CrmResourceType::CRM_SNAT_ENTRY, SAI_OBJECT_TYPE_NULL },
    { CrmResourceType::CRM_DNAT_ENTRY, SAI_OBJECT_TYPE_NULL },
    { CrmResourceType::CRM_MPLS_INSEG, SAI_OBJECT_TYPE_INSEG_ENTRY },
    { CrmResourceType::CRM_MPLS_NEXTHOP, SAI_OBJECT_TYPE_NEXT_HOP },
    { CrmResourceType::CRM_SRV6_MY_SID_ENTRY, SAI_OBJECT_TYPE_MY_SID_ENTRY },
    { CrmResourceType::CRM_SRV6_NEXTHOP, SAI_OBJECT_TYPE_NEXT_HOP },
    { CrmResourceType::CRM_NEXTHOP_GROUP_MAP, SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MAP },
    { CrmResourceType::CRM_EXT_TABLE, SAI_OBJECT_TYPE_GENERIC_PROGRAMMABLE },
    { CrmResourceType::CRM_DASH_VNET, (sai_object_type_t)SAI_OBJECT_TYPE_VNET },
    { CrmResourceType::CRM_DASH_ENI, (sai_object_type_t)SAI_OBJECT_TYPE_ENI },
    { CrmResourceType::CRM_DASH_ENI_ETHER_ADDRESS_MAP, (sai_object_type_t)SAI_OBJECT_TYPE_ENI_ETHER_ADDRESS_MAP_ENTRY },
    { CrmResourceType::CRM_DASH_IPV4_INBOUND_ROUTING, (sai_object_type_t)SAI_OBJECT_TYPE_INBOUND_ROUTING_ENTRY },
    { CrmResourceType::CRM_DASH_IPV6_INBOUND_ROUTING, (sai_object_type_t)SAI_OBJECT_TYPE_INBOUND_ROUTING_ENTRY },
    { CrmResourceType::CRM_DASH_IPV4_OUTBOUND_ROUTING, (sai_object_type_t)SAI_OBJECT_TYPE_OUTBOUND_ROUTING_ENTRY },
    { CrmResourceType::CRM_DASH_IPV6_OUTBOUND_ROUTING, (sai_object_type_t)SAI_OBJECT_TYPE_OUTBOUND_ROUTING_ENTRY },
    { CrmResourceType::CRM_DASH_IPV4_PA_VALIDATION, (sai_object_type_t)SAI_OBJECT_TYPE_PA_VALIDATION_ENTRY },
    { CrmResourceType::CRM_DASH_IPV6_PA_VALIDATION, (sai_object_type_t)SAI_OBJECT_TYPE_PA_VALIDATION_ENTRY },
    { CrmResourceType::CRM_DASH_IPV4_OUTBOUND_CA_TO_PA, (sai_object_type_t)SAI_OBJECT_TYPE_OUTBOUND_CA_TO_PA_ENTRY },
    { CrmResourceType::CRM_DASH_IPV6_OUTBOUND_CA_TO_PA, (sai_object_type_t)SAI_OBJECT_TYPE_OUTBOUND_CA_TO_PA_ENTRY },
    { CrmResourceType::CRM_DASH_IPV4_ACL_GROUP, (sai_object_type_t)SAI_OBJECT_TYPE_DASH_ACL_GROUP },
    { CrmResourceType::CRM_DASH_IPV6_ACL_GROUP, (sai_object_type_t)SAI_OBJECT_TYPE_DASH_ACL_GROUP },
    { CrmResourceType::CRM_DASH_IPV4_ACL_RULE, (sai_object_type_t)SAI_OBJECT_TYPE_DASH_ACL_RULE },
    { CrmResourceType::CRM_DASH_IPV6_ACL_RULE, (sai_object_type_t)SAI_OBJECT_TYPE_DASH_ACL_RULE },
    { CrmResourceType::CRM_TWAMP_ENTRY, SAI_OBJECT_TYPE_NULL }
};

const map<CrmResourceType, sai_attr_id_t> crmResAddrFamilyAttrMap =
{
    { CrmResourceType::CRM_IPV4_ROUTE, SAI_ROUTE_ENTRY_ATTR_IP_ADDR_FAMILY },
    { CrmResourceType::CRM_IPV6_ROUTE, SAI_ROUTE_ENTRY_ATTR_IP_ADDR_FAMILY },
    { CrmResourceType::CRM_IPV4_NEIGHBOR, SAI_NEIGHBOR_ENTRY_ATTR_IP_ADDR_FAMILY },
    { CrmResourceType::CRM_IPV6_NEIGHBOR, SAI_NEIGHBOR_ENTRY_ATTR_IP_ADDR_FAMILY },
    { CrmResourceType::CRM_DASH_IPV4_ACL_GROUP, SAI_DASH_ACL_GROUP_ATTR_IP_ADDR_FAMILY },
    { CrmResourceType::CRM_DASH_IPV6_ACL_GROUP, SAI_DASH_ACL_GROUP_ATTR_IP_ADDR_FAMILY },
};

const map<CrmResourceType, sai_ip_addr_family_t> crmResAddrFamilyValMap =
{
    { CrmResourceType::CRM_IPV4_ROUTE, SAI_IP_ADDR_FAMILY_IPV4 },
    { CrmResourceType::CRM_IPV6_ROUTE, SAI_IP_ADDR_FAMILY_IPV6 },
    { CrmResourceType::CRM_IPV4_NEIGHBOR, SAI_IP_ADDR_FAMILY_IPV4 },
    { CrmResourceType::CRM_IPV6_NEIGHBOR, SAI_IP_ADDR_FAMILY_IPV6 },
    { CrmResourceType::CRM_DASH_IPV4_ACL_GROUP, SAI_IP_ADDR_FAMILY_IPV4 },
    { CrmResourceType::CRM_DASH_IPV6_ACL_GROUP, SAI_IP_ADDR_FAMILY_IPV6 },
};

const map<string, CrmResourceType> crmThreshTypeResMap =
{
    { "ipv4_route_threshold_type", CrmResourceType::CRM_IPV4_ROUTE },
    { "ipv6_route_threshold_type", CrmResourceType::CRM_IPV6_ROUTE },
    { "ipv4_nexthop_threshold_type", CrmResourceType::CRM_IPV4_NEXTHOP },
    { "ipv6_nexthop_threshold_type", CrmResourceType::CRM_IPV6_NEXTHOP },
    { "ipv4_neighbor_threshold_type", CrmResourceType::CRM_IPV4_NEIGHBOR },
    { "ipv6_neighbor_threshold_type", CrmResourceType::CRM_IPV6_NEIGHBOR },
    { "nexthop_group_member_threshold_type", CrmResourceType::CRM_NEXTHOP_GROUP_MEMBER },
    { "nexthop_group_threshold_type", CrmResourceType::CRM_NEXTHOP_GROUP },
    { "acl_table_threshold_type", CrmResourceType::CRM_ACL_TABLE },
    { "acl_group_threshold_type", CrmResourceType::CRM_ACL_GROUP },
    { "acl_entry_threshold_type", CrmResourceType::CRM_ACL_ENTRY },
    { "acl_counter_threshold_type", CrmResourceType::CRM_ACL_COUNTER },
    { "fdb_entry_threshold_type", CrmResourceType::CRM_FDB_ENTRY },
    { "ipmc_entry_threshold_type", CrmResourceType::CRM_IPMC_ENTRY },
    { "snat_entry_threshold_type", CrmResourceType::CRM_SNAT_ENTRY },
    { "dnat_entry_threshold_type", CrmResourceType::CRM_DNAT_ENTRY },
    { "mpls_inseg_threshold_type", CrmResourceType::CRM_MPLS_INSEG },
    { "mpls_nexthop_threshold_type", CrmResourceType::CRM_MPLS_NEXTHOP },
    { "srv6_my_sid_entry_threshold_type", CrmResourceType::CRM_SRV6_MY_SID_ENTRY },
    { "srv6_nexthop_threshold_type", CrmResourceType::CRM_SRV6_NEXTHOP },
    { "nexthop_group_map_threshold_type", CrmResourceType::CRM_NEXTHOP_GROUP_MAP },
    { "extension_table_threshold_type", CrmResourceType::CRM_EXT_TABLE },
    { "dash_vnet_threshold_type", CrmResourceType::CRM_DASH_VNET },
    { "dash_eni_threshold_type", CrmResourceType:: CRM_DASH_ENI },
    { "dash_eni_ether_address_map_threshold_type", CrmResourceType::CRM_DASH_ENI_ETHER_ADDRESS_MAP },
    { "dash_ipv4_inbound_routing_threshold_type", CrmResourceType::CRM_DASH_IPV4_INBOUND_ROUTING },
    { "dash_ipv6_inbound_routing_threshold_type", CrmResourceType::CRM_DASH_IPV6_INBOUND_ROUTING },
    { "dash_ipv4_outbound_routing_threshold_type", CrmResourceType::CRM_DASH_IPV4_OUTBOUND_ROUTING },
    { "dash_ipv6_outbound_routing_threshold_type", CrmResourceType::CRM_DASH_IPV6_OUTBOUND_ROUTING },
    { "dash_ipv4_pa_validation_threshold_type", CrmResourceType::CRM_DASH_IPV4_PA_VALIDATION },
    { "dash_ipv6_pa_validation_threshold_type", CrmResourceType::CRM_DASH_IPV6_PA_VALIDATION },
    { "dash_ipv4_outbound_ca_to_pa_threshold_type", CrmResourceType::CRM_DASH_IPV4_OUTBOUND_CA_TO_PA },
    { "dash_ipv6_outbound_ca_to_pa_threshold_type", CrmResourceType::CRM_DASH_IPV6_OUTBOUND_CA_TO_PA },
    { "dash_ipv4_acl_group_threshold_type", CrmResourceType::CRM_DASH_IPV4_ACL_GROUP },
    { "dash_ipv6_acl_group_threshold_type", CrmResourceType::CRM_DASH_IPV6_ACL_GROUP },
    { "dash_ipv4_acl_rule_threshold_type", CrmResourceType::CRM_DASH_IPV4_ACL_RULE },
    { "dash_ipv6_acl_rule_threshold_type", CrmResourceType::CRM_DASH_IPV6_ACL_RULE },
    { "twamp_entry_threshold_type", CrmResourceType::CRM_TWAMP_ENTRY }
};

const map<string, CrmResourceType> crmThreshLowResMap =
{
    {"ipv4_route_low_threshold", CrmResourceType::CRM_IPV4_ROUTE },
    {"ipv6_route_low_threshold", CrmResourceType::CRM_IPV6_ROUTE },
    {"ipv4_nexthop_low_threshold", CrmResourceType::CRM_IPV4_NEXTHOP },
    {"ipv6_nexthop_low_threshold", CrmResourceType::CRM_IPV6_NEXTHOP },
    {"ipv4_neighbor_low_threshold", CrmResourceType::CRM_IPV4_NEIGHBOR },
    {"ipv6_neighbor_low_threshold", CrmResourceType::CRM_IPV6_NEIGHBOR },
    {"nexthop_group_member_low_threshold", CrmResourceType::CRM_NEXTHOP_GROUP_MEMBER },
    {"nexthop_group_low_threshold", CrmResourceType::CRM_NEXTHOP_GROUP },
    {"acl_table_low_threshold", CrmResourceType::CRM_ACL_TABLE },
    {"acl_group_low_threshold", CrmResourceType::CRM_ACL_GROUP },
    {"acl_entry_low_threshold", CrmResourceType::CRM_ACL_ENTRY },
    {"acl_counter_low_threshold", CrmResourceType::CRM_ACL_COUNTER },
    {"fdb_entry_low_threshold", CrmResourceType::CRM_FDB_ENTRY },
    {"ipmc_entry_low_threshold", CrmResourceType::CRM_IPMC_ENTRY },
    {"snat_entry_low_threshold", CrmResourceType::CRM_SNAT_ENTRY },
    {"dnat_entry_low_threshold", CrmResourceType::CRM_DNAT_ENTRY },
    {"mpls_inseg_low_threshold", CrmResourceType::CRM_MPLS_INSEG },
    {"mpls_nexthop_low_threshold", CrmResourceType::CRM_MPLS_NEXTHOP },
    {"srv6_my_sid_entry_low_threshold", CrmResourceType::CRM_SRV6_MY_SID_ENTRY },
    {"srv6_nexthop_low_threshold", CrmResourceType::CRM_SRV6_NEXTHOP },
    {"nexthop_group_map_low_threshold", CrmResourceType::CRM_NEXTHOP_GROUP_MAP },
    {"extension_table_low_threshold", CrmResourceType::CRM_EXT_TABLE },
    { "dash_vnet_low_threshold", CrmResourceType::CRM_DASH_VNET },
    { "dash_eni_low_threshold", CrmResourceType:: CRM_DASH_ENI },
    { "dash_eni_ether_address_map_low_threshold", CrmResourceType::CRM_DASH_ENI_ETHER_ADDRESS_MAP },
    { "dash_ipv4_inbound_routing_low_threshold", CrmResourceType::CRM_DASH_IPV4_INBOUND_ROUTING },
    { "dash_ipv6_inbound_routing_low_threshold", CrmResourceType::CRM_DASH_IPV6_INBOUND_ROUTING },
    { "dash_ipv4_outbound_routing_low_threshold", CrmResourceType::CRM_DASH_IPV4_OUTBOUND_ROUTING },
    { "dash_ipv6_outbound_routing_low_threshold", CrmResourceType::CRM_DASH_IPV6_OUTBOUND_ROUTING },
    { "dash_ipv4_pa_validation_low_threshold", CrmResourceType::CRM_DASH_IPV4_PA_VALIDATION },
    { "dash_ipv6_pa_validation_low_threshold", CrmResourceType::CRM_DASH_IPV6_PA_VALIDATION },
    { "dash_ipv4_outbound_ca_to_pa_low_threshold", CrmResourceType::CRM_DASH_IPV4_OUTBOUND_CA_TO_PA },
    { "dash_ipv6_outbound_ca_to_pa_low_threshold", CrmResourceType::CRM_DASH_IPV6_OUTBOUND_CA_TO_PA },
    { "dash_ipv4_acl_group_low_threshold", CrmResourceType::CRM_DASH_IPV4_ACL_GROUP },
    { "dash_ipv6_acl_group_low_threshold", CrmResourceType::CRM_DASH_IPV6_ACL_GROUP },
    { "dash_ipv4_acl_rule_low_threshold", CrmResourceType::CRM_DASH_IPV4_ACL_RULE },
    { "dash_ipv6_acl_rule_low_threshold", CrmResourceType::CRM_DASH_IPV6_ACL_RULE },
    { "twamp_entry_low_threshold", CrmResourceType::CRM_TWAMP_ENTRY }
};

const map<string, CrmResourceType> crmThreshHighResMap =
{
    {"ipv4_route_high_threshold", CrmResourceType::CRM_IPV4_ROUTE },
    {"ipv6_route_high_threshold", CrmResourceType::CRM_IPV6_ROUTE },
    {"ipv4_nexthop_high_threshold", CrmResourceType::CRM_IPV4_NEXTHOP },
    {"ipv6_nexthop_high_threshold", CrmResourceType::CRM_IPV6_NEXTHOP },
    {"ipv4_neighbor_high_threshold", CrmResourceType::CRM_IPV4_NEIGHBOR },
    {"ipv6_neighbor_high_threshold", CrmResourceType::CRM_IPV6_NEIGHBOR },
    {"nexthop_group_member_high_threshold", CrmResourceType::CRM_NEXTHOP_GROUP_MEMBER },
    {"nexthop_group_high_threshold", CrmResourceType::CRM_NEXTHOP_GROUP },
    {"acl_table_high_threshold", CrmResourceType::CRM_ACL_TABLE },
    {"acl_group_high_threshold", CrmResourceType::CRM_ACL_GROUP },
    {"acl_entry_high_threshold", CrmResourceType::CRM_ACL_ENTRY },
    {"acl_counter_high_threshold", CrmResourceType::CRM_ACL_COUNTER },
    {"fdb_entry_high_threshold", CrmResourceType::CRM_FDB_ENTRY },
    {"ipmc_entry_high_threshold", CrmResourceType::CRM_IPMC_ENTRY },
    {"snat_entry_high_threshold", CrmResourceType::CRM_SNAT_ENTRY },
    {"dnat_entry_high_threshold", CrmResourceType::CRM_DNAT_ENTRY },
    {"mpls_inseg_high_threshold", CrmResourceType::CRM_MPLS_INSEG },
    {"mpls_nexthop_high_threshold", CrmResourceType::CRM_MPLS_NEXTHOP },
    {"srv6_my_sid_entry_high_threshold", CrmResourceType::CRM_SRV6_MY_SID_ENTRY },
    {"srv6_nexthop_high_threshold", CrmResourceType::CRM_SRV6_NEXTHOP },
    {"nexthop_group_map_high_threshold", CrmResourceType::CRM_NEXTHOP_GROUP_MAP },
    {"extension_table_high_threshold", CrmResourceType::CRM_EXT_TABLE },
    { "dash_vnet_high_threshold", CrmResourceType::CRM_DASH_VNET },
    { "dash_eni_high_threshold", CrmResourceType:: CRM_DASH_ENI },
    { "dash_eni_ether_address_map_high_threshold", CrmResourceType::CRM_DASH_ENI_ETHER_ADDRESS_MAP },
    { "dash_ipv4_inbound_routing_high_threshold", CrmResourceType::CRM_DASH_IPV4_INBOUND_ROUTING },
    { "dash_ipv6_inbound_routing_high_threshold", CrmResourceType::CRM_DASH_IPV6_INBOUND_ROUTING },
    { "dash_ipv4_outbound_routing_high_threshold", CrmResourceType::CRM_DASH_IPV4_OUTBOUND_ROUTING },
    { "dash_ipv6_outbound_routing_high_threshold", CrmResourceType::CRM_DASH_IPV6_OUTBOUND_ROUTING },
    { "dash_ipv4_pa_validation_high_threshold", CrmResourceType::CRM_DASH_IPV4_PA_VALIDATION },
    { "dash_ipv6_pa_validation_high_threshold", CrmResourceType::CRM_DASH_IPV6_PA_VALIDATION },
    { "dash_ipv4_outbound_ca_to_pa_high_threshold", CrmResourceType::CRM_DASH_IPV4_OUTBOUND_CA_TO_PA },
    { "dash_ipv6_outbound_ca_to_pa_high_threshold", CrmResourceType::CRM_DASH_IPV6_OUTBOUND_CA_TO_PA },
    { "dash_ipv4_acl_group_high_threshold", CrmResourceType::CRM_DASH_IPV4_ACL_GROUP },
    { "dash_ipv6_acl_group_high_threshold", CrmResourceType::CRM_DASH_IPV6_ACL_GROUP },
    { "dash_ipv4_acl_rule_high_threshold", CrmResourceType::CRM_DASH_IPV4_ACL_RULE },
    { "dash_ipv6_acl_rule_high_threshold", CrmResourceType::CRM_DASH_IPV6_ACL_RULE },
    { "twamp_entry_high_threshold", CrmResourceType::CRM_TWAMP_ENTRY }
};

const map<string, CrmThresholdType> crmThreshTypeMap =
{
    { "percentage", CrmThresholdType::CRM_PERCENTAGE },
    { "used", CrmThresholdType::CRM_USED },
    { "free", CrmThresholdType::CRM_FREE }
};

const map<string, CrmResourceType> crmAvailCntsTableMap =
{
    { "crm_stats_ipv4_route_available", CrmResourceType::CRM_IPV4_ROUTE },
    { "crm_stats_ipv6_route_available", CrmResourceType::CRM_IPV6_ROUTE },
    { "crm_stats_ipv4_nexthop_available", CrmResourceType::CRM_IPV4_NEXTHOP },
    { "crm_stats_ipv6_nexthop_available", CrmResourceType::CRM_IPV6_NEXTHOP },
    { "crm_stats_ipv4_neighbor_available", CrmResourceType::CRM_IPV4_NEIGHBOR },
    { "crm_stats_ipv6_neighbor_available", CrmResourceType::CRM_IPV6_NEIGHBOR },
    { "crm_stats_nexthop_group_member_available", CrmResourceType::CRM_NEXTHOP_GROUP_MEMBER },
    { "crm_stats_nexthop_group_available", CrmResourceType::CRM_NEXTHOP_GROUP },
    { "crm_stats_acl_table_available", CrmResourceType::CRM_ACL_TABLE },
    { "crm_stats_acl_group_available", CrmResourceType::CRM_ACL_GROUP },
    { "crm_stats_acl_entry_available", CrmResourceType::CRM_ACL_ENTRY },
    { "crm_stats_acl_counter_available", CrmResourceType::CRM_ACL_COUNTER },
    { "crm_stats_fdb_entry_available", CrmResourceType::CRM_FDB_ENTRY },
    { "crm_stats_ipmc_entry_available", CrmResourceType::CRM_IPMC_ENTRY },
    { "crm_stats_snat_entry_available", CrmResourceType::CRM_SNAT_ENTRY },
    { "crm_stats_dnat_entry_available", CrmResourceType::CRM_DNAT_ENTRY },
    { "crm_stats_mpls_inseg_available", CrmResourceType::CRM_MPLS_INSEG },
    { "crm_stats_mpls_nexthop_available", CrmResourceType::CRM_MPLS_NEXTHOP },
    { "crm_stats_srv6_my_sid_entry_available", CrmResourceType::CRM_SRV6_MY_SID_ENTRY },
    { "crm_stats_srv6_nexthop_available", CrmResourceType::CRM_SRV6_NEXTHOP },
    { "crm_stats_nexthop_group_map_available", CrmResourceType::CRM_NEXTHOP_GROUP_MAP },
    { "crm_stats_extension_table_available", CrmResourceType::CRM_EXT_TABLE },
    { "crm_stats_dash_vnet_available", CrmResourceType::CRM_DASH_VNET },
    { "crm_stats_dash_eni_available", CrmResourceType:: CRM_DASH_ENI },
    { "crm_stats_dash_eni_ether_address_map_available", CrmResourceType::CRM_DASH_ENI_ETHER_ADDRESS_MAP },
    { "crm_stats_dash_ipv4_inbound_routing_available", CrmResourceType::CRM_DASH_IPV4_INBOUND_ROUTING },
    { "crm_stats_dash_ipv6_inbound_routing_available", CrmResourceType::CRM_DASH_IPV6_INBOUND_ROUTING },
    { "crm_stats_dash_ipv4_outbound_routing_available", CrmResourceType::CRM_DASH_IPV4_OUTBOUND_ROUTING },
    { "crm_stats_dash_ipv6_outbound_routing_available", CrmResourceType::CRM_DASH_IPV6_OUTBOUND_ROUTING },
    { "crm_stats_dash_ipv4_pa_validation_available", CrmResourceType::CRM_DASH_IPV4_PA_VALIDATION },
    { "crm_stats_dash_ipv6_pa_validation_available", CrmResourceType::CRM_DASH_IPV6_PA_VALIDATION },
    { "crm_stats_dash_ipv4_outbound_ca_to_pa_available", CrmResourceType::CRM_DASH_IPV4_OUTBOUND_CA_TO_PA },
    { "crm_stats_dash_ipv6_outbound_ca_to_pa_available", CrmResourceType::CRM_DASH_IPV6_OUTBOUND_CA_TO_PA },
    { "crm_stats_dash_ipv4_acl_group_available", CrmResourceType::CRM_DASH_IPV4_ACL_GROUP },
    { "crm_stats_dash_ipv6_acl_group_available", CrmResourceType::CRM_DASH_IPV6_ACL_GROUP },
    { "crm_stats_dash_ipv4_acl_rule_available", CrmResourceType::CRM_DASH_IPV4_ACL_RULE },
    { "crm_stats_dash_ipv6_acl_rule_available", CrmResourceType::CRM_DASH_IPV6_ACL_RULE },
    { "crm_stats_twamp_entry_available", CrmResourceType::CRM_TWAMP_ENTRY }
};

const map<string, CrmResourceType> crmUsedCntsTableMap =
{
    { "crm_stats_ipv4_route_used", CrmResourceType::CRM_IPV4_ROUTE },
    { "crm_stats_ipv6_route_used", CrmResourceType::CRM_IPV6_ROUTE },
    { "crm_stats_ipv4_nexthop_used", CrmResourceType::CRM_IPV4_NEXTHOP },
    { "crm_stats_ipv6_nexthop_used", CrmResourceType::CRM_IPV6_NEXTHOP },
    { "crm_stats_ipv4_neighbor_used", CrmResourceType::CRM_IPV4_NEIGHBOR },
    { "crm_stats_ipv6_neighbor_used", CrmResourceType::CRM_IPV6_NEIGHBOR },
    { "crm_stats_nexthop_group_member_used", CrmResourceType::CRM_NEXTHOP_GROUP_MEMBER },
    { "crm_stats_nexthop_group_used", CrmResourceType::CRM_NEXTHOP_GROUP },
    { "crm_stats_acl_table_used", CrmResourceType::CRM_ACL_TABLE },
    { "crm_stats_acl_group_used", CrmResourceType::CRM_ACL_GROUP },
    { "crm_stats_acl_entry_used", CrmResourceType::CRM_ACL_ENTRY },
    { "crm_stats_acl_counter_used", CrmResourceType::CRM_ACL_COUNTER },
    { "crm_stats_fdb_entry_used", CrmResourceType::CRM_FDB_ENTRY },
    { "crm_stats_ipmc_entry_used", CrmResourceType::CRM_IPMC_ENTRY },
    { "crm_stats_snat_entry_used", CrmResourceType::CRM_SNAT_ENTRY },
    { "crm_stats_dnat_entry_used", CrmResourceType::CRM_DNAT_ENTRY },
    { "crm_stats_mpls_inseg_used", CrmResourceType::CRM_MPLS_INSEG },
    { "crm_stats_mpls_nexthop_used", CrmResourceType::CRM_MPLS_NEXTHOP },
    { "crm_stats_srv6_my_sid_entry_used", CrmResourceType::CRM_SRV6_MY_SID_ENTRY },
    { "crm_stats_srv6_nexthop_used", CrmResourceType::CRM_SRV6_NEXTHOP },
    { "crm_stats_nexthop_group_map_used", CrmResourceType::CRM_NEXTHOP_GROUP_MAP },
    { "crm_stats_extension_table_used", CrmResourceType::CRM_EXT_TABLE },
    { "crm_stats_dash_vnet_used", CrmResourceType::CRM_DASH_VNET },
    { "crm_stats_dash_eni_used", CrmResourceType:: CRM_DASH_ENI },
    { "crm_stats_dash_eni_ether_address_map_used", CrmResourceType::CRM_DASH_ENI_ETHER_ADDRESS_MAP },
    { "crm_stats_dash_ipv4_inbound_routing_used", CrmResourceType::CRM_DASH_IPV4_INBOUND_ROUTING },
    { "crm_stats_dash_ipv6_inbound_routing_used", CrmResourceType::CRM_DASH_IPV6_INBOUND_ROUTING },
    { "crm_stats_dash_ipv4_outbound_routing_used", CrmResourceType::CRM_DASH_IPV4_OUTBOUND_ROUTING },
    { "crm_stats_dash_ipv6_outbound_routing_used", CrmResourceType::CRM_DASH_IPV6_OUTBOUND_ROUTING },
    { "crm_stats_dash_ipv4_pa_validation_used", CrmResourceType::CRM_DASH_IPV4_PA_VALIDATION },
    { "crm_stats_dash_ipv6_pa_validation_used", CrmResourceType::CRM_DASH_IPV6_PA_VALIDATION },
    { "crm_stats_dash_ipv4_outbound_ca_to_pa_used", CrmResourceType::CRM_DASH_IPV4_OUTBOUND_CA_TO_PA },
    { "crm_stats_dash_ipv6_outbound_ca_to_pa_used", CrmResourceType::CRM_DASH_IPV6_OUTBOUND_CA_TO_PA },
    { "crm_stats_dash_ipv4_acl_group_used", CrmResourceType::CRM_DASH_IPV4_ACL_GROUP },
    { "crm_stats_dash_ipv6_acl_group_used", CrmResourceType::CRM_DASH_IPV6_ACL_GROUP },
    { "crm_stats_dash_ipv4_acl_rule_used", CrmResourceType::CRM_DASH_IPV4_ACL_RULE },
    { "crm_stats_dash_ipv6_acl_rule_used", CrmResourceType::CRM_DASH_IPV6_ACL_RULE },
    { "crm_stats_twamp_entry_used", CrmResourceType::CRM_TWAMP_ENTRY },
};

CrmOrch::CrmOrch(DBConnector *db, string tableName):
    Orch(db, tableName),
    m_countersDb(new DBConnector("COUNTERS_DB", 0)),
    m_countersCrmTable(new Table(m_countersDb.get(), COUNTERS_CRM_TABLE)),
    m_timer(new SelectableTimer(timespec { .tv_sec = CRM_POLLING_INTERVAL_DEFAULT, .tv_nsec = 0 }))
{
    SWSS_LOG_ENTER();

    m_pollingInterval = chrono::seconds(CRM_POLLING_INTERVAL_DEFAULT);

    for (const auto &res : crmResTypeNameMap)
    {
        m_resourcesMap.emplace(res.first, CrmResourceEntry(res.second, CRM_THRESHOLD_TYPE_DEFAULT, CRM_THRESHOLD_LOW_DEFAULT, CRM_THRESHOLD_HIGH_DEFAULT));
    }

    // The CRM stats needs to be populated again
    m_countersCrmTable->del(CRM_COUNTERS_TABLE_KEY);

    // Note: ExecutableTimer will hold m_timer pointer and release the object later
    auto executor = new ExecutableTimer(m_timer, this, "CRM_COUNTERS_POLL");
    Orch::addExecutor(executor);
    m_timer->start();
}

CrmOrch::CrmResourceEntry::CrmResourceEntry(string name, CrmThresholdType thresholdType, uint32_t lowThreshold, uint32_t highThreshold):
    name(name),
    thresholdType(thresholdType),
    lowThreshold(lowThreshold),
    highThreshold(highThreshold)
{
    if ((thresholdType == CrmThresholdType::CRM_PERCENTAGE) && ((lowThreshold > 100) || (highThreshold > 100)))
    {
        throw runtime_error("CRM percentage threshold value must be <= 100%%");
    }

    if (!(lowThreshold < highThreshold))
    {
        throw runtime_error("CRM low threshold must be less then high threshold");
    }

}

void CrmOrch::doTask(Consumer &consumer)
{
    SWSS_LOG_ENTER();

    string table_name = consumer.getTableName();

    if (table_name != CFG_CRM_TABLE_NAME)
    {
        SWSS_LOG_ERROR("Invalid table %s", table_name.c_str());
    }

    auto it = consumer.m_toSync.begin();
    while (it != consumer.m_toSync.end())
    {
        KeyOpFieldsValuesTuple t = it->second;

        string key = kfvKey(t);
        string op = kfvOp(t);

        if (op == SET_COMMAND)
        {
            handleSetCommand(key, kfvFieldsValues(t));
        }
        else if (op == DEL_COMMAND)
        {
            SWSS_LOG_ERROR("Unsupported operation type %s\n", op.c_str());
        }
        else
        {
            SWSS_LOG_ERROR("Unknown operation type %s\n", op.c_str());
        }

        consumer.m_toSync.erase(it++);
    }
}

void CrmOrch::handleSetCommand(const string& key, const vector<FieldValueTuple>& data)
{
    SWSS_LOG_ENTER();

    for (auto i : data)
    {
        const auto &field = fvField(i);
        const auto &value = fvValue(i);

        try
        {
            if (field == CRM_POLLING_INTERVAL)
            {
                m_pollingInterval = chrono::seconds(to_uint<uint32_t>(value));
                auto interv = timespec { .tv_sec = (time_t)m_pollingInterval.count(), .tv_nsec = 0 };
                m_timer->setInterval(interv);
                m_timer->reset();
            }
            else if (crmThreshTypeResMap.find(field) != crmThreshTypeResMap.end())
            {
                auto thresholdType = crmThreshTypeMap.at(value);
                auto resourceType = crmThreshTypeResMap.at(field);
                auto &resource = m_resourcesMap.at(resourceType);

                if (resource.thresholdType != thresholdType)
                {
                    resource.thresholdType = thresholdType;

                    for (auto &cnt : resource.countersMap)
                    {
                        cnt.second.exceededLogCounter = 0;
                    }
                }
            }
            else if (crmThreshLowResMap.find(field) != crmThreshLowResMap.end())
            {
                auto resourceType = crmThreshLowResMap.at(field);
                auto thresholdValue = to_uint<uint32_t>(value);

                m_resourcesMap.at(resourceType).lowThreshold = thresholdValue;
            }
            else if (crmThreshHighResMap.find(field) != crmThreshHighResMap.end())
            {
                auto resourceType = crmThreshHighResMap.at(field);
                auto thresholdValue = to_uint<uint32_t>(value);

                m_resourcesMap.at(resourceType).highThreshold = thresholdValue;
            }
            else
            {
                SWSS_LOG_ERROR("Failed to parse CRM %s configuration. Unknown attribute %s.\n", key.c_str(), field.c_str());
            }
        }
        catch (const exception& e)
        {
            SWSS_LOG_ERROR("Failed to parse CRM %s attribute %s error: %s.", key.c_str(), field.c_str(), e.what());
            return;
        }
        catch (...)
        {
            SWSS_LOG_ERROR("Failed to parse CRM %s attribute %s. Unknown error has been occurred", key.c_str(), field.c_str());
            return;
        }
    }
}

void CrmOrch::incCrmResUsedCounter(CrmResourceType resource)
{
    SWSS_LOG_ENTER();

    try
    {
        m_resourcesMap.at(resource).countersMap[CRM_COUNTERS_TABLE_KEY].usedCounter++;
    }
    catch (...)
    {
        SWSS_LOG_ERROR("Failed to increment \"used\" counter for the %s CRM resource.", crmResTypeNameMap.at(resource).c_str());
        return;
    }
}

void CrmOrch::decCrmResUsedCounter(CrmResourceType resource)
{
    SWSS_LOG_ENTER();

    try
    {
        m_resourcesMap.at(resource).countersMap[CRM_COUNTERS_TABLE_KEY].usedCounter--;
    }
    catch (...)
    {
        SWSS_LOG_ERROR("Failed to decrement \"used\" counter for the %s CRM resource.", crmResTypeNameMap.at(resource).c_str());
        return;
    }
}

void CrmOrch::incCrmAclUsedCounter(CrmResourceType resource, sai_acl_stage_t stage, sai_acl_bind_point_type_t point)
{
    SWSS_LOG_ENTER();

    try
    {
        m_resourcesMap.at(resource).countersMap[getCrmAclKey(stage, point)].usedCounter++;
    }
    catch (...)
    {
        SWSS_LOG_ERROR("Failed to increment \"used\" counter for the %s CRM resource.", crmResTypeNameMap.at(resource).c_str());
        return;
    }
}

void CrmOrch::decCrmAclUsedCounter(CrmResourceType resource, sai_acl_stage_t stage, sai_acl_bind_point_type_t point, sai_object_id_t oid)
{
    SWSS_LOG_ENTER();

    try
    {
        m_resourcesMap.at(resource).countersMap[getCrmAclKey(stage, point)].usedCounter--;

        // remove acl_entry and acl_counter in this acl table
        if (resource == CrmResourceType::CRM_ACL_TABLE)
        {
            for (auto &resourcesMap : m_resourcesMap)
            {
                if ((resourcesMap.first == (CrmResourceType::CRM_ACL_ENTRY))
                    || (resourcesMap.first == (CrmResourceType::CRM_ACL_COUNTER)))
                {
                    auto &cntMap = resourcesMap.second.countersMap;
                    for (auto it = cntMap.begin(); it != cntMap.end(); ++it)
                    {
                        if (it->second.id == oid)
                        {
                            cntMap.erase(it);
                            break;
                        }
                    }
                }
            }

            // remove ACL_TABLE_STATS in crm database
            m_countersCrmTable->del(getCrmAclTableKey(oid));
        }
    }
    catch (...)
    {
        SWSS_LOG_ERROR("Failed to decrement \"used\" counter for the %s CRM resource.", crmResTypeNameMap.at(resource).c_str());
        return;
    }
}

void CrmOrch::incCrmAclTableUsedCounter(CrmResourceType resource, sai_object_id_t tableId)
{
    SWSS_LOG_ENTER();

    try
    {
        m_resourcesMap.at(resource).countersMap[getCrmAclTableKey(tableId)].usedCounter++;
        m_resourcesMap.at(resource).countersMap[getCrmAclTableKey(tableId)].id = tableId;
    }
    catch (...)
    {
        SWSS_LOG_ERROR("Failed to increment \"used\" counter for the %s CRM resource (tableId:%" PRIx64 ").", crmResTypeNameMap.at(resource).c_str(), tableId);
        return;
    }
}

void CrmOrch::decCrmAclTableUsedCounter(CrmResourceType resource, sai_object_id_t tableId)
{
    SWSS_LOG_ENTER();

    try
    {
        m_resourcesMap.at(resource).countersMap[getCrmAclTableKey(tableId)].usedCounter--;
    }
    catch (...)
    {
        SWSS_LOG_ERROR("Failed to decrement \"used\" counter for the %s CRM resource (tableId:%" PRIx64 ").", crmResTypeNameMap.at(resource).c_str(), tableId);
        return;
    }
}

void CrmOrch::incCrmExtTableUsedCounter(CrmResourceType resource, std::string table_name)
{
    SWSS_LOG_ENTER();

    try
    {
        m_resourcesMap.at(resource).countersMap[getCrmP4rtTableKey(table_name)].usedCounter++;
    }
    catch (...)
    {
        SWSS_LOG_ERROR("Failed to increment \"used\" counter for the EXT %s CRM resource.", table_name.c_str());
        return;
    }
}

void CrmOrch::decCrmExtTableUsedCounter(CrmResourceType resource, std::string table_name)
{
    SWSS_LOG_ENTER();

    try
    {
        m_resourcesMap.at(resource).countersMap[getCrmP4rtTableKey(table_name)].usedCounter--;
    }
    catch (...)
    {
        SWSS_LOG_ERROR("Failed to decrement \"used\" counter for the EXT %s CRM resource.", table_name.c_str());
        return;
    }
}

void CrmOrch::incCrmDashAclUsedCounter(CrmResourceType resource, sai_object_id_t tableId)
{
    SWSS_LOG_ENTER();

    try
    {
        if (resource == CrmResourceType::CRM_DASH_IPV4_ACL_GROUP)
        {
            incCrmResUsedCounter(resource);
            auto &rule_cnt = m_resourcesMap.at(CrmResourceType::CRM_DASH_IPV4_ACL_RULE).countersMap[getCrmDashAclGroupKey(tableId)];
            rule_cnt.usedCounter = 0;
            rule_cnt.id = tableId;
        }
        else if (resource == CrmResourceType::CRM_DASH_IPV6_ACL_GROUP)
        {
            incCrmResUsedCounter(resource);
            auto &rule_cnt = m_resourcesMap.at(CrmResourceType::CRM_DASH_IPV6_ACL_RULE).countersMap[getCrmDashAclGroupKey(tableId)];
            rule_cnt.usedCounter = 0;
            rule_cnt.id = tableId;
        }
        else 
        {
            auto &rule_cnt = m_resourcesMap.at(resource).countersMap[getCrmDashAclGroupKey(tableId)];
            ++rule_cnt.usedCounter;
        }
    }
    catch (...)
    {
        SWSS_LOG_ERROR("Failed to increment \"used\" counter for the %s CRM resource (tableId:%" PRIx64 ").", crmResTypeNameMap.at(resource).c_str(), tableId);
        return;
    }
}

void CrmOrch::decCrmDashAclUsedCounter(CrmResourceType resource, sai_object_id_t tableId)
{
    SWSS_LOG_ENTER();

    try
    {
        if (resource == CrmResourceType::CRM_DASH_IPV4_ACL_GROUP)
        {
            decCrmResUsedCounter(resource);
            m_resourcesMap.at(CrmResourceType::CRM_DASH_IPV4_ACL_RULE).countersMap.erase(getCrmDashAclGroupKey(tableId));
            m_countersCrmTable->del(getCrmDashAclGroupKey(tableId));
        }
        else if (resource == CrmResourceType::CRM_DASH_IPV6_ACL_GROUP)
        {
            decCrmResUsedCounter(resource);
            m_resourcesMap.at(CrmResourceType::CRM_DASH_IPV6_ACL_RULE).countersMap.erase(getCrmDashAclGroupKey(tableId));
            m_countersCrmTable->del(getCrmDashAclGroupKey(tableId));
        }
        else 
        {
            auto &rule_cnt = m_resourcesMap.at(resource).countersMap[getCrmDashAclGroupKey(tableId)];
            --rule_cnt.usedCounter;
        }
    }
    catch (...)
    {
        SWSS_LOG_ERROR("Failed to decrement \"used\" counter for the %s CRM resource (tableId:%" PRIx64 ").", crmResTypeNameMap.at(resource).c_str(), tableId);
        return;
    }
}

void CrmOrch::doTask(SelectableTimer &timer)
{
    SWSS_LOG_ENTER();

    getResAvailableCounters();
    updateCrmCountersTable();
    checkCrmThresholds();
}

bool CrmOrch::getResAvailability(CrmResourceType type, CrmResourceEntry &res)
{
    sai_attribute_t attr;
    uint64_t availCount = 0;
    sai_status_t status = SAI_STATUS_SUCCESS;

    sai_object_type_t objType = crmResSaiObjAttrMap.at(type);

    if (objType != SAI_OBJECT_TYPE_NULL)
    {
        uint32_t attrCount = 0;

        switch (type)
        {
            case CrmResourceType::CRM_IPV4_ROUTE:
            case CrmResourceType::CRM_IPV6_ROUTE:
            case CrmResourceType::CRM_IPV4_NEIGHBOR:
            case CrmResourceType::CRM_IPV6_NEIGHBOR:
            case CrmResourceType::CRM_DASH_IPV4_ACL_GROUP:
            case CrmResourceType::CRM_DASH_IPV6_ACL_GROUP:
                attr.id = crmResAddrFamilyAttrMap.at(type);
                attr.value.s32 = crmResAddrFamilyValMap.at(type);
                attrCount = 1;
                break;
            
            case CrmResourceType::CRM_MPLS_NEXTHOP:
                attr.id = SAI_NEXT_HOP_ATTR_TYPE;
                attr.value.s32 = SAI_NEXT_HOP_TYPE_MPLS;
                attrCount = 1;
                break;
            
            case CrmResourceType::CRM_SRV6_NEXTHOP:
                attr.id = SAI_NEXT_HOP_ATTR_TYPE;
                attr.value.s32 = SAI_NEXT_HOP_TYPE_SRV6_SIDLIST;
                attrCount = 1;
                break;
            
            default:
                break;
        }

        status = sai_object_type_get_availability(gSwitchId, objType, attrCount, &attr, &availCount);
    }

    if ((status != SAI_STATUS_SUCCESS) || (objType == SAI_OBJECT_TYPE_NULL))
    {
        if (crmResSaiAvailAttrMap.find(type) != crmResSaiAvailAttrMap.end())
        {
            attr.id = crmResSaiAvailAttrMap.at(type);
            status = sai_switch_api->get_switch_attribute(gSwitchId, 1, &attr);
        }

        if ((status == SAI_STATUS_NOT_SUPPORTED) ||
            (status == SAI_STATUS_NOT_IMPLEMENTED) ||
            SAI_STATUS_IS_ATTR_NOT_SUPPORTED(status) ||
            SAI_STATUS_IS_ATTR_NOT_IMPLEMENTED(status))
        {
            // mark unsupported resources
            res.resStatus = CrmResourceStatus::CRM_RES_NOT_SUPPORTED;
            SWSS_LOG_NOTICE("CRM resource %s not supported", crmResTypeNameMap.at(type).c_str());
            return false;
        }

        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_ERROR("Failed to get availability counter for %s CRM resource", crmResTypeNameMap.at(type).c_str());
            return false;
        }

        availCount = attr.value.u32;
    }

    res.countersMap[CRM_COUNTERS_TABLE_KEY].availableCounter = static_cast<uint32_t>(availCount);

    return true;
}

bool CrmOrch::getDashAclGroupResAvailability(CrmResourceType type, CrmResourceEntry &res)
{
    sai_object_type_t objType = crmResSaiObjAttrMap.at(type);

    for (auto &cnt : res.countersMap)
    { 
        sai_attribute_t attr;
        attr.id = SAI_DASH_ACL_RULE_ATTR_DASH_ACL_GROUP_ID;
        attr.value.oid = cnt.second.id;

        uint64_t availCount = 0;
        sai_status_t status = sai_object_type_get_availability(gSwitchId, objType, 1, &attr, &availCount);
        if ((status == SAI_STATUS_NOT_SUPPORTED) ||
            (status == SAI_STATUS_NOT_IMPLEMENTED) ||
            SAI_STATUS_IS_ATTR_NOT_SUPPORTED(status) ||
            SAI_STATUS_IS_ATTR_NOT_IMPLEMENTED(status))
        {
            // mark unsupported resources
            res.resStatus = CrmResourceStatus::CRM_RES_NOT_SUPPORTED;
            SWSS_LOG_NOTICE("CRM resource %s not supported", crmResTypeNameMap.at(type).c_str());
            return false;
        }

        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_ERROR("Failed to get ACL table attribute %u , rv:%d", attr.id, status);
            break;
        }

        cnt.second.availableCounter = static_cast<uint32_t>(availCount);
    }

    return true;
}

void CrmOrch::getResAvailableCounters()
{
    SWSS_LOG_ENTER();

    for (auto &res : m_resourcesMap)
    {
        // ignore unsupported resources
        if (res.second.resStatus != CrmResourceStatus::CRM_RES_SUPPORTED)
        {
            continue;
        }

        switch (res.first)
        {
            case CrmResourceType::CRM_IPV4_ROUTE:
            case CrmResourceType::CRM_IPV6_ROUTE:
            case CrmResourceType::CRM_IPV4_NEXTHOP:
            case CrmResourceType::CRM_IPV6_NEXTHOP:
            case CrmResourceType::CRM_IPV4_NEIGHBOR:
            case CrmResourceType::CRM_IPV6_NEIGHBOR:
            case CrmResourceType::CRM_NEXTHOP_GROUP_MEMBER:
            case CrmResourceType::CRM_NEXTHOP_GROUP:
            case CrmResourceType::CRM_FDB_ENTRY:
            case CrmResourceType::CRM_IPMC_ENTRY:
            case CrmResourceType::CRM_SNAT_ENTRY:
            case CrmResourceType::CRM_DNAT_ENTRY:
            case CrmResourceType::CRM_MPLS_INSEG:
            case CrmResourceType::CRM_NEXTHOP_GROUP_MAP:
            case CrmResourceType::CRM_SRV6_MY_SID_ENTRY:
            case CrmResourceType::CRM_MPLS_NEXTHOP:
            case CrmResourceType::CRM_SRV6_NEXTHOP:
            case CrmResourceType::CRM_DASH_VNET:
            case CrmResourceType::CRM_DASH_ENI:
            case CrmResourceType::CRM_DASH_ENI_ETHER_ADDRESS_MAP:
            case CrmResourceType::CRM_DASH_IPV4_INBOUND_ROUTING:
            case CrmResourceType::CRM_DASH_IPV6_INBOUND_ROUTING:
            case CrmResourceType::CRM_DASH_IPV4_OUTBOUND_ROUTING:
            case CrmResourceType::CRM_DASH_IPV6_OUTBOUND_ROUTING:
            case CrmResourceType::CRM_DASH_IPV4_PA_VALIDATION:
            case CrmResourceType::CRM_DASH_IPV6_PA_VALIDATION:
            case CrmResourceType::CRM_DASH_IPV4_OUTBOUND_CA_TO_PA:
            case CrmResourceType::CRM_DASH_IPV6_OUTBOUND_CA_TO_PA:
            case CrmResourceType::CRM_DASH_IPV4_ACL_GROUP:
            case CrmResourceType::CRM_DASH_IPV6_ACL_GROUP:
            case CrmResourceType::CRM_TWAMP_ENTRY:
            {
                getResAvailability(res.first, res.second);
                break;
            }

            case CrmResourceType::CRM_ACL_TABLE:
            case CrmResourceType::CRM_ACL_GROUP:
            {
                sai_attribute_t attr;
                attr.id = crmResSaiAvailAttrMap.at(res.first);

                vector<sai_acl_resource_t> resources(CRM_ACL_RESOURCE_COUNT);

                attr.value.aclresource.count = CRM_ACL_RESOURCE_COUNT;
                attr.value.aclresource.list = resources.data();
                sai_status_t status = sai_switch_api->get_switch_attribute(gSwitchId, 1, &attr);
                if ((status == SAI_STATUS_NOT_SUPPORTED) ||
                    (status == SAI_STATUS_NOT_IMPLEMENTED) ||
                    SAI_STATUS_IS_ATTR_NOT_SUPPORTED(status) ||
                    SAI_STATUS_IS_ATTR_NOT_IMPLEMENTED(status))
                {
                    // mark unsupported resources
                    res.second.resStatus = CrmResourceStatus::CRM_RES_NOT_SUPPORTED;
                    SWSS_LOG_NOTICE("CRM resource %s not supported", crmResTypeNameMap.at(res.first).c_str());
                    break;
                }

                if (status == SAI_STATUS_BUFFER_OVERFLOW)
                {
                    resources.resize(attr.value.aclresource.count);
                    attr.value.aclresource.list = resources.data();
                    status = sai_switch_api->get_switch_attribute(gSwitchId, 1, &attr);
                }

                if (status != SAI_STATUS_SUCCESS)
                {
                    SWSS_LOG_ERROR("Failed to get switch attribute %u , rv:%d", attr.id, status);
                    task_process_status handle_status = handleSaiGetStatus(SAI_API_SWITCH, status);
                    if (handle_status != task_process_status::task_success)
                    {
                        break;
                    }
                }

                for (uint32_t i = 0; i < attr.value.aclresource.count; i++)
                {
                    string key = getCrmAclKey(attr.value.aclresource.list[i].stage, attr.value.aclresource.list[i].bind_point);
                    res.second.countersMap[key].availableCounter = attr.value.aclresource.list[i].avail_num;
                }

                break;
            }

            case CrmResourceType::CRM_ACL_ENTRY:
            case CrmResourceType::CRM_ACL_COUNTER:
            {
                sai_attribute_t attr;
                attr.id = crmResSaiAvailAttrMap.at(res.first);

                for (auto &cnt : res.second.countersMap)
                {
                    sai_status_t status = sai_acl_api->get_acl_table_attribute(cnt.second.id, 1, &attr);
                    if ((status == SAI_STATUS_NOT_SUPPORTED) ||
                        (status == SAI_STATUS_NOT_IMPLEMENTED) ||
                        SAI_STATUS_IS_ATTR_NOT_SUPPORTED(status) ||
                        SAI_STATUS_IS_ATTR_NOT_IMPLEMENTED(status))
                    {
                        // mark unsupported resources
                        res.second.resStatus = CrmResourceStatus::CRM_RES_NOT_SUPPORTED;
                        SWSS_LOG_NOTICE("CRM resource %s not supported", crmResTypeNameMap.at(res.first).c_str());
                        break;
                    }
                    if (status != SAI_STATUS_SUCCESS)
                    {
                        SWSS_LOG_ERROR("Failed to get ACL table attribute %u , rv:%d", attr.id, status);
                        break;
                    }

                    cnt.second.availableCounter = attr.value.u32;
                }

                break;
            }

            case CrmResourceType::CRM_EXT_TABLE:
            {
                for (auto &cnt : res.second.countersMap)
                {
                    std::string table_name = cnt.first;
                    sai_object_type_t objType = crmResSaiObjAttrMap.at(res.first);
                    sai_attribute_t attr;
                    uint64_t availCount = 0;

                    attr.id = SAI_GENERIC_PROGRAMMABLE_ATTR_OBJECT_NAME;
                    attr.value.s8list.count = (uint32_t)table_name.size();
                    attr.value.s8list.list = (int8_t *)const_cast<char *>(table_name.c_str());

                    sai_status_t status = sai_object_type_get_availability(
                                            gSwitchId, objType, 1, &attr, &availCount);
                    if (status != SAI_STATUS_SUCCESS)
                    {
                        SWSS_LOG_ERROR("Failed to get EXT table resource count %s , rv:%d",
       	                                table_name.c_str(), status);
                        break;
                    }

                    cnt.second.availableCounter = static_cast<uint32_t>(availCount);
                }
                break;
            }

            case CrmResourceType::CRM_DASH_IPV4_ACL_RULE:
            case CrmResourceType::CRM_DASH_IPV6_ACL_RULE:
            {
                getDashAclGroupResAvailability(res.first, res.second);
                break;
            }

            default:
                SWSS_LOG_ERROR("Failed to get CRM resource type %u. Unknown resource type.\n", static_cast<uint32_t>(res.first));
                return;
        }
    }
}

void CrmOrch::updateCrmCountersTable()
{
    SWSS_LOG_ENTER();

    // Update CRM used counters in COUNTERS_DB
    for (const auto &i : crmUsedCntsTableMap)
    {
        try
        {
            const auto &res = m_resourcesMap.at(i.second);
            if (res.resStatus == CrmResourceStatus::CRM_RES_NOT_SUPPORTED)
            {
                continue;
            }

            for (const auto &cnt : res.countersMap)
            {
                FieldValueTuple attr(i.first, to_string(cnt.second.usedCounter));
                vector<FieldValueTuple> attrs = { attr };
                m_countersCrmTable->set(cnt.first, attrs);
            }
        }
        catch(const out_of_range &e)
        {
            // expected when a resource is unavailable
        }
    }

    // Update CRM available counters in COUNTERS_DB
    for (const auto &i : crmAvailCntsTableMap)
    {
        try
        {
            const auto &res = m_resourcesMap.at(i.second);
            if (res.resStatus == CrmResourceStatus::CRM_RES_NOT_SUPPORTED)
            {
                continue;
            }

            for (const auto &cnt : res.countersMap)
            {
                FieldValueTuple attr(i.first, to_string(cnt.second.availableCounter));
                vector<FieldValueTuple> attrs = { attr };
                m_countersCrmTable->set(cnt.first, attrs);
            }
        }
        catch(const out_of_range &e)
        {
            // expected when a resource is unavailable
        }
    }
}

void CrmOrch::checkCrmThresholds()
{
    SWSS_LOG_ENTER();

    for (auto &i : m_resourcesMap)
    {
        auto &res = i.second;

        if (res.resStatus == CrmResourceStatus::CRM_RES_NOT_SUPPORTED)
        {
            continue;
        }

        for (auto &j : i.second.countersMap)
        {
            auto &cnt = j.second;
            uint64_t utilization = 0;
            uint32_t percentageUtil = 0;
            string threshType = "";

            if (cnt.usedCounter != 0)
            {
                uint32_t dvsr = cnt.usedCounter + cnt.availableCounter;
                if (dvsr != 0)
                {
                    percentageUtil = (cnt.usedCounter * 100) / dvsr;
                }
                else
                {
                    SWSS_LOG_WARN("%s Exception occurred (div by Zero): Used count %u free count %u",
                                  res.name.c_str(), cnt.usedCounter, cnt.availableCounter);
                }
            }

            switch (res.thresholdType)
            {
                case CrmThresholdType::CRM_PERCENTAGE:
                    utilization = percentageUtil;
                    threshType = "TH_PERCENTAGE";
                    break;
                case CrmThresholdType::CRM_USED:
                    utilization = cnt.usedCounter;
                    threshType = "TH_USED";
                    break;
                case CrmThresholdType::CRM_FREE:
                    utilization = cnt.availableCounter;
                    threshType = "TH_FREE";
                    break;
                default:
                    throw runtime_error("Unknown threshold type for CRM resource");
            }

            if ((utilization >= res.highThreshold) && (cnt.exceededLogCounter < CRM_EXCEEDED_MSG_MAX))
            {
                event_params_t params = {
                    { "percent", to_string(percentageUtil) },
                    { "used_cnt", to_string(cnt.usedCounter) },
                    { "free_cnt", to_string(cnt.availableCounter) }};

                SWSS_LOG_WARN("%s THRESHOLD_EXCEEDED for %s %u%% Used count %u free count %u",
                              res.name.c_str(), threshType.c_str(), percentageUtil, cnt.usedCounter, cnt.availableCounter);

                event_publish(g_events_handle, "chk_crm_threshold", &params);
                cnt.exceededLogCounter++;
            }
            else if ((utilization <= res.lowThreshold) && (cnt.exceededLogCounter > 0) && (res.highThreshold != res.lowThreshold))
            {
                SWSS_LOG_WARN("%s THRESHOLD_CLEAR for %s %u%% Used count %u free count %u",
                              res.name.c_str(), threshType.c_str(), percentageUtil, cnt.usedCounter, cnt.availableCounter);

                cnt.exceededLogCounter = 0;
            }
        } // end of counters loop
    } // end of resources loop
}


string CrmOrch::getCrmAclKey(sai_acl_stage_t stage, sai_acl_bind_point_type_t bindPoint)
{
    string key = "ACL_STATS";

    switch(stage)
    {
        case SAI_ACL_STAGE_INGRESS:
            key += ":INGRESS";
            break;
        case SAI_ACL_STAGE_EGRESS:
            key += ":EGRESS";
            break;
        default:
            return "";
    }

    switch(bindPoint)
    {
        case SAI_ACL_BIND_POINT_TYPE_PORT:
            key += ":PORT";
            break;
        case SAI_ACL_BIND_POINT_TYPE_LAG:
            key += ":LAG";
            break;
        case SAI_ACL_BIND_POINT_TYPE_VLAN:
            key += ":VLAN";
            break;
        case SAI_ACL_BIND_POINT_TYPE_ROUTER_INTERFACE:
            key += ":RIF";
            break;
        case SAI_ACL_BIND_POINT_TYPE_SWITCH:
            key += ":SWITCH";
            break;
        default:
            return "";
    }

    return key;
}

string CrmOrch::getCrmAclTableKey(sai_object_id_t id)
{
    std::stringstream ss;
    ss << "ACL_TABLE_STATS:" << "0x" << std::hex << id;
    return ss.str();
}

string CrmOrch::getCrmP4rtTableKey(std::string table_name)
{
    std::stringstream ss;
    ss << "EXT_TABLE_STATS:" << table_name;
    return ss.str();
}

string CrmOrch::getCrmDashAclGroupKey(sai_object_id_t id)
{
    std::stringstream ss;
    // Prepare the DASH_ACL_GROUP_STATS table key that will be used to store and access DASH ACL group counters
    // in the Counters DB.
    ss << "DASH_ACL_GROUP_STATS:" << "0x" << std::hex << id;
    return ss.str();
}
