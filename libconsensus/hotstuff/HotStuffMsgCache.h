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
    bool existedInPrepareQC(h256 const& blockHash);
    bool existedLockedQC(h256 const& blockHash);
    bool existedPrepareMsg(h256 const& blockHash);
    HotStuffPrepareMsg::Ptr findFuturePrepareMsg(dev::eth::BlockNumber const& blockNumber);
    void eraseFuturePrepare(dev::eth::BlockNumber const& blockNumber);
    size_t getFuturePrepareSize() { return m_futurePrepareCache.size(); }

    // add hotstuff message into the cache
    void addRawPrepare(HotStuffPrepareMsg::Ptr msg);
    void addExecutedPrepare(HotStuffPrepareMsg::Ptr msg);
    void addLockedQC(QuorumCert::Ptr msg);

    void addNewViewCache(HotStuffNewViewMsg::Ptr msg, size_t const& minValidNodes);
    void addPrepareCache(HotStuffMsg::Ptr msg, size_t const& minValidNodes);
    void addPreCommitCache(HotStuffMsg::Ptr msg, size_t const& minValidNodes);
    void addCommitCache(HotStuffMsg::Ptr msg, size_t const& minValidNodes);

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

    // collect cache periodly
    virtual void collectCache(dev::eth::BlockHeader const& highestBlockHeader);

    // add future prepare
    virtual void addFuturePrepare(HotStuffPrepareMsg::Ptr futurePrepareMsg);

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

    /// remove invalid requests cached in cache according to current block
    template <typename T, typename U, typename S>
    void inline removeInvalidEntryFromCache(dev::eth::BlockHeader const& highestBlockHeader,
        std::unordered_map<T, std::unordered_map<U, S>>& cache)
    {
        for (auto it = cache.begin(); it != cache.end();)
        {
            for (auto cache_entry = it->second.begin(); cache_entry != it->second.end();)
            {
                /// delete expired cache
                if (cache_entry->second->blockHeight() < highestBlockHeader.number())
                    cache_entry = it->second.erase(cache_entry);
                /// in case of faked block hash
                else if (cache_entry->second->blockHeight() == highestBlockHeader.number() &&
                         cache_entry->second->blockHash() != highestBlockHeader.hash())
                    cache_entry = it->second.erase(cache_entry);
                else
                    cache_entry++;
            }
            if (it->second.size() == 0)
                it = cache.erase(it);
            else
                it++;
        }
    }

    // remove invalid future prepare
    void removeInvalidFuturePrepare(dev::eth::BlockHeader const& highestBlockHeader)
    {
        for (auto it = m_futurePrepareCache.begin(); it < m_futurePrepareCache.end();)
        {
            if (it->second->blockHeight() <= highestBlockHeader.number())
            {
                it = it->erase(it);
            }
            else
            {
                it++;
            }
        }
    }

private:
    HotStuffPrepareMsg::Ptr m_executedPrepareCache = nullptr;
    HotStuffPrepareMsg::Ptr m_rawPrepareCache = nullptr;
    QuorumCert::Ptr m_lockedQC = nullptr;
    QuorumCert::Ptr m_prepareQC = nullptr;
    std::unordered_map<VIEWTYPE, std::unordered_map<std::string, HotStuffNewViewMsg::Ptr>>
        m_newViewCache;
    std::unordered_map<h256, std::unordered_map<std::string, HotStuffMsg::Ptr>> m_prepareCache;
    std::unordered_map<h256, std::unordered_map<std::string, HotStuffMsg::Ptr>> m_preCommitCache;
    std::unordered_map<h256, std::unordered_map<std::string, HotStuffMsg::Ptr>> m_commitCache;
    // futurePrepare cache
    std::unordered_map<dev::eth::BlockNumber, HotStuffPrepareMsg::Ptr> m_futurePrepareCache;
};

}  // namespace consensus
}  // namespace dev