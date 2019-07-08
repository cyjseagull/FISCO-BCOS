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
 * @brief : implementation of Grouped PBFT Message
 * @file: GroupPBFTMsg.h
 * @author: yujiechen
 * @date: 2019-5-28
 */
#pragma once
#include <libconsensus/pbft/Common.h>

namespace dev
{
namespace consensus
{
struct GroupPBFTPacketType : public PBFTPacketType
{
    static const int SuperSignReqPacket = 0x04;
    static const int SuperCommitReqPacket = 0x05;
    static const int SuperViewChangeReqPacket = 0x06;
};

struct GroupViewChangeReq : public ViewChangeReq
{
    uint8_t type = 0;

    GroupViewChangeReq() = default;
    GroupViewChangeReq(KeyPair const& keyPair, int64_t const& _height, VIEWTYPE const _view,
        IDXTYPE const& _idx, h256 const& _hash, uint8_t global = 0)
      : ViewChangeReq(keyPair, _height, _view, _idx, _hash), type(global)
    {}

    void setType(uint8_t const& _type) { type = _type; }

    bool isGlobal() override
    {
        if (type == 0)
        {
            return false;
        }
        return true;
    }

    void streamRLPFields(RLPStream& _s) const override
    {
        _s << height << view << idx << timestamp << block_hash << sig.asBytes() << sig2.asBytes()
           << type;
    }

    /// populate specified rlp into PBFTMsg object
    void populate(RLP const& rlp) override
    {
        try
        {
            ViewChangeReq::populate(rlp);
            type = rlp[7].toInt<uint8_t>();
        }
        catch (Exception const& _e)
        {
            LOG(ERROR) << "populate GroupViewChangeReq failed";
            throw _e;
        }
    }
};

struct SuperSignReq : public PBFTMsg
{
    SuperSignReq() {}
    // construct SuperSignReq from the given prepare request
    SuperSignReq(
        std::shared_ptr<PrepareReq> prepareReq, VIEWTYPE const& globalView, IDXTYPE const& nodeIdx)
    {
        idx = nodeIdx;
        block_hash = prepareReq->block_hash;
        height = prepareReq->height;
        view = globalView;
        timestamp = u256(utcTime());
    }
    std::string uniqueKey() const override
    {
        auto uniqueKey = std::to_string(idx) + "_" + block_hash.hex() + "_" + std::to_string(view);
        return uniqueKey;
    }
};

struct SuperViewChangeReq : public GroupViewChangeReq
{
    SuperViewChangeReq() {}

    SuperViewChangeReq(KeyPair const& keyPair, int64_t const& _height, VIEWTYPE const _view,
        IDXTYPE const& _idx, h256 const& _hash, uint8_t type = 0)
      : GroupViewChangeReq(keyPair, _height, _view, _idx, _hash, type)
    {}
    std::string uniqueKey() const override
    {
        auto uniqueKey = std::to_string(idx) + "_" + block_hash.hex() + "_" + std::to_string(view);
        return uniqueKey;
    }
};

struct SuperCommitReq : public SuperSignReq
{
    SuperCommitReq() {}
    // construct SuperCommitReq from the given prepare request
    SuperCommitReq(
        std::shared_ptr<PrepareReq> prepareReq, VIEWTYPE const& globalView, IDXTYPE const& nodeIdx)
      : SuperSignReq(prepareReq, globalView, nodeIdx)
    {}
};

}  // namespace consensus
}  // namespace dev