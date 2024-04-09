import os
import re
import time
import json
import pytest

from swsscommon import swsscommon
from dvslib.dvs_common import wait_for_result

def get_exist_entries(db, table):
    tbl =  swsscommon.Table(db, table)
    return set(tbl.getKeys())

def get_created_entry(db, table, existed_entries):
    tbl =  swsscommon.Table(db, table)
    entries = set(tbl.getKeys())
    new_entries = list(entries - existed_entries)
    assert len(new_entries) == 1, "Wrong number of created entries."
    return new_entries[0]

class TestSrv6Mysid(object):
    def setup_db(self, dvs):
        self.pdb = dvs.get_app_db()
        self.adb = dvs.get_asic_db()
        self.cdb = dvs.get_config_db()

    def create_vrf(self, vrf_name):
        table = "ASIC_STATE:SAI_OBJECT_TYPE_VIRTUAL_ROUTER"
        existed_entries = get_exist_entries(self.adb.db_connection, table)

        self.cdb.create_entry("VRF", vrf_name, {"empty": "empty"})

        self.adb.wait_for_n_keys(table, len(existed_entries) + 1)
        return get_created_entry(self.adb.db_connection, table, existed_entries)

    def remove_vrf(self, vrf_name):
        self.cdb.delete_entry("VRF", vrf_name)

    def add_ip_address(self, interface, ip):
        self.cdb.create_entry("INTERFACE", interface + "|" + ip, {"NULL": "NULL"})

    def remove_ip_address(self, interface, ip):
        self.cdb.delete_entry("INTERFACE", interface + "|" + ip)

    def add_neighbor(self, interface, ip, mac, family):
        table = "ASIC_STATE:SAI_OBJECT_TYPE_NEIGHBOR_ENTRY"
        existed_entries = get_exist_entries(self.adb.db_connection, table)

        tbl = swsscommon.ProducerStateTable(self.pdb.db_connection, "NEIGH_TABLE")
        fvs = swsscommon.FieldValuePairs([("neigh", mac),
                                          ("family", family)])
        tbl.set(interface + ":" + ip, fvs)

        self.adb.wait_for_n_keys(table, len(existed_entries) + 1)
        return get_created_entry(self.adb.db_connection, table, existed_entries)

    def remove_neighbor(self, interface, ip):
        tbl = swsscommon.ProducerStateTable(self.pdb.db_connection, "NEIGH_TABLE")
        tbl._del(interface + ":" + ip)
        time.sleep(1)

    def create_mysid(self, mysid, fvs):
        table = "ASIC_STATE:SAI_OBJECT_TYPE_MY_SID_ENTRY"
        existed_entries = get_exist_entries(self.adb.db_connection, table)

        tbl = swsscommon.ProducerStateTable(self.pdb.db_connection, "SRV6_MY_SID_TABLE")
        tbl.set(mysid, fvs)

        self.adb.wait_for_n_keys(table, len(existed_entries) + 1)
        return get_created_entry(self.adb.db_connection, table, existed_entries)

    def remove_mysid(self, mysid):
        tbl = swsscommon.ProducerStateTable(self.pdb.db_connection, "SRV6_MY_SID_TABLE")
        tbl._del(mysid)

    def create_l3_intf(self, interface, vrf_name):
        table = "ASIC_STATE:SAI_OBJECT_TYPE_ROUTER_INTERFACE"
        existed_entries = get_exist_entries(self.adb.db_connection, table)

        if len(vrf_name) == 0:
            self.cdb.create_entry("INTERFACE", interface, {"NULL": "NULL"})
        else:
            self.cdb.create_entry("INTERFACE", interface, {"vrf_name": vrf_name})

        self.adb.wait_for_n_keys(table, len(existed_entries) + 1)
        return get_created_entry(self.adb.db_connection, table, existed_entries)

    def remove_l3_intf(self, interface):
        self.cdb.delete_entry("INTERFACE", interface)

    def get_nexthop_id(self, ip_address):
        next_hop_entries = get_exist_entries(self.adb.db_connection, "ASIC_STATE:SAI_OBJECT_TYPE_NEXT_HOP")
        tbl = swsscommon.Table(self.adb.db_connection, "ASIC_STATE:SAI_OBJECT_TYPE_NEXT_HOP")
        for next_hop_entry in next_hop_entries:
            (status, fvs) = tbl.get(next_hop_entry)

            assert status == True
            assert len(fvs) == 3

            for fv in fvs:
                if fv[0] == "SAI_NEXT_HOP_ATTR_IP" and fv[1] == ip_address:
                    return next_hop_entry

        return None

    def set_interface_status(self, dvs, interface, admin_status):
        tbl_name = "PORT"
        tbl = swsscommon.Table(self.cdb.db_connection, tbl_name)
        fvs = swsscommon.FieldValuePairs([("admin_status", "up")])
        tbl.set(interface, fvs)
        time.sleep(1)

    def test_mysid(self, dvs, testlog):
        self.setup_db(dvs)

        # create MySID entries
        mysid1='16:8:8:8:baba:2001:10::'
        mysid2='16:8:8:8:baba:2001:20::'
        mysid3='16:8:8:8:fcbb:bb01:800::'
        mysid4='16:8:8:8:baba:2001:40::'
        mysid5='32:16:16:0:fc00:0:1:e000::'
        mysid6='32:16:16:0:fc00:0:1:e001::'
        mysid7='32:16:16:0:fc00:0:1:e002::'
        mysid8='32:16:16:0:fc00:0:1:e003::'
        mysid9='32:16:16:0:fc00:0:1:e004::'
        mysid10='32:16:16:0:fc00:0:1:e005::'

        # create MySID END
        fvs = swsscommon.FieldValuePairs([('action', 'end')])
        key = self.create_mysid(mysid1, fvs)

        # check ASIC MySID database
        mysid = json.loads(key)
        assert mysid["sid"] == "baba:2001:10::"
        tbl = swsscommon.Table(self.adb.db_connection, "ASIC_STATE:SAI_OBJECT_TYPE_MY_SID_ENTRY")
        (status, fvs) = tbl.get(key)
        assert status == True
        for fv in fvs:
            if fv[0] == "SAI_MY_SID_ENTRY_ATTR_ENDPOINT_BEHAVIOR":
                assert fv[1] == "SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_E"

        # create vrf
        vrf_id = self.create_vrf("VrfDt46")

        # create MySID END.DT46
        fvs = swsscommon.FieldValuePairs([('action', 'end.dt46'), ('vrf', 'VrfDt46')])
        key = self.create_mysid(mysid2, fvs)

        # check ASIC MySID database
        mysid = json.loads(key)
        assert mysid["sid"] == "baba:2001:20::"
        tbl = swsscommon.Table(self.adb.db_connection, "ASIC_STATE:SAI_OBJECT_TYPE_MY_SID_ENTRY")
        (status, fvs) = tbl.get(key)
        assert status == True
        for fv in fvs:
            if fv[0] == "SAI_MY_SID_ENTRY_ATTR_VRF":
                assert fv[1] == vrf_id
            elif fv[0] == "SAI_MY_SID_ENTRY_ATTR_ENDPOINT_BEHAVIOR":
                assert fv[1] == "SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_DT46"

        # create MySID uN
        fvs = swsscommon.FieldValuePairs([('action', 'un')])
        key = self.create_mysid(mysid3, fvs)

        # check ASIC MySID database
        mysid = json.loads(key)
        assert mysid["sid"] == "fcbb:bb01:800::"
        tbl = swsscommon.Table(self.adb.db_connection, "ASIC_STATE:SAI_OBJECT_TYPE_MY_SID_ENTRY")
        (status, fvs) = tbl.get(key)
        assert status == True
        for fv in fvs:
            if fv[0] == "SAI_MY_SID_ENTRY_ATTR_ENDPOINT_BEHAVIOR":
                assert fv[1] == "SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_UN"
            elif fv[0] == "SAI_MY_SID_ENTRY_ATTR_ENDPOINT_BEHAVIOR_FLAVOR":
                assert fv[1] == "SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_FLAVOR_PSP_AND_USD"

        # create MySID END.DT4 with default vrf
        fvs = swsscommon.FieldValuePairs([('action', 'end.dt4'), ('vrf', 'default')])
        key = self.create_mysid(mysid4, fvs)

        # check ASIC MySID database
        mysid = json.loads(key)
        assert mysid["sid"] == "baba:2001:40::"
        tbl = swsscommon.Table(self.adb.db_connection, "ASIC_STATE:SAI_OBJECT_TYPE_MY_SID_ENTRY")
        (status, fvs) = tbl.get(key)
        assert status == True
        for fv in fvs:
            if fv[0] == "SAI_MY_SID_ENTRY_ATTR_VRF":
                assert True
            elif fv[0] == "SAI_MY_SID_ENTRY_ATTR_ENDPOINT_BEHAVIOR":
                assert fv[1] == "SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_DT4"

        # create interface
        self.create_l3_intf("Ethernet104", "")

        # Assign IP to interface
        self.add_ip_address("Ethernet104", "2001::2/126")
        self.add_ip_address("Ethernet104", "192.0.2.2/30")

        # create neighbor
        self.add_neighbor("Ethernet104", "2001::1", "00:00:00:01:02:04", "IPv6")
        self.add_neighbor("Ethernet104", "192.0.2.1", "00:00:00:01:02:05", "IPv4")

        # get nexthops
        next_hop_entries = get_exist_entries(self.adb.db_connection, "ASIC_STATE:SAI_OBJECT_TYPE_NEXT_HOP")
        assert len(next_hop_entries) == 2

        tbl = swsscommon.Table(self.adb.db_connection, "ASIC_STATE:SAI_OBJECT_TYPE_NEXT_HOP")
        for next_hop_entry in next_hop_entries:
            (status, fvs) = tbl.get(next_hop_entry)

            assert status == True
            assert len(fvs) == 3

            for fv in fvs:
                if fv[0] == "SAI_NEXT_HOP_ATTR_IP":
                    if fv[1] == "2001::1":
                        next_hop_ipv6_id = next_hop_entry
                    elif fv[1] == "192.0.2.1":
                        next_hop_ipv4_id = next_hop_entry
                    else:
                        assert False, "Nexthop IP %s not expected" % fv[1]

        assert next_hop_ipv6_id is not None
        assert next_hop_ipv4_id is not None

        # create MySID END.X
        fvs = swsscommon.FieldValuePairs([('action', 'end.x'), ('adj', '2001::1')])
        key = self.create_mysid(mysid5, fvs)

        # check ASIC MySID database
        mysid = json.loads(key)
        assert mysid["sid"] == "fc00:0:1:e000::"
        tbl = swsscommon.Table(self.adb.db_connection, "ASIC_STATE:SAI_OBJECT_TYPE_MY_SID_ENTRY")
        (status, fvs) = tbl.get(key)
        assert status == True
        for fv in fvs:
            if fv[0] == "SAI_MY_SID_ENTRY_ATTR_NEXT_HOP_ID":
                assert fv[1] == next_hop_ipv6_id
            if fv[0] == "SAI_MY_SID_ENTRY_ATTR_ENDPOINT_BEHAVIOR":
                assert fv[1] == "SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_X"
            elif fv[0] == "SAI_MY_SID_ENTRY_ATTR_ENDPOINT_BEHAVIOR_FLAVOR":
                assert fv[1] == "SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_FLAVOR_PSP_AND_USD"

        # create MySID END.DX4
        fvs = swsscommon.FieldValuePairs([('action', 'end.dx4'), ('adj', '192.0.2.1')])
        key = self.create_mysid(mysid6, fvs)

        # check ASIC MySID database
        mysid = json.loads(key)
        assert mysid["sid"] == "fc00:0:1:e001::"
        tbl = swsscommon.Table(self.adb.db_connection, "ASIC_STATE:SAI_OBJECT_TYPE_MY_SID_ENTRY")
        (status, fvs) = tbl.get(key)
        assert status == True
        for fv in fvs:
            if fv[0] == "SAI_MY_SID_ENTRY_ATTR_NEXT_HOP_ID":
                assert fv[1] == next_hop_ipv4_id
            if fv[0] == "SAI_MY_SID_ENTRY_ATTR_ENDPOINT_BEHAVIOR":
                assert fv[1] == "SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_DX4"
            elif fv[0] == "SAI_MY_SID_ENTRY_ATTR_ENDPOINT_BEHAVIOR_FLAVOR":
                assert fv[1] == "SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_FLAVOR_PSP_AND_USD"

        # create MySID END.DX6
        fvs = swsscommon.FieldValuePairs([('action', 'end.dx6'), ('adj', '2001::1')])
        key = self.create_mysid(mysid7, fvs)

        # check ASIC MySID database
        mysid = json.loads(key)
        assert mysid["sid"] == "fc00:0:1:e002::"
        tbl = swsscommon.Table(self.adb.db_connection, "ASIC_STATE:SAI_OBJECT_TYPE_MY_SID_ENTRY")
        (status, fvs) = tbl.get(key)
        assert status == True
        for fv in fvs:
            if fv[0] == "SAI_MY_SID_ENTRY_ATTR_NEXT_HOP_ID":
                assert fv[1] == next_hop_ipv6_id
            if fv[0] == "SAI_MY_SID_ENTRY_ATTR_ENDPOINT_BEHAVIOR":
                assert fv[1] == "SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_DX6"
            elif fv[0] == "SAI_MY_SID_ENTRY_ATTR_ENDPOINT_BEHAVIOR_FLAVOR":
                assert fv[1] == "SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_FLAVOR_PSP_AND_USD"

        # create MySID uA
        fvs = swsscommon.FieldValuePairs([('action', 'ua'), ('adj', '2001::1')])
        key = self.create_mysid(mysid8, fvs)

        # check ASIC MySID database
        mysid = json.loads(key)
        assert mysid["sid"] == "fc00:0:1:e003::"
        tbl = swsscommon.Table(self.adb.db_connection, "ASIC_STATE:SAI_OBJECT_TYPE_MY_SID_ENTRY")
        (status, fvs) = tbl.get(key)
        assert status == True
        for fv in fvs:
            if fv[0] == "SAI_MY_SID_ENTRY_ATTR_NEXT_HOP_ID":
                assert fv[1] == next_hop_ipv6_id
            if fv[0] == "SAI_MY_SID_ENTRY_ATTR_ENDPOINT_BEHAVIOR":
                assert fv[1] == "SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_UA"
            elif fv[0] == "SAI_MY_SID_ENTRY_ATTR_ENDPOINT_BEHAVIOR_FLAVOR":
                assert fv[1] == "SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_FLAVOR_PSP_AND_USD"

        # create MySID uDX4
        fvs = swsscommon.FieldValuePairs([('action', 'udx4'), ('adj', '192.0.2.1')])
        key = self.create_mysid(mysid9, fvs)

        # check ASIC MySID database
        mysid = json.loads(key)
        assert mysid["sid"] == "fc00:0:1:e004::"
        tbl = swsscommon.Table(self.adb.db_connection, "ASIC_STATE:SAI_OBJECT_TYPE_MY_SID_ENTRY")
        (status, fvs) = tbl.get(key)
        assert status == True
        for fv in fvs:
            if fv[0] == "SAI_MY_SID_ENTRY_ATTR_NEXT_HOP_ID":
                assert fv[1] == next_hop_ipv4_id
            if fv[0] == "SAI_MY_SID_ENTRY_ATTR_ENDPOINT_BEHAVIOR":
                assert fv[1] == "SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_DX4"
            elif fv[0] == "SAI_MY_SID_ENTRY_ATTR_ENDPOINT_BEHAVIOR_FLAVOR":
                assert fv[1] == "SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_FLAVOR_PSP_AND_USD"

        # create MySID END.DX6
        fvs = swsscommon.FieldValuePairs([('action', 'udx6'), ('adj', '2001::1')])
        key = self.create_mysid(mysid10, fvs)

        # check ASIC MySID database
        mysid = json.loads(key)
        assert mysid["sid"] == "fc00:0:1:e005::"
        tbl = swsscommon.Table(self.adb.db_connection, "ASIC_STATE:SAI_OBJECT_TYPE_MY_SID_ENTRY")
        (status, fvs) = tbl.get(key)
        assert status == True
        for fv in fvs:
            if fv[0] == "SAI_MY_SID_ENTRY_ATTR_NEXT_HOP_ID":
                assert fv[1] == next_hop_ipv6_id
            if fv[0] == "SAI_MY_SID_ENTRY_ATTR_ENDPOINT_BEHAVIOR":
                assert fv[1] == "SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_DX6"
            elif fv[0] == "SAI_MY_SID_ENTRY_ATTR_ENDPOINT_BEHAVIOR_FLAVOR":
                assert fv[1] == "SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_FLAVOR_PSP_AND_USD"

        # delete MySID
        self.remove_mysid(mysid1)
        self.remove_mysid(mysid2)
        self.remove_mysid(mysid3)
        self.remove_mysid(mysid4)
        self.remove_mysid(mysid5)
        self.remove_mysid(mysid6)
        self.remove_mysid(mysid7)
        self.remove_mysid(mysid8)
        self.remove_mysid(mysid9)
        self.remove_mysid(mysid10)

        # remove vrf
        self.remove_vrf("VrfDt46")

        # remove nexthop
        self.remove_neighbor("Ethernet104", "2001::1")
        self.remove_neighbor("Ethernet104", "192.0.2.1")

        # Reemove IP from interface
        self.remove_ip_address("Ethernet104", "2001::2/126")
        self.remove_ip_address("Ethernet104", "192.0.2.2/30")

        self.remove_l3_intf("Ethernet104")

    def test_mysid_l3adj(self, dvs, testlog):
        self.setup_db(dvs)

        # create MySID entries
        mysid1='32:16:16:0:fc00:0:1:e000::'

        # create interface
        self.create_l3_intf("Ethernet104", "")

        # assign IP to interface
        self.add_ip_address("Ethernet104", "2001::2/64")

        time.sleep(3)

        # bring up Ethernet104
        self.set_interface_status(dvs, "Ethernet104", "up")

        time.sleep(3)

        # save the initial number of entries in MySID table
        initial_my_sid_entries = get_exist_entries(self.adb.db_connection, "ASIC_STATE:SAI_OBJECT_TYPE_MY_SID_ENTRY")

        # save the initial number of entries in Nexthop table
        initial_next_hop_entries = get_exist_entries(self.adb.db_connection, "ASIC_STATE:SAI_OBJECT_TYPE_NEXT_HOP")

        # create MySID END.X, neighbor does not exist yet
        fvs = swsscommon.FieldValuePairs([('action', 'end.x'), ('adj', '2001::1')])
        tbl = swsscommon.ProducerStateTable(self.pdb.db_connection, "SRV6_MY_SID_TABLE")
        tbl.set(mysid1, fvs)

        time.sleep(2)

        # check the current number of entries in MySID table
        # since the neighbor does not exist yet, we expect the SID has not been installed (i.e., we
        # expect the same number of MySID entries as before)
        exist_my_sid_entries = get_exist_entries(self.adb.db_connection, "ASIC_STATE:SAI_OBJECT_TYPE_MY_SID_ENTRY")
        assert len(exist_my_sid_entries) == len(initial_my_sid_entries)

        # now, let's create the neighbor
        self.add_neighbor("Ethernet104", "2001::1", "00:00:00:01:02:04", "IPv6")

        # verify that the nexthop is created in the ASIC (i.e., we have the previous number of next hop entries + 1)
        self.adb.wait_for_n_keys("ASIC_STATE:SAI_OBJECT_TYPE_NEXT_HOP", len(initial_next_hop_entries) + 1)

        # get the new nexthop and nexthop ID, which will be used later to verify the MySID entry
        next_hop_entry = get_created_entry(self.adb.db_connection, "ASIC_STATE:SAI_OBJECT_TYPE_NEXT_HOP", initial_next_hop_entries)
        assert next_hop_entry is not None
        next_hop_id = self.get_nexthop_id("2001::1")
        assert next_hop_id is not None

        # now the neighbor has been created in the ASIC, we expect the MySID entry to be created in the ASIC as well
        self.adb.wait_for_n_keys("ASIC_STATE:SAI_OBJECT_TYPE_MY_SID_ENTRY", len(initial_my_sid_entries) + 1)
        my_sid_entry = get_created_entry(self.adb.db_connection, "ASIC_STATE:SAI_OBJECT_TYPE_MY_SID_ENTRY", initial_my_sid_entries)
        assert my_sid_entry is not None

        # check ASIC MySID database and verify the SID
        mysid = json.loads(my_sid_entry)
        assert mysid is not None
        assert mysid["sid"] == "fc00:0:1:e000::"
        tbl = swsscommon.Table(self.adb.db_connection, "ASIC_STATE:SAI_OBJECT_TYPE_MY_SID_ENTRY")
        (status, fvs) = tbl.get(my_sid_entry)
        assert status == True
        for fv in fvs:
            if fv[0] == "SAI_MY_SID_ENTRY_ATTR_NEXT_HOP_ID":
                assert fv[1] == next_hop_id
            if fv[0] == "SAI_MY_SID_ENTRY_ATTR_ENDPOINT_BEHAVIOR":
                assert fv[1] == "SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_X"
            elif fv[0] == "SAI_MY_SID_ENTRY_ATTR_ENDPOINT_BEHAVIOR_FLAVOR":
                assert fv[1] == "SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_FLAVOR_PSP_AND_USD"

        # remove neighbor
        self.remove_neighbor("Ethernet104", "2001::1")

        # delete MySID
        self.remove_mysid(mysid1)

        # # verify that the nexthop has been removed from the ASIC
        self.adb.wait_for_n_keys("ASIC_STATE:SAI_OBJECT_TYPE_NEXT_HOP", len(initial_next_hop_entries))

        # check the current number of entries in MySID table
        # since the MySID has been removed, we expect the SID has been removed from the ASIC as well
        exist_my_sid_entries = get_exist_entries(self.adb.db_connection, "ASIC_STATE:SAI_OBJECT_TYPE_MY_SID_ENTRY")
        assert len(exist_my_sid_entries) == len(initial_my_sid_entries)

        # remove IP from interface
        self.remove_ip_address("Ethernet104", "2001::2/64")

        # remove interface
        self.remove_l3_intf("Ethernet104")

