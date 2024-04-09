#include "nhgorch.h"
#include "neighorch.h"
#include "crmorch.h"
#include "routeorch.h"
#include "bulker.h"
#include "logger.h"
#include "swssnet.h"

extern sai_object_id_t gSwitchId;

extern NeighOrch *gNeighOrch;
extern RouteOrch *gRouteOrch;
extern NhgOrch *gNhgOrch;

extern size_t gMaxBulkSize;

extern sai_next_hop_group_api_t* sai_next_hop_group_api;
extern sai_next_hop_api_t*         sai_next_hop_api;

NhgOrch::NhgOrch(DBConnector *db, string tableName) : NhgOrchCommon(db, tableName)
{
    SWSS_LOG_ENTER();
}

/*
 * Purpose:     Perform the operations requested by APPL_DB users.
 * Description: Iterate over the untreated operations list and resolve them.
 *              The operations supported are SET and DEL.  If an operation
 *              could not be resolved, it will either remain in the list, or be
 *              removed, depending on the case.
 * Params:      IN  consumer - The cosumer object.
 * Returns:     Nothing.
 */
void NhgOrch::doTask(Consumer& consumer)
{
    SWSS_LOG_ENTER();

    if (!gPortsOrch->allPortsReady())
    {
        return;
    }

    auto it = consumer.m_toSync.begin();

    while (it != consumer.m_toSync.end())
    {
        KeyOpFieldsValuesTuple t = it->second;

        string index = kfvKey(t);
        string op = kfvOp(t);

        bool success = false;
        const auto& nhg_it = m_syncdNextHopGroups.find(index);

        if (op == SET_COMMAND)
        {
            string ips;
            string aliases;
            string weights;
            string mpls_nhs;

            /* Get group's next hop IPs and aliases */
            for (auto i : kfvFieldsValues(t))
            {
                if (fvField(i) == "nexthop")
                    ips = fvValue(i);

                if (fvField(i) == "ifname")
                    aliases = fvValue(i);

                if (fvField(i) == "weight")
                    weights = fvValue(i);

                if (fvField(i) == "mpls_nh")
                    mpls_nhs = fvValue(i);
            }

            /* Split ips and alaises strings into vectors of tokens. */
            vector<string> ipv = tokenize(ips, ',');
            vector<string> alsv = tokenize(aliases, ',');
            vector<string> mpls_nhv = tokenize(mpls_nhs, ',');

            /* Create the next hop group key. */
            string nhg_str;

            for (uint32_t i = 0; i < ipv.size(); i++)
            {
                if (i) nhg_str += NHG_DELIMITER;
                if (!mpls_nhv.empty() && mpls_nhv[i] != "na")
                {
                    nhg_str += mpls_nhv[i] + LABELSTACK_DELIMITER;
                }
                nhg_str += ipv[i] + NH_DELIMITER + alsv[i];
            }

            NextHopGroupKey nhg_key = NextHopGroupKey(nhg_str, weights);

            /* If the group does not exist, create one. */
            if (nhg_it == m_syncdNextHopGroups.end())
            {
                /*
                * If we've reached the NHG limit, we're going to create a temporary
                * group, represented by one of it's NH only until we have
                * enough resources to sync the whole group.  The item is going
                * to be kept in the sync list so we keep trying to create the
                * actual group when there are enough resources.
                */
                if (gRouteOrch->getNhgCount() + NextHopGroup::getSyncedCount() >= gRouteOrch->getMaxNhgCount())
                {
                    SWSS_LOG_DEBUG("Next hop group count reached its limit.");

                    try
                    {
                        auto nhg = std::make_unique<NextHopGroup>(createTempNhg(nhg_key));
                        if (nhg->sync())
                        {
                            m_syncdNextHopGroups.emplace(index, NhgEntry<NextHopGroup>(std::move(nhg)));
                        }
                        else
                        {
                            SWSS_LOG_INFO("Failed to sync temporary NHG %s with %s",
                                index.c_str(),
                                nhg_key.to_string().c_str());
                        }
                    }
                    catch (const std::exception& e)
                    {
                        SWSS_LOG_INFO("Got exception: %s while adding temp group %s",
                            e.what(),
                            nhg_key.to_string().c_str());
                    }
                }
                else
                {
                    auto nhg = std::make_unique<NextHopGroup>(nhg_key, false);
                    success = nhg->sync();

                    if (success)
                    {
                        m_syncdNextHopGroups.emplace(index, NhgEntry<NextHopGroup>(std::move(nhg)));
                    }
                }
            }
            /* If the group exists, update it. */
            else
            {
                const auto& nhg_ptr = nhg_it->second.nhg;

                /*
                 * If the update would mandate promoting a temporary next hop
                 * group to a multiple next hops group and we do not have the
                 * resources yet, we have to skip it until we have enough
                 * resources.
                 */
                if (nhg_ptr->isTemp() &&
                    (gRouteOrch->getNhgCount() + NextHopGroup::getSyncedCount() >= gRouteOrch->getMaxNhgCount()))
                {
                    /*
                     * If the group was updated in such way that the previously
                     * chosen next hop does not represent the new group key,
                     * update the temporary group to choose a new next hop from
                     * the new key.  Otherwise, this will be a no-op as we have
                     * to wait for resources in order to promote the group.
                     */
                    if (!nhg_key.contains(nhg_ptr->getKey()))
                    {
                        try
                        {
                            /* Create the new temporary next hop group. */
                            auto new_nhg = std::make_unique<NextHopGroup>(createTempNhg(nhg_key));

                            /*
                            * If we successfully sync the new group, update
                            * only the next hop group entry's pointer so we
                            * don't mess up the reference counter, as other
                            * objects may already reference it.
                            */
                            if (new_nhg->sync())
                            {
                                nhg_it->second.nhg = std::move(new_nhg);
                            }
                            else
                            {
                                SWSS_LOG_INFO("Failed to sync updated temp NHG %s with %s",
                                  index.c_str(),
                                  nhg_key.to_string().c_str());
                            }
                        }
                        catch (const std::exception& e)
                        {
                            SWSS_LOG_INFO("Got exception: %s while adding temp group %s",
                                e.what(),
                                nhg_key.to_string().c_str());
                        }
                    }
                }
                /*
                 * If the group is temporary but can now be promoted, create and sync a new group for
                 * the desired next hops.
                 */
                else if (nhg_ptr->isTemp())
                {
                    auto nhg = std::make_unique<NextHopGroup>(nhg_key, false);
                    success = nhg->sync();

                    if (success)
                    {
                        /*
                         * Placing the new group in the map will replace the temporary group, causing
                         * it to be removed and freed.
                         */
                        nhg_it->second.nhg = std::move(nhg);
                    }
                }
                /* Common update, when all the requirements are met. */
                else
                {
                    success = nhg_ptr->update(nhg_key);
                }
            }
        }
        else if (op == DEL_COMMAND)
        {
            /*
             * If there is a pending SET after this DEL operation, skip the
             * DEL operation to perform the update instead.  Otherwise, in the
             * scenario where the DEL operation may be blocked by the ref
             * counter, we'd end up deleting the object after the SET operation
             * is performed, which would not reflect the desired state of the
             * object.
             */
            if (consumer.m_toSync.count(it->first) > 1)
            {
                success = true;
            }
            /* If the group does not exist, do nothing. */
            else if (nhg_it == m_syncdNextHopGroups.end())
            {
                SWSS_LOG_INFO("Unable to find group with key %s to remove", index.c_str());
                /* Mark the operation as successful to consume it. */
                success = true;
            }
            /* If the group does exist, but it's still referenced, skip. */
            else if (nhg_it->second.ref_count > 0)
            {
                SWSS_LOG_INFO("Unable to remove group %s which is referenced", index.c_str());
            }
            /* Else, if the group is no more referenced, remove it. */
            else
            {
                const auto& nhg = nhg_it->second.nhg;

                success = nhg->remove();

                if (success)
                {
                    m_syncdNextHopGroups.erase(nhg_it);
                }
            }
        }
        else
        {
            SWSS_LOG_ERROR("Unknown operation type %s\n", op.c_str());
            /* Mark the operation as successful to consume it. */
            success = true;
        }

        /* Depending on the operation success, consume it or skip it. */
        if (success)
        {
            it = consumer.m_toSync.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

/*
 * Purpose:     Validate a next hop for any groups that contains it.
 * Description: Iterate over all next hop groups and validate the next hop in
 *              those who contain it.
 * Params:      IN  nh_key - The next hop to validate.
 * Returns:     true, if the next hop was successfully validated in all
 *              containing groups;
 *              false, otherwise.
 */
bool NhgOrch::validateNextHop(const NextHopKey& nh_key)
{
    SWSS_LOG_ENTER();

    /*
     * Iterate through all groups and validate the next hop in those who
     * contain it.
     */
    for (auto& it : m_syncdNextHopGroups)
    {
        auto& nhg = it.second.nhg;

        if (nhg->hasMember(nh_key))
        {
            /*
             * If sync fails, exit right away, as we expect it to be due to a
             * raeson for which any other future validations will fail too.
             */
            if (!nhg->validateNextHop(nh_key))
            {
                SWSS_LOG_ERROR("Failed to validate next hop %s in group %s",
                                nh_key.to_string().c_str(),
                                it.first.c_str());
                return false;
            }
        }
    }

    return true;
}

/*
 * Purpose:     Invalidate a next hop for any groups containing it.
 * Description: Iterate through the next hop groups and remove the next hop
 *              from those that contain it.
 * Params:      IN  nh_key - The next hop to invalidate.
 * Returns:     true, if the next hop was successfully invalidatedd from all
 *              containing groups;
 *              false, otherwise.
 */
bool NhgOrch::invalidateNextHop(const NextHopKey& nh_key)
{
    SWSS_LOG_ENTER();

    /*
     * Iterate through all groups and invalidate the next hop from those who
     * contain it.
     */
    for (auto& it : m_syncdNextHopGroups)
    {
        auto& nhg = it.second.nhg;

        if (nhg->hasMember(nh_key))
        {
            /* If the remove fails, exit right away. */
            if (!nhg->invalidateNextHop(nh_key))
            {
                SWSS_LOG_WARN("Failed to invalidate next hop %s from group %s",
                                nh_key.to_string().c_str(),
                                it.first.c_str());
                return false;
            }
        }
    }

    return true;
}

/*
 * Purpose:     Get the next hop ID of the member.
 * Description: Get the SAI ID of the next hop from NeighOrch.
 * Params:      None.
 * Returns:     The SAI ID of the next hop, or SAI_NULL_OBJECT_ID if the next
 *              hop is not valid.
 */
sai_object_id_t NextHopGroupMember::getNhId() const
{
    SWSS_LOG_ENTER();

    sai_object_id_t nh_id = SAI_NULL_OBJECT_ID;

    if (gNeighOrch->hasNextHop(m_key))
    {
        nh_id = gNeighOrch->getNextHopId(m_key);
    }
    /*
     * If the next hop is labeled and the IP next hop exists, create the
     * labeled one over NeighOrch as it doesn't know about these next hops.
     * We don't do this in the constructor as the IP next hop may be added
     * after the object is created and would never create the labeled next hop
     * afterwards.
     */
    else if (isLabeled() && gNeighOrch->isNeighborResolved(m_key))
    {
        if (gNeighOrch->addNextHop(m_key))
        {
            nh_id = gNeighOrch->getNextHopId(m_key);
        }
    }
    else
    {
        gNeighOrch->resolveNeighbor(m_key);
    }

    return nh_id;
}

/*
 * Purpose:     Update the weight of a member.
 * Description: Set the new member's weight and if the member is synced, update
 *              the SAI attribute as well.
 * Params:      IN  weight - The weight of the next hop group member.
 * Returns:     true, if the operation was successful;
 *              false, otherwise.
 */
bool NextHopGroupMember::updateWeight(uint32_t weight)
{
    SWSS_LOG_ENTER();

    bool success = true;

    m_key.weight = weight;

    if (isSynced())
    {
        sai_attribute_t nhgm_attr;
        nhgm_attr.id = SAI_NEXT_HOP_GROUP_MEMBER_ATTR_WEIGHT;
        nhgm_attr.value.s32 = m_key.weight;

        sai_status_t status = sai_next_hop_group_api->set_next_hop_group_member_attribute(m_gm_id, &nhgm_attr);
        success = status == SAI_STATUS_SUCCESS;
    }

    return success;
}

/*
 * Purpose:     Sync the group member with the given group member ID.
 * Description: Set the group member's SAI ID to the the one given and
 *              increment the appropriate ref counters.
 * Params:      IN  gm_id - The group member SAI ID to set.
 * Returns:     Nothing.
 */
void NextHopGroupMember::sync(sai_object_id_t gm_id)
{
    SWSS_LOG_ENTER();

    NhgMember::sync(gm_id);
    gNeighOrch->increaseNextHopRefCount(m_key);
}

/*
 * Purpose:     Remove the group member, resetting it's SAI ID.
 * Description: Reset the group member's SAI ID and decrement the appropriate
 *              ref counters.
 * Params:      None.
 * Returns:     Nothing.
 */
void NextHopGroupMember::remove()
{
    SWSS_LOG_ENTER();

    NhgMember::remove();
    gNeighOrch->decreaseNextHopRefCount(m_key);
}

/*
 * Purpose:     Destructor.
 * Description: Assert the group member is removed and remove the labeled
 *              next hop from NeighOrch if it is unreferenced.
 * Params:      None.
 * Returns:     Nothing.
 */
NextHopGroupMember::~NextHopGroupMember()
{
    SWSS_LOG_ENTER();

    /*
     * If the labeled next hop is unreferenced, remove it from NeighOrch as
     * NhgOrch and RouteOrch are the ones controlling it's lifetime.  They both
     * watch over these labeled next hops, so it doesn't matter who created
     * them as they're both doing the same checks before removing a labeled
     * next hop.
     */
    if (isLabeled() &&
        gNeighOrch->hasNextHop(m_key) &&
        (gNeighOrch->getNextHopRefCount(m_key) == 0))
    {
        gNeighOrch->removeMplsNextHop(m_key);
    }
}

/*
 * Purpose:     Constructor.
 * Description: Initialize the group's members based on the next hop group key.
 * Params:      IN  key - The next hop group's key.
 * Returns:     Nothing.
 */
NextHopGroup::NextHopGroup(const NextHopGroupKey& key, bool is_temp) : NhgCommon(key), m_is_temp(is_temp)
{
    SWSS_LOG_ENTER();

    /* Parse the key and create the members. */
    for (const auto& it : m_key.getNextHops())
    {
        m_members.emplace(it, NextHopGroupMember(it));
    }
}

/*
 * Purpose:     Move assignment operator.
 * Description: Perform member-wise swap.
 * Params:      IN  nhg - The rvalue object to swap with.
 * Returns:     Referene to this object.
 */
NextHopGroup& NextHopGroup::operator=(NextHopGroup&& nhg)
{
    SWSS_LOG_ENTER();

    m_is_temp = nhg.m_is_temp;

    NhgCommon::operator=(std::move(nhg));

    return *this;
}

/*
 * Purpose:     Sync a next hop group.
 * Description: Fill in the NHG ID.  If the group contains only one NH, this ID
 *              will be the SAI ID of the next hop that NeighOrch owns.  If it
 *              has more than one NH, create a group over the SAI API and then
 *              add it's members.
 * Params:      None.
 * Returns:     true, if the operation was successful;
 *              false, otherwise.
 */
bool NextHopGroup::sync()
{
    SWSS_LOG_ENTER();

    /* If the group is already synced, exit. */
    if (isSynced())
    {
        return true;
    }

    /*
     * If the group is temporary, the group ID will be the only member's NH
     * ID.
     */
    if (m_is_temp)
    {
        const NextHopGroupMember& nhgm = m_members.begin()->second;
        sai_object_id_t nhid = nhgm.getNhId();

        if (nhid == SAI_NULL_OBJECT_ID)
        {
            SWSS_LOG_WARN("Next hop %s is not synced", nhgm.getKey().to_string().c_str());
            return false;
        }
        else
        {
            m_id = nhid;
        }
    }
    else
    {
        /* Assert the group is not empty. */
        assert(!m_members.empty());

        /* Create the group over SAI. */
        sai_attribute_t nhg_attr;
        vector<sai_attribute_t> nhg_attrs;

        nhg_attr.id = SAI_NEXT_HOP_GROUP_ATTR_TYPE;
        nhg_attr.value.s32 = SAI_NEXT_HOP_GROUP_TYPE_ECMP;
        nhg_attrs.push_back(nhg_attr);

        sai_status_t status = sai_next_hop_group_api->create_next_hop_group(
                                                    &m_id,
                                                    gSwitchId,
                                                    (uint32_t)nhg_attrs.size(),
                                                    nhg_attrs.data());

        /* If the operation fails, exit. */
        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_ERROR("Failed to create next hop group %s, rv:%d",
                            m_key.to_string().c_str(), status);

            task_process_status handle_status = handleSaiCreateStatus(SAI_API_NEXT_HOP_GROUP, status);
            if (handle_status != task_success)
            {
                return parseHandleSaiStatusFailure(handle_status);
            }
        }

        /* Increment the amount of programmed next hop groups. */
        gCrmOrch->incCrmResUsedCounter(CrmResourceType::CRM_NEXTHOP_GROUP);

        /* Increment the number of synced NHGs. */
        ++m_syncdCount;

        /*
        * Try creating the next hop group's members over SAI.
        */
        if (!syncMembers(m_key.getNextHops()))
        {
            SWSS_LOG_WARN("Failed to create next hop members of group %s",
                            to_string().c_str());
            return false;
        }
    }

    return true;
}

/*
 * Purpose:     Create a temporary next hop group when resources are exhausted.
 * Description: Choose one member to represent the group and create a group
 *              with only that next hop as a member.  Any object referencing
 *              the SAI ID of a temporary group should keep querying NhgOrch in
 *              case the group is updated, as it's SAI ID will change at that
 *              point.
 * Params:      IN  index   - The CP index of the next hop group.
 * Returns:     The created temporary next hop group.
 */
NextHopGroup NhgOrch::createTempNhg(const NextHopGroupKey& nhg_key)
{
    SWSS_LOG_ENTER();

    /* Get a list of all valid next hops in the group. */
    std::list<NextHopKey> valid_nhs;

    for (const auto& nh_key : nhg_key.getNextHops())
    {
        /*
         * Check if the IP next hop exists.  We check for the IP next hop as
         * the group might contain labeled NHs which we should create if their
         * IP next hop does exist.
         */
        if (gNeighOrch->isNeighborResolved(nh_key))
        {
            valid_nhs.push_back(nh_key);
        }
    }

    /* If there is no valid member, exit. */
    if (valid_nhs.empty())
    {
        SWSS_LOG_INFO("There is no valid NH to sync temporary group %s",
                        nhg_key.to_string().c_str());
        throw std::logic_error("No valid NH in the key");
    }

    /* Randomly select the valid NH to represent the group. */
    auto it = valid_nhs.begin();
    advance(it, rand() % valid_nhs.size());

    /* Create the temporary group. */
    NextHopGroup nhg(NextHopGroupKey(it->to_string()), true);

    return nhg;
}

/*
 * Purpose:     Remove the next hop group.
 * Description: Reset the group's SAI ID.  If the group has more than one
 *              members, remove the members and the group.
 * Params:      None.
 * Returns:     true, if the operation was successful;
 *              false, otherwise
 */
bool NextHopGroup::remove()
{
    SWSS_LOG_ENTER();

    //  If the group is temporary, there is nothing to be done - just reset the ID.
    if (m_is_temp)
    {
        m_id = SAI_NULL_OBJECT_ID;
        return true;
    }

    return NhgCommon::remove();
}

/*
 * Purpose:     Sync the given next hop group's members over the SAI API.
 * Description: Iterate over the given members and sync them.  If the member
 *              is already synced, we skip it.  If any of the next hops isn't
 *              already synced by the neighOrch, this will fail.  Any next hop
 *              which has the neighbor interface down will be skipped.
 * Params:      IN  nh_keys - The next hop keys of the members to sync.
 * Returns:     true, if the members were added succesfully;
 *              false, otherwise.
 */
bool NextHopGroup::syncMembers(const std::set<NextHopKey>& nh_keys)
{
    SWSS_LOG_ENTER();

    ObjectBulker<sai_next_hop_group_api_t> nextHopGroupMemberBulker(sai_next_hop_group_api, gSwitchId, gMaxBulkSize);

    /*
     * Iterate over the given next hops.
     * If the group member is already synced, skip it.
     * If any next hop is not synced, thus neighOrch doesn't have it, stop
     * immediately.
     * If a next hop's interface is down, skip it from being synced.
     */
    std::map<NextHopKey, sai_object_id_t> syncingMembers;

    for (const auto& nh_key : nh_keys)
    {
        NextHopGroupMember& nhgm = m_members.at(nh_key);

        /* If the member is already synced, continue. */
        if (nhgm.isSynced())
        {
            continue;
        }

        /*
         * If the next hop doesn't exist, stop from syncing the members.
         */
        if (nhgm.getNhId() == SAI_NULL_OBJECT_ID)
        {
            SWSS_LOG_WARN("Failed to get next hop %s in group %s",
                        nhgm.to_string().c_str(), to_string().c_str());
            return false;
        }

        /* If the neighbor's interface is down, skip from being syncd. */
        if (gNeighOrch->isNextHopFlagSet(nh_key, NHFLAGS_IFDOWN))
        {
            SWSS_LOG_WARN("Skip next hop %s in group %s, interface is down",
                        nh_key.to_string().c_str(), to_string().c_str());
            continue;
        }

        /* Create the next hop group member's attributes and fill them. */
        vector<sai_attribute_t> nhgm_attrs = createNhgmAttrs(nhgm);

        /* Add a bulker entry for this member. */
        nextHopGroupMemberBulker.create_entry(&syncingMembers[nh_key],
                                               (uint32_t)nhgm_attrs.size(),
                                               nhgm_attrs.data());
    }

    /* Flush the bulker to perform the sync. */
    nextHopGroupMemberBulker.flush();

    /*
     * Go through the synced members and increment the Crm ref count for the
     * successful ones.
     */
    bool success = true;
    for (const auto& mbr : syncingMembers)
    {
        /* Check that the returned member ID is valid. */
        if (mbr.second == SAI_NULL_OBJECT_ID)
        {
            SWSS_LOG_ERROR("Failed to create next hop group %s's member %s",
                            m_key.to_string().c_str(), mbr.first.to_string().c_str());
            success = false;
        }
        else
        {
            m_members.at(mbr.first).sync(mbr.second);
        }
    }

    return success;
}

/*
 * Purpose:     Update the next hop group based on a new next hop group key.
 * Description: Update the group's members by removing the members that aren't
 *              in the new next hop group and adding the new members.  We first
 *              remove the missing members to avoid cases where we reached the
 *              ASIC group members limit.  This will not update the group's SAI
 *              ID in any way, unless we are promoting a temporary group.
 * Params:      IN  nhg_key - The new next hop group key to update to.
 * Returns:     true, if the operation was successful;
 *              false, otherwise.
 */
bool NextHopGroup::update(const NextHopGroupKey& nhg_key)
{
    SWSS_LOG_ENTER();

    /* Update the key. */
    m_key = nhg_key;

    std::set<NextHopKey> new_nh_keys = nhg_key.getNextHops();
    std::set<NextHopKey> removed_nh_keys;

    /* Mark the members that need to be removed. */
    for (auto& mbr_it : m_members)
    {
        const NextHopKey& nh_key = mbr_it.first;

        /* Look for the existing member inside the new ones. */
        const auto& new_nh_key_it = new_nh_keys.find(nh_key);

        /* If the member is not found, then it needs to be removed. */
        if (new_nh_key_it == new_nh_keys.end())
        {
            removed_nh_keys.insert(nh_key);
        }
        /* If the member is updated, update it's weight. */
        else
        {
            if (!mbr_it.second.updateWeight(new_nh_key_it->weight))
            {
                SWSS_LOG_WARN("Failed to update member %s weight", nh_key.to_string().c_str());
                return false;
            }

            /*
             * Erase the member from the new members list as it already
             * exists.
             */
            new_nh_keys.erase(new_nh_key_it);
        }
    }

    /* Remove the removed members. */
    if (!removeMembers(removed_nh_keys))
    {
        SWSS_LOG_WARN("Failed to remove members from group %s", to_string().c_str());
        return false;
    }

    /* Remove the removed members. */
    for (const auto& nh_key : removed_nh_keys)
    {
        m_members.erase(nh_key);
    }

    /* Add any new members to the group. */
    for (const auto& it : new_nh_keys)
    {
        m_members.emplace(it, NextHopGroupMember(it));
    }

    /*
     * Sync all the members of the group.  We sync all of them because
     * there may be previous members that were not successfully synced
     * before the update, so we must make sure we sync those as well.
     */
    if (!syncMembers(m_key.getNextHops()))
    {
        SWSS_LOG_WARN("Failed to sync new members for group %s", to_string().c_str());
        return false;
    }

    return true;
}

/*
 * Purpose:     Create the attributes vector for a next hop group member.
 * Description: Create the group ID and next hop ID attributes.
 * Params:      IN  nhgm - The next hop group member.
 * Returns:     The attributes vector for the given next hop.
 */
vector<sai_attribute_t> NextHopGroup::createNhgmAttrs(const NextHopGroupMember& nhgm) const
{
    SWSS_LOG_ENTER();

    vector<sai_attribute_t> nhgm_attrs;
    sai_attribute_t nhgm_attr;

    /* Fill in the group ID. */
    nhgm_attr.id = SAI_NEXT_HOP_GROUP_MEMBER_ATTR_NEXT_HOP_GROUP_ID;
    nhgm_attr.value.oid = m_id;
    nhgm_attrs.push_back(nhgm_attr);

    /* Fill in the next hop ID. */
    nhgm_attr.id = SAI_NEXT_HOP_GROUP_MEMBER_ATTR_NEXT_HOP_ID;
    nhgm_attr.value.oid = nhgm.getNhId();
    nhgm_attrs.push_back(nhgm_attr);

    /* Fill in the weight if set. */
    auto weight  = nhgm.getWeight();
    if (weight != 0)
    {
        nhgm_attr.id = SAI_NEXT_HOP_GROUP_MEMBER_ATTR_WEIGHT;
        nhgm_attr.value.s32 = weight;
        nhgm_attrs.push_back(nhgm_attr);
    }

    return nhgm_attrs;
}

/*
 * Purpose:     Validate a next hop in the group.
 * Description: Sync the validated next hop group member.
 * Params:      IN  nh_key - The next hop to validate.
 * Returns:     true, if the operation was successful;
 *              false, otherwise.
 */
bool NextHopGroup::validateNextHop(const NextHopKey& nh_key)
{
    SWSS_LOG_ENTER();

    return syncMembers({nh_key});
}

/*
 * Purpose:     Invalidate a next hop in the group.
 * Description: Sync the invalidated next hop group member.
 * Params:      IN  nh_key - The next hop to invalidate.
 * Returns:     true, if the operation was successful;
 *              false, otherwise.
 */
bool NextHopGroup::invalidateNextHop(const NextHopKey& nh_key)
{
    SWSS_LOG_ENTER();

    return removeMembers({nh_key});
}
