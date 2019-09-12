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
 * @brief : Implementation of Rotation PBFT Engine
 * @file: RotationPBFTEngine.cpp
 * @author: yujiechen
 * @date: 2019-09-11
 */
#include "RotationPBFTEngine.h"
using namespace dev::consensus;
using namespace dev::p2p;
using namespace dev::network;

// reset config
void RotationPBFTEngine::resetConfig()
{
    PBFTEngine::resetConfig();
    m_f = (m_groupSize - 1) / 3;
    updateConsensusList();
}

void RotationPBFTEngine::updateConsensusList()
{
    // reset consensusList
    if (m_highestBlock.number() == 0)
    {
        for (auto index = 0; index < m_groupSize; index++)
        {
            NodeID nodeId;
            if (getNodeIDByIndex(nodeId, index))
            {
                m_consensusList.insert(nodeId);
            }
        }
    }
    if (m_highestBlock.number() / m_rotationInterval > 0 &&
        0 == m_highestBlock.number() % m_rotationInterval)
    {
        // get the node should be removed
        NodeID removeNodeId;
        size_t removeIndex = (m_highestBlock.number() / m_rotationInterval - 1) % m_groupSize;
        if (getNodeIDByIndex(removeNodeId, removeIndex))
        {
            m_consensusList.erase(removeNodeId);
        }
        // insert a new node
        NodeID insertNodeId;
        size_t insertIndex =
            (m_highestBlock.number() / m_rotationInterval + m_groupSize - 1) % m_groupSize;
        if (getNodeIDByIndex(insertNodeId, insertIndex))
        {
            m_consensusList.insert(insertNodeId);
        }
        RPBFTENGINE_LOG(DEBUG) << LOG_DESC("updateConsensusList")
                               << LOG_KV("curNumber", m_highestBlock.number())
                               << LOG_KV("removeIndex", removeIndex)
                               << LOG_KV("removeNode", removeNodeId.abridged())
                               << LOG_KV("insertIndex", insertIndex)
                               << LOG_KV("insertNode", insertNodeId.abridged())
                               << LOG_KV("nodeIdx", m_idx)
                               << LOG_KV("nodeId", keyPair.pub().abridged());
    }
}

// get the currentLeader
std::pair<bool, IDXTYPE> RotationPBFTEngine::getLeader()
{
    // this node not loacted in consensus list
    if (!locatedInConsensusList)
    {
        return std::make_pair(false, MAXIDX);
    }
    return PBFTEngine::getLeader();
}

// determine the node should run consensus or not
bool RotationPBFTEngine::locatedInConsensusList()
{
    if (m_consensusList.count(m_keyPair.pub()))
    {
        return true;
    }
    return false;
}


// handler used to filter the broadcasted nodes
ssize_t ScalablePBFTEngine::filterSealerList(dev::network::NodeID const& nodeId)
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