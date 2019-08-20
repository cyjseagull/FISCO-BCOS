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
 * @brief : define messages releated to HotStuff
 * @file: HotStuffMsg.h
 * @author: yujiechen
 * @date: 2019-8-25
 */

#pragma once
#include "HotStuffEngine.h"
#include "HotStuffEngineFactory.h"
#include <libconsensus/Sealer.h>

namespace dev
{
namespace consensus
{
class HotStuffSealer : public Sealer
{
public:
    HotStuffSealer(std::shared_ptr<dev::txpool::TxPoolInterface> _txPool,
        std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain,
        std::shared_ptr<dev::sync::SyncInterface> _blockSync)
      : Sealer(_txPool, _blockChain, _blockSync)
    {}

    void setConsensusEngineFactory(std::shared_ptr<ConsensusEngineFactory> hotstuffEngineFactory)
    {
        m_consensusEngineFactory = hotstuffEngineFactory;
    }
    virtual std::shared_ptr<HotStuffEngine> createConsensusEngine(
        std::shared_ptr<dev::p2p::P2PInterface> _service,
        std::shared_ptr<dev::txpool::TxPoolInterface> _txPool,
        std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain,
        std::shared_ptr<dev::sync::SyncInterface> _blockSync,
        std::shared_ptr<dev::blockverifier::BlockVerifierInterface> _blockVerifier,
        dev::PROTOCOL_ID const& _protocolId, KeyPair const& _keyPair,
        h512s const& _sealerList = h512s())
    {
        m_consensusEngine = m_consensusEngineFactory->createConsensusEngine(_service, _txPool,
            _blockChain, _blockSync, _blockVerifier, _protocolId, _keyPair, _sealerList);
        m_hotStuffEngine = std::dynamic_pointer_cast<HotStuffEngine>(m_consensusEngine);
        m_hotStuffEngine->onNotifyGeneratePrepare([&]() { m_canBroadCastPrepare = true; });
        return m_hotStuffEngine;
    }

    bool shouldSeal() override;
    void start() override;
    void stop() override;
    bool reachBlockIntervalTime() override;
    bool shouldHandleBlock();
    void handleBlock() override;

protected:
    std::atomic_bool m_canBroadCastPrepare = {false};
    ConsensusEngineFactory::Ptr m_consensusEngineFactory = nullptr;
    HotStuffEngine::Ptr m_hotStuffEngine = nullptr;
};
}  // namespace consensus
}  // namespace dev