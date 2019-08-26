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
 * @brief : cache for group pbft
 * @file: GroupPBFTReqCache.h
 * @author: yujiechen
 * @date: 2019-5-28
 */
#pragma once
#include "Common.h"
#include "GroupPBFTMsg.h"
#include <libconsensus/pbft/PBFTReqCache.h>
namespace dev
{
namespace consensus
{
class GroupPBFTReqCache : public PBFTReqCache
{
public:
    bool superSignReqExists(std::shared_ptr<SuperSignReq> req, ZONETYPE const& zoneId)
    {
        return cacheExists(m_superSignCache, req->block_hash, zoneId);
    }

    bool superCommitReqExists(std::shared_ptr<SuperCommitReq> req, ZONETYPE const& zoneId)
    {
        return cacheExists(m_superSignCache, req->block_hash, zoneId);
    }

    void addSuperSignReq(std::shared_ptr<SuperSignReq> req, ZONETYPE const& zoneId)
    {
        GPBFTREQCACHE_LOG(DEBUG) << LOG_DESC("addSuperSignReq") << LOG_KV("height", req->height)
                                 << LOG_KV("genZone", zoneId) << LOG_KV("genIdx", req->idx)
                                 << LOG_KV("hash", req->block_hash.abridged());
        m_superSignCache[req->block_hash][zoneId] = req;
    }
    void addSuperCommitReq(std::shared_ptr<SuperCommitReq> req, ZONETYPE const& zoneId)
    {
        GPBFTREQCACHE_LOG(DEBUG) << LOG_DESC("addSuperCommitReq") << LOG_KV("height", req->height)
                                 << LOG_KV("genZone", zoneId) << LOG_KV("genIdx", req->idx)
                                 << LOG_KV("hash", req->block_hash.abridged());
        m_superCommitCache[req->block_hash][zoneId] = req;
    }

    void addSuperViewChangeReq(std::shared_ptr<SuperViewChangeReq> req, ZONETYPE const& zoneId)
    {
        GPBFTREQCACHE_LOG(DEBUG) << LOG_DESC("addSuperViewChangeReq")
                                 << LOG_KV("height", req->height) << LOG_KV("view", req->view)
                                 << LOG_KV("type", req->isGlobal()) << LOG_KV("genZone", zoneId)
                                 << LOG_KV("genIdx", req->idx)
                                 << LOG_KV("hash", req->block_hash.abridged());
        if (req->isGlobal())
        {
            m_globalSuperViewChangeCache[req->view][zoneId] = req;
        }
        else
        {
            m_superViewChangeCache[req->view][zoneId] = req;
        }
    }

    size_t getSuperViewChangeSize(VIEWTYPE const& toView) const
    {
        return getSizeFromCache(toView, m_superViewChangeCache);
    }

    size_t getGlobalSuperViewChangeSize(VIEWTYPE const& toView) const
    {
        return getSizeFromCache(toView, m_globalSuperViewChangeCache);
    }


    size_t getSuperSignCacheSize(h256 const& blockHash) const
    {
        return getSizeFromCache(blockHash, m_superSignCache);
    }

    size_t getSuperCommitCacheSize(h256 const& blockHash) const
    {
        return getSizeFromCache(blockHash, m_superCommitCache);
    }

    std::unordered_map<h256, std::unordered_map<ZONETYPE, std::shared_ptr<SuperSignReq>>> const&
    superSignCache()
    {
        return m_superSignCache;
    }
    std::unordered_map<h256, std::unordered_map<ZONETYPE, std::shared_ptr<SuperCommitReq>>> const&
    superCommitCache()
    {
        return m_superCommitCache;
    }

    std::unordered_map<h256, std::unordered_map<std::string, std::shared_ptr<SignReq>>> const&
    signCache()
    {
        return m_signCache;
    }
    std::unordered_map<h256, std::unordered_map<std::string, std::shared_ptr<CommitReq>>> const&
    commitCache()
    {
        return m_commitCache;
    }

