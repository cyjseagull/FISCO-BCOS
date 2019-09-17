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
 * @brief : Implementation of Rotating PBFT Engine
 * @file: RotatingPBFTEngine.cpp
 * @author: yujiechen
 * @date: 2019-09-11
 */
#include "RotatingPBFTEngine.h"
using namespace dev::consensus;
using namespace dev::p2p;
using namespace dev::network;

// reset config
void RotatingPBFTEngine::resetConfig()
{
    PBFTEngine::resetConfig();
    m_f = (m_groupSize - 1) / 3;
    updateConsensusList();
}

void RotatingPBFTEngine::updateConsensusList()
{
    int64_t blockNumber = m_blockChain->number();
    if (m_lastGroup == -1)
    {
        m_lastGroup = blockNumber / m_rotatingInterval;
        if (m_lastGroup > 1)
        {
            m_lastGroup -= 1;
        }
    }

    // reset consensusList
    if (blockNumber == 0 && m_consensusList.size() == 0 && m_consensusIdList.size() == 0)
    {
        for (auto index = 0; index < m_groupSize; index++)
        {
            NodeID nodeId;
            if (getNodeIDByIndex(nodeId, index))
            {
                m_consensusList.insert(nodeId);
                m_consensusIdList.push_back(index);
            }
        }
    }

    int64_t currentGroup = blockNumber / m_rotatingInterval;
    if (currentGroup == m_lastGroup || blockNumber % m_rotatingInterval != 0)
    {
        return;
    }
    WriteGuard l(x_consensusListMutex);
    // get the node should be removed
    NodeID removeNodeId;
    size_t removeIndex = (currentGroup - 1) % m_nodeNum;
    if (getNodeIDByIndex(removeNodeId, removeIndex))
    {
        m_consensusList.erase(removeNodeId);
        IDXTYPE index = m_consensusIdList.front();
        assert(index == removeIndex);
        m_consensusIdList.pop_front();
    }
    // insert a new node
    NodeID insertNodeId;
    size_t insertIndex = (currentGroup + m_groupSize - 1) % m_nodeNum;
    if (getNodeIDByIndex(insertNodeId, insertIndex))
    {
        m_consensusList.insert(insertNodeId);
        m_consensusIdList.push_back((IDXTYPE)(insertIndex));
    }
    m_lastGroup = currentGroup;
    m_leaderFailed = false;
    RPBFTENGINE_LOG(DEBUG) << LOG_DESC("updateConsensusList") << LOG_KV("curNumber", blockNumber)
                           << LOG_KV("removeIndex", removeIndex)
                           << LOG_KV("removeNode", removeNodeId.abridged())
                           << LOG_KV("insertIndex", insertIndex)
                           << LOG_KV("insertNode", insertNodeId.abridged())
                           << LOG_KV("nodeIdx", m_idx)
                           << LOG_KV("nodeId", m_keyPair.pub().abridged());
}

// get the currentLeader
std::pair<bool, IDXTYPE> RotatingPBFTEngine::getLeader() const
{
    // this node not loacted in consensus list
    if (!locatedInConsensusList())
    {
        return std::make_pair(false, MAXIDX);
    }
    if (m_cfgErr || m_leaderFailed || m_highestBlock.sealer() == Invalid256 || m_nodeNum == 0)
    {
        return std::make_pair(false, MAXIDX);
    }
    ReadGuard l(x_consensusListMutex);
    size_t index = (m_view + m_highestBlock.number()) % m_groupSize;
    auto iter = m_consensusIdList.begin();
    advance(iter, index);
    return std::make_pair(true, *iter);
}

// determine the node should run consensus or not
bool RotatingPBFTEngine::locatedInConsensusList() const
{
    ReadGuard l(x_consensusListMutex);
    if (m_consensusList.count(m_keyPair.pub()))
    {
        return true;
    }
    return false;
}


// handler used to filter the broadcasted nodes
ssize_t RotatingPBFTEngine::filterSealerList(dev::network::NodeID const& nodeId)
{
    // the node should be sealer
    if (-1 == getIndexBySealer(nodeId))
    {
        return -1;
    }
    // the node should be located in the consensusList
    if (!m_consensusList.count(nodeId))
    {
        return -1;
    }
    return 0;
}