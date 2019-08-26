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
#include "HotStuffMsg.h"
namespace dev
{
namespace consensus
{
class HotStuffMsgFactoryInterface
{
public:
    HotStuffMsgFactoryInterface() {}
    virtual ~HotStuffMsgFactoryInterface() {}
    virtual HotStuffMsg::Ptr buildHotStuffMsg() = 0;
    virtual HotStuffMsg::Ptr buildHotStuffMsg(KeyPair const& keyPair, int const& _type,
        IDXTYPE const& _idx, h256 const& _blockHash, dev::eth::BlockNumber const& _blockHeight,
        VIEWTYPE const& _view) = 0;

    virtual QuorumCert::Ptr buildQuorumCert(KeyPair const& keyPair, int const& _type,
        IDXTYPE const& _idx, h256 const& _blockHash, dev::eth::BlockNumber const& _blockHeight,
        VIEWTYPE const& _view) = 0;

    virtual HotStuffNewViewMsg::Ptr buildHotStuffNewViewMsg(KeyPair const& keyPair,
        IDXTYPE const& _idx, h256 const& _blockHash, dev::eth::BlockNumber const& _blockHeight,
        VIEWTYPE const& _view, QuorumCert::Ptr _justifyQC) = 0;

    virtual HotStuffPrepareMsg::Ptr buildHotStuffPrepare(KeyPair const& keyPair,
        IDXTYPE const& _idx, h256 const& _blockHash, dev::eth::BlockNumber const& _blockHeight,
        VIEWTYPE const& _view, QuorumCert::Ptr _justifyQC) = 0;
};

class HotStuffMsgFactory : public HotStuffMsgFactoryInterface
{
public:
    using Ptr = std::shared_ptr<HotStuffMsgFactory>;
    HotStuffMsgFactory() = default;
    virtual ~HotStuffMsgFactory() {}
    HotStuffMsg::Ptr buildHotStuffMsg() override { return std::make_shared<HotStuffMsg>(); }
    HotStuffMsg::Ptr buildHotStuffMsg(KeyPair const& keyPair, int const& _type, IDXTYPE const& _idx,
        h256 const& _blockHash, dev::eth::BlockNumber const& _blockHeight,
        VIEWTYPE const& _view) override
    {
        return std::make_shared<HotStuffMsg>(keyPair, _type, _idx, _blockHash, _blockHeight, _view);
    }

    HotStuffPrepareMsg::Ptr buildHotStuffPrepare(KeyPair const& keyPair, IDXTYPE const& _idx,
        h256 const& _blockHash, dev::eth::BlockNumber const& _blockHeight, VIEWTYPE const& _view,
        QuorumCert::Ptr _justifyQC) override
    {
        int packetType = HotStuffPacketType::PreparePacket;
        return std::make_shared<HotStuffPrepareMsg>(
            keyPair, packetType, _idx, _blockHash, _blockHeight, _view, _justifyQC);
    }

    HotStuffNewViewMsg::Ptr buildHotStuffNewViewMsg(KeyPair const& keyPair, IDXTYPE const& _idx,
        h256 const& _blockHash, dev::eth::BlockNumber const& _blockHeight, VIEWTYPE const& _view,
        QuorumCert::Ptr _justifyQC) override
    {
        int packetType = HotStuffPacketType::NewViewPacket;
        return std::make_shared<HotStuffNewViewMsg>(
            keyPair, packetType, _idx, _blockHash, _blockHeight, _view, _justifyQC);
    }

    QuorumCert::Ptr buildQuorumCert(KeyPair const& keyPair, int const& _type, IDXTYPE const& _idx,
        h256 const& _blockHash, dev::eth::BlockNumber const& _blockHeight,
        VIEWTYPE const& _view) override
    {
        return std::make_shared<QuorumCert>(keyPair, _type, _idx, _blockHash, _blockHeight, _view);
    }
};

}  // namespace consensus
}  // namespace dev
