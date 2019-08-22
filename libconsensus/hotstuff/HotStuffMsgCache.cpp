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

#include "HotStuffMsgCache.h"

using namespace dev;
using namespace dev::consensus;
bool HotStuffMsgCache::existedRawPrepare(h256 const& blockHash)
{
    if (!m_rawPrepareCache)
    {
        return false;
    }
    return (m_rawPrepareCache->blockHash() == blockHash);
}

bool HotStuffMsgCache::existedExecutedPrepare(h256 const& blockHash)
{
    if (!m_executedPrepareCache)
    {
        return false;
    }
    return (m_executedPrepareCache->blockHash() == blockHash);
}

bool HotStuffMsgCache::existedInPrepareQC(h256 const& blockHash)
{
    if (!m_prepareQC)
    {
        return false;
    }
    return (m_prepareQC->blockHash() == blockHash);
}

bool HotStuffMsgCache::existedLockedQC(h256 const& blockHash)
{
    if (!m_lockedQC)
    {
        return false;
    }
    return (m_lockedQC->blockHash() == blockHash);
}

void HotStuffMsgCache::addRawPrepare(HotStuffPrepareMsg::Ptr msg)
{
    HOTSTUFFCache_LOG(DEBUG) << LOG_DESC("addRawPrepare")
                             << LOG_KV("hash", msg->blockHash().abridged())
                             << LOG_KV("height", msg->blockHeight())
                             << LOG_KV("txNum", msg->getBlock()->getTransactionSize())
                             << LOG_KV("view", msg->view()) << LOG_KV("reqIdx", msg->idx());
    m_rawPrepareCache = msg;
}

void HotStuffMsgCache::addExecutedPrepare(HotStuffPrepareMsg::Ptr msg)
{
    HOTSTUFFCache_LOG(DEBUG) << LOG_DESC("addExecutedPrepare")
                             << LOG_KV("hash", msg->blockHash().abridged())
                             << LOG_KV("height", msg->blockHeight())
                             << LOG_KV("txNum", msg->getBlock()->getTransactionSize())
                             << LOG_KV("view", msg->view()) << LOG_KV("reqIdx", msg->idx());
    m_executedPrepareCache = msg;
}

void HotStuffMsgCache::addLockedQC(QuorumCert::Ptr msg)
{
    HOTSTUFFCache_LOG(DEBUG) << LOG_DESC("addLockedQC when receive precommitQC")
                             << LOG_KV("hash", msg->blockHash().abridged())
                             << LOG_KV("height", msg->blockHeight()) << LOG_KV("view", msg->view())
                             << LOG_KV("reqIdx", msg->idx());
    m_lockedQC = msg;
}

void HotStuffMsgCache::addNewViewCache(HotStuffNewViewMsg::Ptr msg, size_t const& minValidNodes)
{
    auto cacheSize = getNewViewCacheSize(msg->view());
    if (cacheSize >= minValidNodes)
    {
        return;
    }
    HOTSTUFFCache_LOG(DEBUG) << LOG_DESC("addNewViewCache")
                             << LOG_KV("hash", msg->blockHash().abridged())
                             << LOG_KV("height", msg->blockHeight()) << LOG_KV("view", msg->view())
                             << LOG_KV("cacheSize", (cacheSize + 1))
                             << LOG_KV("reqIdx", msg->idx());
    return addCache(m_newViewCache, msg, msg->view());
}
void HotStuffMsgCache::addPrepareCache(HotStuffMsg::Ptr msg, size_t const& minValidNodes)
{
    auto cacheSize = getPrepareCacheSize(msg->blockHash());
    if (cacheSize >= minValidNodes)
    {
        return;
    }
    HOTSTUFFCache_LOG(DEBUG) << LOG_DESC("addPrepareCache")
                             << LOG_KV("hash", msg->blockHash().abridged())
                             << LOG_KV("height", msg->blockHeight()) << LOG_KV("view", msg->view())
                             << LOG_KV("cacheSize", (cacheSize + 1))
                             << LOG_KV("reqIdx", msg->idx());
    return addCache(m_prepareCache, msg, msg->blockHash());
}

