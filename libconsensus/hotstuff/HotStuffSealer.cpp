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
#include "HotStuffSealer.h"

using namespace dev;
using namespace dev::consensus;

bool HotStuffSealer::shouldSeal()
{
    return Sealer::shouldSeal() && m_hotStuffEngine->shouldSeal();
}

void HotStuffSealer::start()
{
    m_hotStuffEngine->start();
    Sealer::start();
}

void HotStuffSealer::stop()
{
    Sealer::stop();
    m_hotStuffEngine->stop();
}

void HotStuffSealer::handleBlock()
{
    // check the max transaction number of a block
    if (m_sealing->block->getTransactionSize() > m_hotStuffEngine->maxBlockTransactions())
    {
        HOTSTUFFSEALER_LOG(ERROR)
            << LOG_DESC("Drop block for transaction number is over maxTransactionLimit")
            << LOG_KV("transactionNum", m_sealing->block->getTransactionSize())
            << LOG_KV("maxTransNum", m_hotStuffEngine->maxBlockTransactions())
            << LOG_KV("blockNum", m_sealing->block->blockHeader().number());
        resetSealingBlock();
        // notify to re-generate the block
        m_signalled.notify_all();
        m_blockSignalled.notify_all();
        return;
    }
    setBlock();
    HOTSTUFFSEALER_LOG(INFO) << LOG_DESC("++++++++++++++++ Generating seal on")
                             << LOG_KV("blkNum", m_sealing->block->header().number())
                             << LOG_KV("tx", m_sealing->block->getTransactionSize())
                             << LOG_KV("nodeIdx", m_hotStuffEngine->nodeIdx())
                             << LOG_KV("hash", m_sealing->block->header().hash().abridged());

    m_hotStuffEngine->generateAndBroadcastPrepare(m_sealing->block);
#if 0
    if(m_hotStuffEngine->shouldReset(m_sealing->block))
    {
        resetSealingBlock();
        m_signalled.notify_all();
        m_blockSignalled.notify_all();
    }
#endif
    m_canBroadCastPrepare = false;
}

bool HotStuffSealer::reachBlockIntervalTime()
{
    return m_hotStuffEngine->reachBlockIntervalTime() ||
           (m_sealing->block->getTransactionSize() > 0 && m_hotStuffEngine->reachMinBlockGenTime());
}


bool HotStuffSealer::shouldHandleBlock()
{
    // not the leader
    if (m_hotStuffEngine->getLeader() != m_hotStuffEngine->nodeIdx())
    {
        return false;
    }
    // hasn't generate the new block yet
    if (m_sealing->block->blockHeader().number() != (m_blockChain->number() + 1))
    {
        return false;
    }
    if (!m_canBroadCastPrepare)
    {
        return false;
    }
    return true;
}