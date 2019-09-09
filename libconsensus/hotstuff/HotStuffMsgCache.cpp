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

HotStuffPrepareMsg::Ptr HotStuffMsgCache::findFuturePrepareMsg(
    dev::eth::BlockNumber const& blockNumber)
{
    if (!m_futurePrepareCache.count(blockNumber))
    {
        return nullptr;
    }
    return m_futurePrepareCache[blockNumber];
}

void HotStuffMsgCache::addRawPrepare(HotStuffPrepareMsg::Ptr msg)
{
    HOTSTUFFCache_LOG(DEBUG) << LOG_DESC("addRawPrepare")
                             << LOG_KV("hash", msg->blockHash().abridged())
                             << LOG_KV("height", msg->blockHeight()) << LOG_KV("view", msg->view())
                             << LOG_KV("reqIdx", msg->idx());
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

void HotStuffMsgCache::addNewViewCache(HotStuffNewViewMsg::Ptr msg)
{
    auto cacheSize = getNewViewCacheSize(msg->blockHeight(), msg->view());
    HOTSTUFFCache_LOG(DEBUG) << LOG_DESC("addNewViewCache")
                             << LOG_KV("hash", msg->blockHash().abridged())
                             << LOG_KV("height", msg->blockHeight()) << LOG_KV("view", msg->view())
                             << LOG_KV("cacheSize", (cacheSize)) << LOG_KV("reqIdx", msg->idx());
    m_newViewCache[msg->blockHeight()][msg->view()][msg->idx()] = msg;
}

void HotStuffMsgCache::addPrepareCache(HotStuffMsg::Ptr msg)
{
    auto cacheSize = getPrepareCacheSize(msg->blockHash());
    HOTSTUFFCache_LOG(DEBUG) << LOG_DESC("addPrepareCache")
                             << LOG_KV("hash", msg->blockHash().abridged())
                             << LOG_KV("height", msg->blockHeight()) << LOG_KV("view", msg->view())
                             << LOG_KV("cacheSize", (cacheSize + 1))
                             << LOG_KV("reqIdx", msg->idx());
    return addCache(m_prepareCache, msg, msg->blockHash());
}

void HotStuffMsgCache::addPreCommitCache(HotStuffMsg::Ptr msg)
{
    auto cacheSize = getPreCommitCacheSize(msg->blockHash());
    HOTSTUFFCache_LOG(DEBUG) << LOG_DESC("addPreCommitCache")
                             << LOG_KV("hash", msg->blockHash().abridged())
                             << LOG_KV("height", msg->blockHeight()) << LOG_KV("view", msg->view())
                             << LOG_KV("cacheSize", (cacheSize + 1))
                             << LOG_KV("reqIdx", msg->idx());
    return addCache(m_preCommitCache, msg, msg->blockHash());
}

void HotStuffMsgCache::addCommitCache(HotStuffMsg::Ptr msg)
{
    auto cacheSize = getCommitCacheSize(msg->blockHash());
    HOTSTUFFCache_LOG(DEBUG) << LOG_DESC("addCommitCache")
                             << LOG_KV("hash", msg->blockHash().abridged())
                             << LOG_KV("height", msg->blockHeight()) << LOG_KV("view", msg->view())
                             << LOG_KV("cacheSize", (cacheSize + 1))
                             << LOG_KV("reqIdx", msg->idx());
    return addCache(m_commitCache, msg, msg->blockHash());
}


size_t HotStuffMsgCache::getNewViewCacheSize(
    dev::eth::BlockNumber const& blockNumber, VIEWTYPE const& view)
{
    if (!m_newViewCache.count(blockNumber))
    {
        return 0;
    }
    if (!m_newViewCache[blockNumber].count(view))
    {
        return 0;
    }
    return m_newViewCache[blockNumber][view].size();
}

size_t HotStuffMsgCache::getPrepareCacheSize(h256 const& blockHash)
{
    return getCollectedMsgSize(m_prepareCache, blockHash);
}

void HotStuffMsgCache::setSigList(QuorumCert::Ptr qcMsg)
{
    std::vector<std::pair<IDXTYPE, Signature>> sigs;
    if (qcMsg->type() == HotStuffPacketType::PrepareQCPacket)
    {
        getCollectedSigList(sigs, m_prepareCache, qcMsg->blockHash());
    }
    if (qcMsg->type() == HotStuffPacketType::PrecommitQCPacket)
    {
        getCollectedSigList(sigs, m_preCommitCache, qcMsg->blockHash());
    }
    if (qcMsg->type() == HotStuffPacketType::CommitQCPacket)
    {
        getCollectedSigList(sigs, m_commitCache, qcMsg->blockHash());
    }
    qcMsg->setSigList(sigs);
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

    eraseFutureQCMsg(HotStuffPacketType::PrepareQCPacket, blockHash);
    eraseFutureQCMsg(HotStuffPacketType::PrecommitQCPacket, blockHash);
    eraseFutureQCMsg(HotStuffPacketType::CommitQCPacket, blockHash);

    m_rawPrepareCache.reset();
    m_executedPrepareCache.reset();
    m_commitQC.reset();
}

void HotStuffMsgCache::removeInvalidViewChange(ViewCacheType& viewCache, VIEWTYPE const& view)
{
    for (auto it = viewCache.begin(); it != viewCache.end();)
    {
        if (it->first <= view)
        {
            it = viewCache.erase(it);
        }
        else
        {
            it++;
        }
    }
}

// get the maximum prepare view
QuorumCert::Ptr HotStuffMsgCache::getHighJustifyQC(
    dev::eth::BlockNumber const& blockNumber, VIEWTYPE const& curView)
{
    if (!m_newViewCache.count(blockNumber) || !m_newViewCache[blockNumber].count(curView))
    {
        HOTSTUFFCache_LOG(FATAL) << LOG_DESC(
                                        "generate prepare before collect enough NewView message")
                                 << LOG_KV("curView", curView);
    }
    QuorumCert::Ptr highQC = nullptr;
    VIEWTYPE maxView = 0;
    for (auto const& item : m_newViewCache[blockNumber][curView])
    {
        if (maxView == 0 || maxView < item.second->justifyView())
        {
            maxView = item.second->justifyView();
            highQC = item.second->justifyQC();
        }
    }
    HOTSTUFFCache_LOG(INFO) << LOG_DESC("getHighJustifyQC: get HighQC from the newViewCache")
                            << LOG_KV("hash", highQC->blockHash().abridged())
                            << LOG_KV("height", highQC->blockHeight())
                            << LOG_KV("idx", highQC->idx()) << LOG_KV("view", highQC->view())
                            << LOG_KV("curView", curView);
    return highQC;
}


void HotStuffMsgCache::addFuturePrepare(HotStuffPrepareMsg::Ptr futurePrepareMsg)
{
    // invalid future prepare message
    if (m_futurePrepareCache.count(futurePrepareMsg->blockHeight()) != 0)
    {
        if (futurePrepareMsg->view() <
            m_futurePrepareCache[futurePrepareMsg->blockHeight()]->view())
        {
            HOTSTUFFCache_LOG(WARNING)
                << LOG_DESC("addFuturePrepare: invalid futurePrepareMsg for lower view")
                << LOG_KV("reqHash", futurePrepareMsg->blockHash().abridged())
                << LOG_KV("reqHeight", futurePrepareMsg->blockHeight())
                << LOG_KV("reqView", futurePrepareMsg->view())
                << LOG_KV("reqIdx", futurePrepareMsg->idx());
            return;
        }
    }
    HOTSTUFFCache_LOG(DEBUG) << LOG_DESC("addFuturePrepare")
                             << LOG_KV("reqHash", futurePrepareMsg->blockHash().abridged())
                             << LOG_KV("reqHeight", futurePrepareMsg->blockHeight())
                             << LOG_KV("reqView", futurePrepareMsg->view())
                             << LOG_KV("reqIdx", futurePrepareMsg->idx());
    // valid future prepare message
    m_futurePrepareCache[futurePrepareMsg->blockHeight()] = futurePrepareMsg;
}

void HotStuffMsgCache::eraseFuturePrepare(dev::eth::BlockNumber const& blockNumber)
{
    for (auto it = m_futurePrepareCache.begin(); it != m_futurePrepareCache.end();)
    {
        if (it->second == nullptr || it->second->blockHeight() <= blockNumber.number())
        {
            it = m_futurePrepareCache.erase(it);
        }
        else
        {
            it++;
        }
    }
}

void HotStuffMsgCache::addFutureQC(QuorumCert::Ptr qcMsg)
{
    HOTSTUFFCache_LOG(DEBUG) << LOG_DESC("try to addFutureQC")
                             << LOG_KV("reqHash", qcMsg->blockHash().abridged())
                             << LOG_KV("reqHeight", qcMsg->blockHeight())
                             << LOG_KV("reqView", qcMsg->view()) << LOG_KV("idx", qcMsg->idx());
    if (!m_futureQCCache.count(qcMsg->type()))
    {
        std::unordered_map<h256, QuorumCert::Ptr> qcCache;
        m_futureQCCache[qcMsg->type()] = qcCache;
    }
    if (m_futureQCCache[qcMsg->type()].count(qcMsg->blockHash()))
    {
        auto cachedQC = m_futureQCCache[qcMsg->type()][qcMsg->blockHash()];
        if (cachedQC->view() <= qcMsg->view())
        {
            m_futureQCCache[qcMsg->type()][qcMsg->blockHash()] = qcMsg;
            return;
        }
    }
    m_futureQCCache[qcMsg->type()][qcMsg->blockHash()] = qcMsg;
}

QuorumCert::Ptr HotStuffMsgCache::getFutureQCMsg(int msgType, h256 const& blockHash)
{
    if (!m_futureQCCache.count(msgType))
    {
        return nullptr;
    }
    if (!m_futureQCCache[msgType].count(blockHash))
    {
        return nullptr;
    }
    return m_futureQCCache[msgType][blockHash];
}

void HotStuffMsgCache::collectCache(dev::eth::BlockHeader const& highestBlockHeader)
{
    removeInvalidEntryFromCache(highestBlockHeader, m_prepareCache);
    removeInvalidEntryFromCache(highestBlockHeader, m_preCommitCache);
    removeInvalidEntryFromCache(highestBlockHeader, m_commitCache);
    eraseFuturePrepare(highestBlockHeader.number());
}
