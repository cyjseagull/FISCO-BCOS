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

#include "SyncTreeTopology.h"
using namespace dev;
using namespace dev::sync;

void SyncTreeTopology::updateNodeListInfo(dev::h512s const& _nodeList)
{
    {
        ReadGuard l(x_nodeList);
        if (_nodeList == m_nodeList)
        {
            return;
        }
        // update the nodeNum
        int64_t nodeNum = _nodeList.size();
        if (m_nodeNum != nodeNum)
        {
            m_nodeNum = nodeNum;
        }
    }
    {
        WriteGuard l(x_nodeList);
        m_nodeList = _nodeList;
    }
    // update the nodeIndex
    m_nodeIndex = getNodeIndexByNodeId(m_nodeList, m_nodeId, x_nodeList);
    updateStartAndEndIndex();
}

void SyncTreeTopology::updateConsensusNodeInfo(dev::h512s const& _consensusNodes)
{
    {
        ReadGuard l(x_currentConsensusNodes);
        if (m_currentConsensusNodes == _consensusNodes)
        {
            return;
        }
    }
    {
        WriteGuard l(x_currentConsensusNodes);
        m_currentConsensusNodes = _consensusNodes;
    }
    m_consIndex = getNodeIndexByNodeId(m_currentConsensusNodes, m_nodeId, x_currentConsensusNodes);
    updateStartAndEndIndex();
}

void SyncTreeTopology::updateStartAndEndIndex()
{
    {
        ReadGuard l(x_currentConsensusNodes);
        if (m_currentConsensusNodes.size() == 0)
        {
            return;
        }
    }
    {
        ReadGuard l(x_nodeList);
        if (m_nodeList.size() == 0)
        {
            return;
        }
    }

    int64_t consensusNodeSize = 0;
    {
        ReadGuard l(x_currentConsensusNodes);
        consensusNodeSize = m_currentConsensusNodes.size();
    }
    int64_t slotSize = m_nodeNum / consensusNodeSize;
    // the consensus node
    if (m_consIndex > 0)
    {
        m_startIndex = slotSize * m_consIndex.load();
    }
    else
    {
        m_startIndex = (m_nodeIndex / slotSize) * slotSize;
    }
    int64_t endIndex = m_startIndex + slotSize - 1;
    if (endIndex > (m_nodeNum - 1))
    {
        endIndex = m_nodeNum - 1;
    }
    m_endIndex = (m_nodeNum - endIndex <= slotSize) ? (m_nodeNum - 1) : endIndex;

    SYNCTREE_LOG(DEBUG) << LOG_DESC("updateStartAndEndIndex") << LOG_KV("startIndex", m_startIndex)
                        << LOG_KV("endIndex", m_endIndex);
}

ssize_t SyncTreeTopology::getNodeIndexByNodeId(
    dev::h512s const& findSet, dev::h512& nodeId, SharedMutex& mutex)
{
    ssize_t nodeIndex = -1;
    ReadGuard l(mutex);
    for (ssize_t i = 0; i < m_nodeNum; i++)
    {
        if (nodeId == findSet[i])
        {
            nodeIndex = i;
            break;
        }
    }
    return nodeIndex;
}

bool SyncTreeTopology::getNodeIDByIndex(h512& nodeID, ssize_t const& nodeIndex) const
{
    if (nodeIndex >= m_nodeNum)
    {
        SYNCTREE_LOG(DEBUG) << LOG_DESC("getNodeIDByIndex: invalidNode")
                            << LOG_KV("nodeIndex", nodeIndex) << LOG_KV("nodeListSize", m_nodeNum);
        return false;
    }
    ReadGuard l(x_nodeList);
    nodeID = m_nodeList[nodeIndex];
    return true;
}

void SyncTreeTopology::recursiveSelectChildNodes(
    h512s& selectedNodeList, ssize_t const& parentIndex, std::set<dev::h512> const& peers)
{
    dev::h512 selectedNode;
    for (ssize_t i = 1; i < m_treeWidth; i++)
    {
        ssize_t expectedIndex = parentIndex * m_treeWidth + i;
        if (expectedIndex > m_endIndex)
        {
            break;
        }
        // the child node exists in the peers
        if (getNodeIDByIndex(selectedNode, expectedIndex) && peers.count(selectedNode))
        {
            SYNCTREE_LOG(DEBUG) << LOG_DESC("recursiveSelectChildNodes")
                                << LOG_KV("selectedNode", selectedNode.abridged())
                                << LOG_KV("selectedIndex", expectedIndex);
            selectedNodeList.push_back(selectedNode);
        }
        // the child node doesn't exit in the peers, select the grand child recursively
        else
        {
            recursiveSelectChildNodes(selectedNodeList, expectedIndex, peers);
        }
    }
}

void SyncTreeTopology::selectParentNodes(
    dev::h512s& selectedNodeList, std::set<dev::h512> const& peers)
{
    // push all other consensus node to the selectedNodeList if this node is the consensus node
    if (m_consIndex > 0)
    {
        ReadGuard l(x_currentConsensusNodes);
        for (auto const& consNode : m_currentConsensusNodes)
        {
            if (peers.count(consNode))
            {
                selectedNodeList.push_back(consNode);
            }
        }
        return;
    }
    // find the parentNode if this node is not the consensus node
    ssize_t parentIndex = (m_nodeIndex - 1) / m_treeWidth;
    // the parentNode is the node-slef
    if (parentIndex == m_nodeIndex)
    {
        return;
    }
    dev::h512 selectedNode;
    while (parentIndex > m_startIndex)
    {
        // find the parentNode from the peers
        if (getNodeIDByIndex(selectedNode, parentIndex) && peers.count(selectedNode))
        {
            selectedNodeList.push_back(selectedNode);
            break;
        }
        parentIndex = (parentIndex - 1) / m_treeWidth;
    }
}


dev::h512s SyncTreeTopology::selectNodes(std::set<dev::h512> const& peers)
{
    dev::h512s selectedNodeList;

    // the node is the consensusNode, chose the childNode
    if (m_consIndex > 0)
    {
        recursiveSelectChildNodes(selectedNodeList, m_consIndex, peers);
    }
    // the node is not the consensusNode
    else
    {
        recursiveSelectChildNodes(selectedNodeList, m_nodeIndex, peers);
    }
    // find the parent nodes
    selectParentNodes(selectedNodeList, peers);
    return selectedNodeList;
}