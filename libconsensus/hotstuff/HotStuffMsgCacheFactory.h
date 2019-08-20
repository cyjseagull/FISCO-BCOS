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
 * @file: HotStuffEngine.h
 * @author: yujiechen
 * @date: 2019-8-16
 */
#pragma once
#include "HotStuffMsgCache.h"
namespace dev
{
namespace consensus
{
class HotStuffMsgCacheFactory
{
public:
    using Ptr = std::shared_ptr<HotStuffMsgCacheFactory>;
    HotStuffMsgCacheFactory() = default;
    virtual ~HotStuffMsgCacheFactory() {}

    HotStuffMsgCache::Ptr createHotStuffMsgCache() { return std::make_shared<HotStuffMsgCache>(); }
};

}  // namespace consensus
}  // namespace dev