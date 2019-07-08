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
 * (c) 2016-2018 fisco-dev contributors.
 */

#pragma once
#include "Common.h"

namespace dev
{
namespace consensus
{
class SuperViewChangeReq;
class PBFTMsgFactoryInterface
{
public:
    PBFTMsgFactoryInterface() {}
    virtual ~PBFTMsgFactoryInterface() {}
    virtual std::shared_ptr<ViewChangeReq> buildViewChangeReq(KeyPair const& keyPair,
        int64_t const& height, VIEWTYPE const view, IDXTYPE const& idx, h256 const& hash) = 0;
    virtual std::shared_ptr<ViewChangeReq> buildViewChangeReq() = 0;
};

class PBFTMsgFactory : public PBFTMsgFactoryInterface
{
public:
    PBFTMsgFactory() {}
    virtual ~PBFTMsgFactory() {}
    std::shared_ptr<ViewChangeReq> buildViewChangeReq() override
    {
        return std::make_shared<ViewChangeReq>();
    }

    std::shared_ptr<ViewChangeReq> buildViewChangeReq(KeyPair const& keyPair, int64_t const& height,
        VIEWTYPE const view, IDXTYPE const& idx, h256 const& hash) override
    {
        return std::make_shared<ViewChangeReq>(keyPair, height, view, idx, hash);
    }
};

}  // namespace consensus
}  // namespace dev