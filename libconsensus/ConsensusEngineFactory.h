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
 * @brief : factory used to create PBFTEngine
 * @file: PBFTEngineFactory.h
 * @author: yujiechen
 *
 * @date: 2019-06-24
 *
 */
#pragma once
#include "ConsensusEngineBase.h"
namespace dev
{
namespace consensus
{
class ConsensusEngineFactory
{
public:
    using Ptr = std::shared_ptr<ConsensusEngineFactory>;
    ConsensusEngineFactory() {}
    virtual ~ConsensusEngineFactory() {}

    virtual std::shared_ptr<ConsensusEngineBase> createConsensusEngine(
        std::shared_ptr<dev::p2p::P2PInterface> service,
        std::shared_ptr<dev::txpool::TxPoolInterface> txPool,
        std::shared_ptr<dev::blockchain::BlockChainInterface> blockChain,
        std::shared_ptr<dev::sync::SyncInterface> blockSync,
        std::shared_ptr<dev::blockverifier::BlockVerifierInterface> blockVerifier,
        dev::PROTOCOL_ID const& protocolId, KeyPair const& keyPair,
        h512s const& sealerList = h512s()) = 0;
};
}  // namespace consensus
}  // namespace dev