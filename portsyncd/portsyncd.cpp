#include <getopt.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <set>
#include <map>
#include <list>
#include <sys/stat.h>
#include "dbconnector.h"
#include "select.h"
#include "netdispatcher.h"
#include "netlink.h"
#include "producerstatetable.h"
#include "portsyncd/linksync.h"
#include "subscriberstatetable.h"
#include "exec.h"
#include "warm_restart.h"

using namespace std;
using namespace swss;

#define DEFAULT_SELECT_TIMEOUT 1000 /* ms */

/*
 * This g_portSet contains all the front panel ports that the corresponding
 * host interfaces needed to be created. When this LinkSync class is
 * initialized, we check the database to see if some of the ports' host
 * interfaces are already created and remove them from this set. We will
 * remove the rest of the ports in the set when receiving the first netlink
 * message indicating that the host interfaces are created. After the set
 * is empty, we send out the signal PortInitDone. g_init is used to limit the
 * command to be run only once.
 */
set<string> g_portSet;
bool g_init = false;
string g_switchType;

void usage()
{
    cout << "Usage: portsyncd" << endl;
    cout << "       port lane mapping is from configDB" << endl;
    cout << "       this program will exit if configDB does not contain that info" << endl;
}

void handlePortConfigFromConfigDB(ProducerStateTable &p, DBConnector &cfgDb, bool warm);

int main(int argc, char **argv)
{
    try
    {
        Logger::linkToDbNative("portsyncd");
        int opt;

        while ((opt = getopt(argc, argv, "v:h")) != -1 )
        {
            switch (opt)
            {
            case 'h':
                usage();
                return 1;
            default: /* '?' */
                usage();
                return EXIT_FAILURE;
            }
        }

        DBConnector cfgDb("CONFIG_DB", 0);
        DBConnector appl_db("APPL_DB", 0);
        DBConnector state_db("STATE_DB", 0);
        ProducerStateTable p(&appl_db, APP_PORT_TABLE_NAME);

        Table cfgDeviceMetaDataTable(&cfgDb, CFG_DEVICE_METADATA_TABLE_NAME);
        if (!cfgDeviceMetaDataTable.hget("localhost", "switch_type", g_switchType))
        {
            //Switch type is not configured. Consider it default = "switch" (regular switch)
            g_switchType = "switch";
        }

        WarmStart::initialize("portsyncd", "swss");
        WarmStart::checkWarmStart("portsyncd", "swss");
        const bool warm = WarmStart::isWarmStart();

        NetLink netlink;
        Select s;

        netlink.registerGroup(RTNLGRP_LINK);
        netlink.dumpRequest(RTM_GETLINK);
        cout << "Listen to link messages..." << endl;

        handlePortConfigFromConfigDB(p, cfgDb, warm);

        LinkSync sync(&appl_db, &state_db);
        NetDispatcher::getInstance().registerMessageHandler(RTM_NEWLINK, &sync);
        NetDispatcher::getInstance().registerMessageHandler(RTM_DELLINK, &sync);

        s.addSelectable(&netlink);

        while (true)
        {
            Selectable *temps;
            int ret;
            ret = s.select(&temps, DEFAULT_SELECT_TIMEOUT);

            if (ret == Select::ERROR)
            {
                cerr << "Error had been returned in select" << endl;
                continue;
            }
            else if (ret == Select::TIMEOUT)
            {
                continue;
            }
            else if (ret != Select::OBJECT)
            {
                SWSS_LOG_ERROR("Unknown return value from Select %d", ret);
                continue;
            }

            if (temps == static_cast<Selectable*>(&netlink))
            {
                /* on netlink message, check if PortInitDone should be sent out */
                if (!g_init && g_portSet.empty())
                {
                    /*
                     * After finishing reading port configuration file and
                     * creating all host interfaces, this daemon shall send
                     * out a signal to orchagent indicating port initialization
                     * procedure is done and other application could start
                     * syncing.
                     */
                    FieldValueTuple finish_notice("lanes", "0");
                    vector<FieldValueTuple> attrs = { finish_notice };
                    p.set("PortInitDone", attrs);
                    SWSS_LOG_NOTICE("PortInitDone");

                    g_init = true;
                }
            }
            else
            {
                SWSS_LOG_ERROR("Unknown object returned by select");
                continue;
            }
        }
    }
    catch (const swss::RedisError& e)
    {
        cerr << "Exception \"" << e.what() << "\" was thrown in daemon" << endl;
        return EXIT_FAILURE;
    }
    catch (const std::out_of_range& e)
    {
        cerr << "Exception \"" << e.what() << "\" was thrown in daemon" << endl;
        return EXIT_FAILURE;
    }
    catch (const std::exception& e)
    {
        cerr << "Exception \"" << e.what() << "\" was thrown in daemon" << endl;
        return EXIT_FAILURE;
    }
    catch (...)
    {
        cerr << "Exception was thrown in daemon" << endl;
        return EXIT_FAILURE;
    }

    return 1;
}

static void notifyPortConfigDone(ProducerStateTable &p)
{
    /* Notify that all ports added */
    FieldValueTuple finish_notice("count", to_string(g_portSet.size()));
    vector<FieldValueTuple> attrs = { finish_notice };
    p.set("PortConfigDone", attrs);
}

void handlePortConfigFromConfigDB(ProducerStateTable &p, DBConnector &cfgDb, bool warm)
{
    SWSS_LOG_ENTER();

    SWSS_LOG_NOTICE("Getting port configuration from ConfigDB...");

    Table table(&cfgDb, CFG_PORT_TABLE_NAME);
    std::vector<FieldValueTuple> ovalues;
    std::vector<string> keys;
    table.getKeys(keys);

    if (keys.empty())
    {
        SWSS_LOG_NOTICE("ConfigDB does not have port information, "
                        "however ports can be added later on, continuing...");
    }

    for ( auto &k : keys )
    {
        table.get(k, ovalues);
        vector<FieldValueTuple> attrs;
        for ( auto &v : ovalues )
        {
            FieldValueTuple attr(v.first, v.second);
            attrs.push_back(attr);
        }
        if (!warm)
        {
            p.set(k, attrs);
        }
        g_portSet.insert(k);
    }
    if (!warm)
    {
        notifyPortConfigDone(p);
    }

}
