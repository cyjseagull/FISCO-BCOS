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
#include "Common.h"
#include "HotStuffMsg.h"
#include <libconsensus/Common.h>

namespace dev
{
namespace consensus
{
class HotStuffMsgCache
{
public:
    using Ptr = std::shared_ptr<HotStuffMsgCache>;

    HotStuffMsgCache() = default;
    ~HotStuffMsgCache() {}
    // check the existence of the message
    bool existedRawPrepare(h256 const& blockHash);
    bool existedExecutedPrepare(h256 const& blockHash);
    bool existedLockedQC(h256 const& blockHash);
    bool existedPrepareMsg(h256 const& blockHash);

    // add hotstuff message into the cache
    void addRawPrepare(HotStuffMsg::Ptr msg);
    void addExecutedPrepare(HotStuffPrepareMsg::Ptr msg);
    void addLockedQC(QuorumCert::Ptr msg);

    void addNewViewCache(HotStuffNewViewMsg::Ptr msg);
    void addPrepareCache(HotStuffMsg::Ptr msg);
    void addPreCommitCache(HotStuffMsg::Ptr msg);
    void addCommitCache(HotStuffMsg::Ptr msg);

    // get cache size
    size_t getNewViewCacheSize(VIEWTYPE const& view);
    size_t getPrepareCacheSize(h256 const& blockHash);
    size_t getPreCommitCacheSize(h256 const& blockHash);
    size_t getCommitCacheSize(h256 const& blockHash);

    // reset cache when commit a new block
    void resetCacheAfterCommit(h256 const& blockHash);

    HotStuffPrepareMsg::Ptr executedPrepareCache() { return m_executedPrepareCache; }

    void removeInvalidViewChange(VIEWTYPE const& view);

    void setPrepareQC(QuorumCert::Ptr _prepareQC) { m_prepareQC = _prepareQC; }

    QuorumCert::Ptr prepareQC() { return m_prepareQC; }

    QuorumCert::Ptr lockedQC() { return m_lockedQC; }

    VIEWTYPE getMaxJustifyView(VIEWTYPE const& curView);

protected:
    template <typename T, typename U, typename S>
    void addCache(T& cache, std::shared_ptr<U> msg, S const& mainKey)
    {
        cache[mainKey][msg->patialSig().hex()] = msg;
    }

    template <typename T, typename S>
    size_t getCollectedMsgSize(T& cache, S const& key)
    {
        if (!cache.count(key))
        {
            return 0;
        }
        return cache[key].size();
    }

    template <typename T, typename S>
    void clearCache(T& cache, S const& key)
    {
        if (!cache.count(key))
        {
            return;
        }
        cache.erase(key);
    }

private:
    HotStuffPrepareMsg::Ptr m_executedPrepareCache = nullptr;
    HotStuffMsg::Ptr m_rawPrepareCache = nullptr;
    QuorumCert::Ptr m_lockedQC = nullptr;
    QuorumCert::Ptr m_prepareQC = nullptr;
    std::unordered_map<VIEWTYPE, std::unordered_map<std::string, HotStuffNewViewMsg::Ptr>>
        m_newViewCache;
    std::unordered_map<h256, std::unordered_map<std::string, HotStuffMsg::Ptr>> m_prepareCache;
    std::unordered_map<h256, std::unordered_map<std::string, HotStuffMsg::Ptr>> m_preCommitCache;
    std::unordered_map<h256, std::unordered_map<std::string, HotStuffMsg::Ptr>> m_commitCache;
};

}  // namespace consensus
}  // namespace dev
