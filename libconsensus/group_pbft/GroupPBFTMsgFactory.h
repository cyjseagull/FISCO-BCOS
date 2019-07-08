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
#pragma once
#include "GroupPBFTMsg.h"
#include <libconsensus/pbft/PBFTMsgFactory.h>
namespace dev
{
namespace consensus
{
class GroupPBFTMsgFactory : public PBFTMsgFactoryInterface
{
public:
    GroupPBFTMsgFactory() {}
    virtual ~GroupPBFTMsgFactory() {}
    std::shared_ptr<ViewChangeReq> buildViewChangeReq() override
    {
        return std::make_shared<GroupViewChangeReq>();
    }

    std::shared_ptr<ViewChangeReq> buildViewChangeReq(KeyPair const& keyPair, int64_t const& height,
        VIEWTYPE const view, IDXTYPE const& idx, h256 const& hash) override
    {
        return std::make_shared<GroupViewChangeReq>(keyPair, height, view, idx, hash);
    }

    virtual std::shared_ptr<SuperViewChangeReq> buildSuperViewChangeReq()
    {
        return std::make_shared<SuperViewChangeReq>();
    }

    virtual std::shared_ptr<SuperViewChangeReq> buildSuperViewChangeReq(KeyPair const& keyPair,
        int64_t const& height, VIEWTYPE const view, IDXTYPE const& idx, h256 const& hash,
        uint8_t type = 0)
    {
        return std::make_shared<SuperViewChangeReq>(keyPair, height, view, idx, hash, type);
    }
};
}  // namespace consensus
}  // namespace dev