    std::unordered_map<VIEWTYPE,
        std::unordered_map<ZONETYPE, std::shared_ptr<SuperViewChangeReq>>> const&
    superViewChangeCache()
    {
        return m_superViewChangeCache;
    }

    void triggerViewChange(VIEWTYPE const& curView) override
    {
        PBFTReqCache::triggerViewChange(curView);
        m_superSignCache.clear();
        m_superCommitCache.clear();
        removeSuperViewChangeReqByView(curView, m_superViewChangeCache);
        removeSuperViewChangeReqByView(curView, m_globalSuperViewChangeCache);
        PBFTReqCache::removeAllInvalidViewChange(curView, m_globalViewChangeReq);
    }

    void removeInvalidViewChange(
        VIEWTYPE const& view, dev::eth::BlockHeader const& highestBlock) override
    {
        // remove invalid viewchangeReq
        PBFTReqCache::removeInvalidViewChangeCache(view, highestBlock, m_globalViewChangeReq);
        // remove invalid superviewchangeReq
        removeInvalidSuperViewChangeReq(view, highestBlock, m_superViewChangeCache);
        // remove invalid globalSuperViewChangeReq
        removeInvalidSuperViewChangeReq(view, highestBlock, m_globalSuperViewChangeCache)
    }

    void addViewChangeReq(std::shared_ptr<ViewChangeReq> req)
    {
        if (!req->isGlobal())
        {
            PBFTReqCache::addViewChangeReq(req, m_recvViewChangeReq);
        }
        else
        {
            PBFTReqCache::addViewChangeReq(req, m_globalViewChangeReq);
        }
    }

    inline size_t getGlobalViewChangeSize(VIEWTYPE const& toView) const
    {
        return getSizeFromCache(toView, m_globalViewChangeReq);
    }

private:
    template <typename T>
    void removeInvalidSuperViewChangeReq(
        VIEWTYPE const& view, dev::eth::BlockHeader const& highestBlock, T const& cache)
    {
        auto it = cache.find(view);
        if (it == cache.end())
        {
            return;
        }
        for (auto pSuperView = it->second.begin(); pSuperView != it->second.end();)
        {
            // remove invalid superViewChangeRequest with small block number
            if (pSuperView->second->height < highestBlock.number())
            {
                pSuperView = it->second.erase(pSuperView);
            }
            else if (pSuperView->second->height == highestBlock.number() &&
                     pSuperView->second->block_hash != highestBlock.hash())
            {
                pSuperView = it->second.erase(pSuperView);
            }
            else
            {
                pSuperView++;
            }
        }
    }


    template <typename T>
    void removeSuperViewChangeReqByView(VIEWTYPE const& curView, T const& cache)
    {
        for (auto it = cache.begin(); it != cache.end();)
        {
            if (it->first <= curView)
            {
                it = cache.erase(it);
            }
            else
            {
                it++;
            }
        }
    }

protected:
    // cache for SuperSignReq
    std::unordered_map<h256, std::unordered_map<ZONETYPE, std::shared_ptr<SuperSignReq>>>
        m_superSignCache;
    // cache for SuperCommitReq
    std::unordered_map<h256, std::unordered_map<ZONETYPE, std::shared_ptr<SuperCommitReq>>>
        m_superCommitCache;

    // cache for SuperViewChangeReq
    std::unordered_map<VIEWTYPE, std::unordered_map<ZONETYPE, std::shared_ptr<SuperViewChangeReq>>>
        m_superViewChangeCache;

    // cache for globalSuperViewChangeReq
    std::unordered_map<VIEWTYPE, std::unordered_map<ZONETYPE, std::shared_ptr<SuperViewChangeReq>>>
        m_globalSuperViewChangeCache;

    std::unordered_map<VIEWTYPE, std::unordered_map<IDXTYPE, std::shared_ptr<ViewChangeReq>>>
        m_globalViewChangeReq;
};
}  // namespace consensus
}  // namespace dev