void HotStuffMsgCache::addPreCommitCache(HotStuffMsg::Ptr msg, size_t const& minValidNodes)
{
    auto cacheSize = getPreCommitCacheSize(msg->blockHash());
    if (cacheSize >= minValidNodes)
    {
        return;
    }
    HOTSTUFFCache_LOG(DEBUG) << LOG_DESC("addPreCommitCache")
                             << LOG_KV("hash", msg->blockHash().abridged())
                             << LOG_KV("height", msg->blockHeight()) << LOG_KV("view", msg->view())
                             << LOG_KV("cacheSize", (cacheSize + 1))
                             << LOG_KV("reqIdx", msg->idx());
    return addCache(m_preCommitCache, msg, msg->blockHash());
}

void HotStuffMsgCache::addCommitCache(HotStuffMsg::Ptr msg, size_t const& minValidNodes)
{
    auto cacheSize = getCommitCacheSize(msg->blockHash());
    if (cacheSize >= minValidNodes)
    {
        return;
    }
    HOTSTUFFCache_LOG(DEBUG) << LOG_DESC("addCommitCache")
                             << LOG_KV("hash", msg->blockHash().abridged())
                             << LOG_KV("height", msg->blockHeight()) << LOG_KV("view", msg->view())
                             << LOG_KV("newViewSize", (cacheSize + 1))
                             << LOG_KV("reqIdx", msg->idx());
    return addCache(m_commitCache, msg, msg->blockHash());
}


size_t HotStuffMsgCache::getNewViewCacheSize(VIEWTYPE const& view)
{
    return getCollectedMsgSize(m_newViewCache, view);
}

size_t HotStuffMsgCache::getPrepareCacheSize(h256 const& blockHash)
{
    return getCollectedMsgSize(m_prepareCache, blockHash);
}

size_t HotStuffMsgCache::getPreCommitCacheSize(h256 const& blockHash)
{
    return getCollectedMsgSize(m_preCommitCache, blockHash);
}
size_t HotStuffMsgCache::getCommitCacheSize(h256 const& blockHash)
{
    return getCollectedMsgSize(m_commitCache, blockHash);
}

void HotStuffMsgCache::resetCacheAfterCommit(h256 const& blockHash)
{
    clearCache(m_prepareCache, blockHash);
    clearCache(m_preCommitCache, blockHash);
    clearCache(m_commitCache, blockHash);
    m_rawPrepareCache.reset();
    m_executedPrepareCache.reset();
}

void HotStuffMsgCache::removeInvalidViewChange(VIEWTYPE const& view)
{
    for (auto it = m_newViewCache.begin(); it != m_newViewCache.end();)
    {
        if (it->first <= view)
        {
            it = m_newViewCache.erase(it);
        }
        else
        {
            it++;
        }
    }
}

// get the maximum prepare view
VIEWTYPE HotStuffMsgCache::getMaxJustifyView(VIEWTYPE const& curView)
{
    if (!m_newViewCache.count(curView))
    {
        HOTSTUFFCache_LOG(FATAL) << LOG_DESC(
                                        "generate prepare before collect enough NewView message")
                                 << LOG_KV("curView", curView);
    }
    VIEWTYPE maxView = 0;
    for (auto const& item : m_newViewCache[curView])
    {
        if (maxView < item.second->justifyView())
        {
            maxView = item.second->justifyView();
        }
    }
    HOTSTUFFCache_LOG(INFO) << LOG_DESC("obtain maxView from the newViewCache")
                            << LOG_KV("curView", curView) << LOG_KV("maxView", maxView);
    return maxView;
}

void HotStuffMsgCache::collectCache(dev::eth::BlockHeader const& highestBlockHeader)
{
    removeInvalidEntryFromCache(highestBlockHeader, m_prepareCache);
    removeInvalidEntryFromCache(highestBlockHeader, m_preCommitCache);
    removeInvalidEntryFromCache(highestBlockHeader, m_commitCache);
}
