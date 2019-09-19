/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2019 fisco-dev contributors.
 */
/**
 * @brief : SyncTreeTopology implementation
 * @author: yujiechen
 * @date: 2019-09-19
 */
#pragma once
#include "Common.h"
#include <libdevcore/Common.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/Guards.h>

#define SYNCTREE_LOG(_OBV)                                                 \
    LOG(_OBV) << LOG_BADGE("SYNCTREE") << LOG_KV("nodeIndex", m_nodeIndex) \
              << LOG_KV("consIndex", m_consIndex) << LOG_KV("nodeId", m_nodeId.abridged())

namespace dev
{
namespace sync
{
class SyncTreeTopology
{
public:
    using Ptr = std::shared_ptr<SyncTreeTopology>;
    SyncTreeTopology(dev::h512 const& nodeId) : m_nodeId(nodeId) {}

    virtual ~SyncTreeTopology() {}

    virtual void updateNodeListInfo(dev::h512s const& _nodeList);
    virtual void updateConsensusNodeInfo(dev::h512s const& _consensusNodes);
    virtual void updateStartAndEndIndex();

    dev::h512s selectNodes(std::set<dev::h512s> const& peers);

private:
    bool getNodeIDByIndex(dev::h512& nodeID, ssize_t const& nodeIndex) const;

    ssize_t getNodeIndexByNodeId(
        std::set<dev::h512s> const& findSet, dev::h512& nodeId, SharedMutex& mutex);

    void recursiveSelectChildNodes(dev::h512s& selectedNodeList, ssize_t const& parentIndex,
        std::set<dev::h512s> const& peers);

    void selectParentNodes(dev::h512s& selectedNodeList, std::set<dev::h512s> const& peers);

private:
    mutable SharedMutex x_nodeList;
    // the nodeList include both the consensus nodes and the observer nodes
    dev::h512s m_nodeList;
    std::atomic<int64_t> m_nodeNum = {0};

    mutable SharedMutex x_currentConsensusNodes;
    // the current consensusList
    dev::h512s m_currentConsensusNodes;
    unsigned m_treeWidth = 3;

    dev::h512 m_nodeId;
    std::atomic<int64_t> m_nodeIndex = {0};
    std::atomic<int64_t> m_consIndex = {0};
    std::atomic<int64_t> m_endIndex = {0};
    std::atomic<int64_t> m_startIndex = {0};
    std::atomic_bool m_needUpdateIndex = {false};
};
}  // namespace sync
}  // namespace dev