class TestSrv6(object):
    def setup_db(self, dvs):
        self.pdb = dvs.get_app_db()
        self.adb = dvs.get_asic_db()
        self.cdb = dvs.get_config_db()

    def create_sidlist(self, segname, ips, type=None):
        table = "ASIC_STATE:SAI_OBJECT_TYPE_SRV6_SIDLIST"
        existed_entries = get_exist_entries(self.adb.db_connection, table)

        if type is None:
            fvs=swsscommon.FieldValuePairs([('path', ips)])
        else:
            fvs=swsscommon.FieldValuePairs([('path', ips), ('type', type)])
        segtbl = swsscommon.ProducerStateTable(self.pdb.db_connection, "SRV6_SID_LIST_TABLE")
        segtbl.set(segname, fvs)

        self.adb.wait_for_n_keys(table, len(existed_entries) + 1)
        return get_created_entry(self.adb.db_connection, table, existed_entries)

    def remove_sidlist(self, segname):
        segtbl = swsscommon.ProducerStateTable(self.pdb.db_connection, "SRV6_SID_LIST_TABLE")
        segtbl._del(segname)

    def create_srv6_route(self, routeip,segname,segsrc):
        table = "ASIC_STATE:SAI_OBJECT_TYPE_ROUTE_ENTRY"
        existed_entries = get_exist_entries(self.adb.db_connection, table)

        fvs=swsscommon.FieldValuePairs([('seg_src',segsrc),('segment',segname)])
        routetbl = swsscommon.ProducerStateTable(self.pdb.db_connection, "ROUTE_TABLE")
        routetbl.set(routeip,fvs)

        self.adb.wait_for_n_keys(table, len(existed_entries) + 1)
        return get_created_entry(self.adb.db_connection, table, existed_entries)

    def remove_srv6_route(self, routeip):
        routetbl = swsscommon.ProducerStateTable(self.pdb.db_connection, "ROUTE_TABLE")
        routetbl._del(routeip)

    def check_deleted_route_entries(self, destinations):
        def _access_function():
            route_entries = self.adb.get_keys("ASIC_STATE:SAI_OBJECT_TYPE_ROUTE_ENTRY")
            route_destinations = [json.loads(route_entry)["dest"] for route_entry in route_entries]
            return (all(destination not in route_destinations for destination in destinations), None)

        wait_for_result(_access_function)

    def add_neighbor(self, interface, ip, mac):
        fvs=swsscommon.FieldValuePairs([("neigh", mac)])
        neightbl = swsscommon.Table(self.cdb.db_connection, "NEIGH")
        neightbl.set(interface + "|" +ip, fvs)
        time.sleep(1)

    def remove_neighbor(self, interface,ip):
        neightbl = swsscommon.Table(self.cdb.db_connection, "NEIGH")
        neightbl._del(interface + "|" + ip)
        time.sleep(1)

    def test_srv6(self, dvs, testlog):
        self.setup_db(dvs)
        dvs.setup_db()

        # save exist asic db entries
        tunnel_entries = get_exist_entries(self.adb.db_connection, "ASIC_STATE:SAI_OBJECT_TYPE_TUNNEL")
        nexthop_entries = get_exist_entries(self.adb.db_connection, "ASIC_STATE:SAI_OBJECT_TYPE_NEXT_HOP")
        route_entries = get_exist_entries(self.adb.db_connection, "ASIC_STATE:SAI_OBJECT_TYPE_ROUTE_ENTRY")


        # bring up interfacee
        dvs.set_interface_status("Ethernet104", "up")
        dvs.set_interface_status("Ethernet112", "up")
        dvs.set_interface_status("Ethernet120", "up")

        # add neighbors
        self.add_neighbor("Ethernet104", "baba:2001:10::", "00:00:00:01:02:01")
        self.add_neighbor("Ethernet112", "baba:2002:10::", "00:00:00:01:02:02")
        self.add_neighbor("Ethernet120", "baba:2003:10::", "00:00:00:01:02:03")

        # create seg lists
        sidlist_id = self.create_sidlist('seg1', 'baba:2001:10::,baba:2001:20::')

        # check ASIC SAI_OBJECT_TYPE_SRV6_SIDLIST database
        tbl = swsscommon.Table(self.adb.db_connection, "ASIC_STATE:SAI_OBJECT_TYPE_SRV6_SIDLIST")
        (status, fvs) = tbl.get(sidlist_id)
        assert status == True
        for fv in fvs:
            if fv[0] == "SAI_SRV6_SIDLIST_ATTR_SEGMENT_LIST":
                assert fv[1] == "2:baba:2001:10::,baba:2001:20::"
            elif fv[0] == "SAI_SRV6_SIDLIST_ATTR_TYPE":
                assert fv[1] == "SAI_SRV6_SIDLIST_TYPE_ENCAPS_RED"


        # create v4 route with single sidlists
        route_key = self.create_srv6_route('20.20.20.20/32','seg1','1001:2000::1')
        nexthop_id = get_created_entry(self.adb.db_connection, "ASIC_STATE:SAI_OBJECT_TYPE_NEXT_HOP", nexthop_entries)
        tunnel_id = get_created_entry(self.adb.db_connection, "ASIC_STATE:SAI_OBJECT_TYPE_TUNNEL", tunnel_entries)

        # check ASIC SAI_OBJECT_TYPE_ROUTE_ENTRY database
        tbl = swsscommon.Table(self.adb.db_connection, "ASIC_STATE:SAI_OBJECT_TYPE_ROUTE_ENTRY")
        (status, fvs) = tbl.get(route_key)
        assert status == True
        for fv in fvs:
            if fv[0] == "SAI_ROUTE_ENTRY_ATTR_NEXT_HOP_ID":
                assert fv[1] == nexthop_id

        # check ASIC SAI_OBJECT_TYPE_NEXT_HOP database
        tbl = swsscommon.Table(self.adb.db_connection, "ASIC_STATE:SAI_OBJECT_TYPE_NEXT_HOP")
        (status, fvs) = tbl.get(nexthop_id)
        assert status == True
        for fv in fvs:
            if fv[0] == "SAI_NEXT_HOP_ATTR_SRV6_SIDLIST_ID":
                assert fv[1] == sidlist_id
            elif fv[0] == "SAI_NEXT_HOP_ATTR_TUNNEL_ID":
                assert fv[1] == tunnel_id

        # check ASIC SAI_OBJECT_TYPE_TUNNEL database
        tbl = swsscommon.Table(self.adb.db_connection, "ASIC_STATE:SAI_OBJECT_TYPE_TUNNEL")
        (status, fvs) = tbl.get(tunnel_id)
        assert status == True
        for fv in fvs:
            if fv[0] == "SAI_TUNNEL_ATTR_TYPE":
                assert fv[1] == "SAI_TUNNEL_TYPE_SRV6"
            elif fv[0] == "SAI_TUNNEL_ATTR_ENCAP_SRC_IP":
                assert fv[1] == "1001:2000::1"


        # create 2nd seg lists
        sidlist_id = self.create_sidlist('seg2', 'baba:2002:10::,baba:2002:20::', 'insert.red')

        # check ASIC SAI_OBJECT_TYPE_SRV6_SIDLIST database
        tbl = swsscommon.Table(self.adb.db_connection, "ASIC_STATE:SAI_OBJECT_TYPE_SRV6_SIDLIST")
        (status, fvs) = tbl.get(sidlist_id)
        assert status == True
        for fv in fvs:
            if fv[0] == "SAI_SRV6_SIDLIST_ATTR_SEGMENT_LIST":
                assert fv[1] == "2:baba:2002:10::,baba:2002:20::"
            elif fv[0] == "SAI_SRV6_SIDLIST_ATTR_TYPE":
                assert fv[1] == "SAI_SRV6_SIDLIST_TYPE_INSERT_RED"

        # create 3rd seg lists with unsupported or wrong naming of sid list type, for this case, it will use default type: ENCAPS_RED
        sidlist_id = self.create_sidlist('seg3', 'baba:2003:10::,baba:2003:20::', 'reduced')

        # check ASIC SAI_OBJECT_TYPE_SRV6_SIDLIST database
        tbl = swsscommon.Table(self.adb.db_connection, "ASIC_STATE:SAI_OBJECT_TYPE_SRV6_SIDLIST")
        (status, fvs) = tbl.get(sidlist_id)
        assert status == True
        for fv in fvs:
            if fv[0] == "SAI_SRV6_SIDLIST_ATTR_SEGMENT_LIST":
                assert fv[1] == "2:baba:2003:10::,baba:2003:20::"
            elif fv[0] == "SAI_SRV6_SIDLIST_ATTR_TYPE":
                assert fv[1] == "SAI_SRV6_SIDLIST_TYPE_ENCAPS_RED"

        # create 2nd v4 route with single sidlists
        self.create_srv6_route('20.20.20.21/32','seg2','1001:2000::1')
        # create 3rd v4 route with single sidlists
        self.create_srv6_route('20.20.20.22/32','seg3','1001:2000::1')

        # remove routes
        self.remove_srv6_route('20.20.20.20/32')
        self.check_deleted_route_entries('20.20.20.20/32')
        self.remove_srv6_route('20.20.20.21/32')
        self.check_deleted_route_entries('20.20.20.21/32')
        self.remove_srv6_route('20.20.20.22/32')
        self.check_deleted_route_entries('20.20.20.22/32')

        # remove sid lists
        self.remove_sidlist('seg1')
        self.remove_sidlist('seg2')
        self.remove_sidlist('seg3')

        # remove neighbors
        self.remove_neighbor("Ethernet104", "baba:2001:10::")
        self.remove_neighbor("Ethernet112", "baba:2002:10::")
        self.remove_neighbor("Ethernet120", "baba:2003:10::")

        # check if asic db entries are all restored
        assert tunnel_entries == get_exist_entries(self.adb.db_connection, "ASIC_STATE:SAI_OBJECT_TYPE_TUNNEL")
        assert nexthop_entries == get_exist_entries(self.adb.db_connection, "ASIC_STATE:SAI_OBJECT_TYPE_NEXT_HOP")
        assert route_entries == get_exist_entries(self.adb.db_connection, "ASIC_STATE:SAI_OBJECT_TYPE_ROUTE_ENTRY")

# Add Dummy always-pass test at end as workaroud
# for issue when Flaky fail on final test it invokes module tear-down before retrying
def test_nonflaky_dummy():
    pass
