#ifndef SWSS_NEIGHORCH_H
#define SWSS_NEIGHORCH_H

#include "orch.h"
#include "observer.h"
#include "portsorch.h"
#include "intfsorch.h"
#include "fdborch.h"

#include "ipaddress.h"
#include "nexthopkey.h"
#include "producerstatetable.h"
#include "schema.h"
#include "bfdorch.h"

#define NHFLAGS_IFDOWN                  0x1 // nexthop's outbound i/f is down

typedef NextHopKey NeighborEntry;

struct NextHopEntry
{
    sai_object_id_t     next_hop_id;    // next hop id
    int                 ref_count;      // reference count
    uint32_t            nh_flags;       // flags
};

struct NeighborData
{
    MacAddress    mac;
    bool          hw_configured = false; // False means, entry is not written to HW
    uint32_t      voq_encap_index = 0;
};

/* NeighborTable: NeighborEntry, neighbor MAC address */
typedef map<NeighborEntry, NeighborData> NeighborTable;
/* NextHopTable: NextHopKey, NextHopEntry */
typedef map<NextHopKey, NextHopEntry> NextHopTable;

struct NeighborUpdate
{
    NeighborEntry entry;
    MacAddress mac;
    bool add;
};

class NeighOrch : public Orch, public Subject, public Observer
{
public:
    NeighOrch(DBConnector *db, string tableName, IntfsOrch *intfsOrch, FdbOrch *fdbOrch, PortsOrch *portsOrch, DBConnector *chassisAppDb);
    ~NeighOrch();

    bool hasNextHop(const NextHopKey&);
    bool isNeighborResolved(const NextHopKey&);
    bool addNextHop(const NextHopKey&);
    bool removeMplsNextHop(const NextHopKey&);

    sai_object_id_t getNextHopId(const NextHopKey&);
    sai_object_id_t getLocalNextHopId(const NextHopKey&);
    int getNextHopRefCount(const NextHopKey&);

    void increaseNextHopRefCount(const NextHopKey&, uint32_t count = 1);
    void decreaseNextHopRefCount(const NextHopKey&, uint32_t count = 1);

    bool getNeighborEntry(const NextHopKey&, NeighborEntry&, MacAddress&);
    bool getNeighborEntry(const IpAddress&, NeighborEntry&, MacAddress&);

    bool enableNeighbor(const NeighborEntry&);
    bool disableNeighbor(const NeighborEntry&);
    bool isHwConfigured(const NeighborEntry&);

    sai_object_id_t addTunnelNextHop(const NextHopKey&);
    bool removeTunnelNextHop(const NextHopKey&);

    bool ifChangeInformNextHop(const string &, bool);
    bool isNextHopFlagSet(const NextHopKey &, const uint32_t);
    bool removeOverlayNextHop(const NextHopKey &);
    void update(SubjectType, void *);

    bool addInbandNeighbor(string alias, IpAddress ip_address);
    bool delInbandNeighbor(string alias, IpAddress ip_address);

    void resolveNeighbor(const NeighborEntry &);
    void updateSrv6Nexthop(const NextHopKey &, const sai_object_id_t &);

private:
    PortsOrch *m_portsOrch;
    IntfsOrch *m_intfsOrch;
    FdbOrch *m_fdbOrch;
    ProducerStateTable m_appNeighResolveProducer;

    NeighborTable m_syncdNeighbors;
    NextHopTable m_syncdNextHops;

    std::set<NextHopKey> m_neighborToResolve;

    bool removeNextHop(const IpAddress&, const string&);

    bool addNeighbor(const NeighborEntry&, const MacAddress&);
    bool removeNeighbor(const NeighborEntry&, bool disable = false);

    bool setNextHopFlag(const NextHopKey &, const uint32_t);
    bool clearNextHopFlag(const NextHopKey &, const uint32_t);

    void processFDBFlushUpdate(const FdbFlushUpdate &);

    void doTask(Consumer &consumer);
    void doVoqSystemNeighTask(Consumer &consumer);

    unique_ptr<Table> m_tableVoqSystemNeighTable;
    unique_ptr<Table> m_stateSystemNeighTable;
    bool getSystemPortNeighEncapIndex(string &alias, IpAddress &ip, uint32_t &encap_index);
    bool addVoqEncapIndex(string &alias, IpAddress &ip, vector<sai_attribute_t> &neighbor_attrs);
    void voqSyncAddNeigh(string &alias, IpAddress &ip_address, const MacAddress &mac, sai_neighbor_entry_t &neighbor_entry);
    void voqSyncDelNeigh(string &alias, IpAddress &ip_address);
    bool updateVoqNeighborEncapIndex(const NeighborEntry &neighborEntry, uint32_t encap_index);
    void updateNextHop(const BfdUpdate&);

    bool resolveNeighborEntry(const NeighborEntry &, const MacAddress &);
    void clearResolvedNeighborEntry(const NeighborEntry &);

    bool addZeroMacTunnelRoute(const NeighborEntry &, const MacAddress &);
};

#endif /* SWSS_NEIGHORCH_H */
