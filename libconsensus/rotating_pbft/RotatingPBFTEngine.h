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
 * @file: RotationPBFTEngine.h
 * @author: yujiechen
 * @date: 2019-09-11
 */
#pragma once
#include "Common.h"
#include <libconsensus/pbft/PBFTEngine.h>

namespace dev
{
namespace consensus
{
class RotatingPBFTEngine : public PBFTEngine
{
public:
    RotatingPBFTEngine(std::shared_ptr<dev::p2p::P2PInterface> _service,
        std::shared_ptr<dev::txpool::TxPoolInterface> _txPool,
        std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain,
        std::shared_ptr<dev::sync::SyncInterface> _blockSync,
        std::shared_ptr<dev::blockverifier::BlockVerifierInterface> _blockVerifier,
        dev::PROTOCOL_ID const& _protocolId, KeyPair const& _keyPair,
        h512s const& _sealerList = h512s())
      : PBFTEngine(_service, _txPool, _blockChain, _blockSync, _blockVerifier, _protocolId,
            _keyPair, _sealerList)
    {
        m_broadcastFilter = boost::bind(&RotatingPBFTEngine::filterSealerList, this, _1);
        m_blockSync->registerNodeIdFilterHandler(boost::bind(
            &RotatingPBFTEngine::NodeIdFilterHandler<std::set<dev::p2p::NodeID> const&>, this, _1));
        // only broadcast to the consensus list
        m_broadcastPrepareByTree = false;
    }

    void setGroupSize(int64_t const& groupSize)
    {
        groupSize > m_nodeNum ? m_groupSize = m_nodeNum : m_groupSize = groupSize;
        m_groupSize = groupSize;
        RPBFTENGINE_LOG(INFO) << LOG_KV("configured groupSize", m_groupSize);
    }

    void setRotatingInterval(int64_t const& rotatingInterval)
    {
        m_rotatingInterval = rotatingInterval;
    }

    bool locatedInConsensusList() const override;

    template <typename T>
    dev::p2p::NodeIDs NodeIdFilterHandler(T const& peers)
    {
        dev::p2p::NodeIDs selectedNode;
        for (auto const& peer : peers)
        {
            if (m_consensusList.count(peer))
                selectedNode.push_back(peer);
        }
        return selectedNode;
    }

protected:
    void resetConfig() override;
    virtual void updateConsensusList();
    IDXTYPE minValidNodes() const override { return m_groupSize - m_f; }
    // get the currentLeader
    std::pair<bool, IDXTYPE> getLeader() const override;

    virtual ssize_t filterSealerList(dev::network::NodeID const& nodeId);

protected:
    // configured group size
    std::atomic<int64_t> m_groupSize = {0};
    // the interval(measured by block number) to adjust the sealers
    std::atomic<int64_t> m_rotatingInterval = {10};
    std::atomic<int64_t> m_lastGroup = {-1};

    std::set<dev::h512> m_consensusList;

    mutable SharedMutex x_consensusListMutex;
    std::list<IDXTYPE> m_consensusIdList;
};
}  // namespace consensus
}  // namespace dev
