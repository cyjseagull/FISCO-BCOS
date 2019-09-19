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

/**
 * @brief : implementation of PBFT consensus
 * @file: ConsensusEngineBase.cpp
 * @author: yujiechen
 * @date: 2018-09-28
 */
#include "ConsensusEngineBase.h"
using namespace dev::eth;
using namespace dev::db;
using namespace dev::blockverifier;
using namespace dev::blockchain;
using namespace dev::p2p;
namespace dev
{
namespace consensus
{
void ConsensusEngineBase::start()
{
    if (m_startConsensusEngine)
    {
        ENGINE_LOG(WARNING) << "[ConsensusEngineBase has already been started]";
        return;
    }
    ENGINE_LOG(INFO) << "[Start ConsensusEngineBase]";
    /// start  a thread to execute doWork()&&workLoop()
    startWorking();
    m_startConsensusEngine = true;
}

void ConsensusEngineBase::stop()
{
    if (m_startConsensusEngine == false)
    {
        return;
    }
    ENGINE_LOG(INFO) << "[Stop ConsensusEngineBase]";
    m_startConsensusEngine = false;
    doneWorking();
    if (isWorking())
    {
        stopWorking();
        // will not restart worker, so terminate it
        terminate();
    }
}

/// update m_sealing and receiptRoot
dev::blockverifier::ExecutiveContext::Ptr ConsensusEngineBase::executeBlock(
    std::shared_ptr<Block> block)
{
    auto parentBlock = m_blockChain->getBlockByNumber(m_blockChain->number());
    BlockInfo parentBlockInfo{parentBlock->header().hash(), parentBlock->header().number(),
        parentBlock->header().stateRoot()};
    /// reset execute context
    return m_blockVerifier->executeBlock(block, parentBlockInfo);
}

void ConsensusEngineBase::checkBlockValid(Block const& block)
{
    h256 block_hash = block.blockHeader().hash();
    if (isSyncingHigherBlock(block.blockHeader().number()))
    {
        ENGINE_LOG(DEBUG) << LOG_DESC("checkBlockValid: isSyncingHigherBlock")
                          << LOG_KV("blkNumber", block.blockHeader().number())
                          << LOG_KV("highestBlk", m_blockSync->status().knownHighestNumber)
                          << LOG_KV("idx", nodeIdx());
        BOOST_THROW_EXCEPTION(SyncingHigherBlock() << errinfo_comment("isSyncingHigherBlock"));
    }
    /// check transaction num
    if (block.getTransactionSize() > maxBlockTransactions())
    {
        ENGINE_LOG(DEBUG) << LOG_DESC("checkBlockValid: overthreshold transaction num")
                          << LOG_KV("blockTransactionLimit", maxBlockTransactions())
                          << LOG_KV("blockTransNum", block.getTransactionSize());
        BOOST_THROW_EXCEPTION(
            OverThresTransNum() << errinfo_comment("overthreshold transaction num"));
    }

    /// check the timestamp
    if (block.blockHeader().timestamp() > utcTime() && !m_allowFutureBlocks)
    {
        ENGINE_LOG(DEBUG) << LOG_DESC("checkBlockValid: future timestamp")
                          << LOG_KV("timestamp", block.blockHeader().timestamp())
                          << LOG_KV("utcTime", utcTime()) << LOG_KV("hash", block_hash.abridged());
        BOOST_THROW_EXCEPTION(DisabledFutureTime() << errinfo_comment("Future time Disabled"));
    }
    /// check the block number
    if (block.blockHeader().number() <= m_blockChain->number())
    {
        ENGINE_LOG(DEBUG) << LOG_DESC("checkBlockValid: old height")
                          << LOG_KV("highNumber", m_blockChain->number())
                          << LOG_KV("blockNumber", block.blockHeader().number())
                          << LOG_KV("hash", block_hash.abridged());
        BOOST_THROW_EXCEPTION(InvalidBlockHeight() << errinfo_comment("Invalid block height"));
    }

    /// check the existence of the parent block (Must exist)
    if (!blockExists(block.blockHeader().parentHash()))
    {
        ENGINE_LOG(DEBUG) << LOG_DESC("checkBlockValid: Parent doesn't exist")
                          << LOG_KV("hash", block_hash.abridged());
        BOOST_THROW_EXCEPTION(ParentNoneExist() << errinfo_comment("Parent Block Doesn't Exist"));
    }
    if (block.blockHeader().number() > 1)
    {
        if (m_blockChain->numberHash(block.blockHeader().number() - 1) !=
            block.blockHeader().parentHash())
        {
            ENGINE_LOG(DEBUG)
                << LOG_DESC("checkBlockValid: Invalid block for unconsistent parentHash")
                << LOG_KV("block.parentHash", block.blockHeader().parentHash().abridged())
                << LOG_KV("parentHash",
                       m_blockChain->numberHash(block.blockHeader().number() - 1).abridged());
            BOOST_THROW_EXCEPTION(
                WrongParentHash() << errinfo_comment("Invalid block for unconsistent parentHash"));
        }
    }
}

void ConsensusEngineBase::updateConsensusNodeList()
{
    try
    {
        std::stringstream s2;
        s2 << "[updateConsensusNodeList] Sealers:";
        {
            WriteGuard l(m_sealerListMutex);
            m_sealerList = m_blockChain->sealerList();
            /// to make sure the index of all sealers are consistent
            std::sort(m_sealerList.begin(), m_sealerList.end());
            for (dev::h512 node : m_sealerList)
                s2 << node.abridged() << ",";
        }
        s2 << "Observers:";
        dev::h512s observerList = m_blockChain->observerList();
        for (dev::h512 node : observerList)
            s2 << node.abridged() << ",";
        ENGINE_LOG(TRACE) << s2.str();

        if (m_lastNodeList != s2.str())
        {
            ENGINE_LOG(TRACE) << "[updateConsensusNodeList] update P2P List done.";

            // get all nodes
            auto sealerList = m_blockChain->sealerList();
            dev::h512s nodeList = sealerList + observerList;
            std::sort(nodeList.begin(), nodeList.end());
            std::sort(sealerList.begin(), sealerList.end());
            // update the requiredInfo for treeTopologyRouter
            m_blockSync->updateNodeInfo(nodeList, sealerList);

            // update the p2p nodeList
            updateNodeListInP2P(nodeList);
            // update the cache
            m_lastNodeList = s2.str();
        }
    }
    catch (std::exception& e)
    {
        ENGINE_LOG(ERROR)
            << "[updateConsensusNodeList] update consensus node list failed [EINFO]:  "
            << boost::diagnostic_information(e);
    }
}

void ConsensusEngineBase::updateNodeListInP2P(dev::h512s const& nodeList)
{
    std::pair<GROUP_ID, MODULE_ID> ret = getGroupAndProtocol(m_protocolId);
    m_service->setNodeListByGroupID(ret.first, nodeList);
}

bool ConsensusEngineBase::getNodeIDByIndex(h512& nodeID, const IDXTYPE& idx) const
{
    nodeID = getSealerByIndex(idx);
    if (nodeID == h512())
    {
        ENGINE_LOG(ERROR) << LOG_DESC("getNodeIDByIndex: not sealer") << LOG_KV("Idx", idx)
                          << LOG_KV("myNode", m_keyPair.pub().abridged());
        return false;
    }
    return true;
}

void ConsensusEngineBase::resetConfig()
{
    updateMaxBlockTransactions();
    auto node_idx = MAXIDX;
    m_accountType = NodeAccountType::ObserverAccount;
    updateConsensusNodeList();
    {
        ReadGuard l(m_sealerListMutex);
        for (size_t i = 0; i < m_sealerList.size(); i++)
        {
            if (m_sealerList[i] == m_keyPair.pub())
            {
                m_accountType = NodeAccountType::SealerAccount;
                node_idx = i;
                break;
            }
        }
        m_nodeNum = m_sealerList.size();
    }
    if (m_nodeNum < 1)
    {
        ENGINE_LOG(ERROR) << LOG_DESC(
            "Must set at least one pbft sealer, current number of sealers is zero");
        raise(SIGTERM);
        BOOST_THROW_EXCEPTION(
            EmptySealers() << errinfo_comment("Must set at least one pbft sealer!"));
    }
    m_f = (m_nodeNum - 1) / 3;
    m_cfgErr = (node_idx == MAXIDX);
    m_idx = node_idx;
}

bool ConsensusEngineBase::checkSigList(
    std::vector<std::pair<IDXTYPE, Signature>> const& sigList, h256 const& blockHash)
{
    if (sigList.size() < minValidNodes())
    {
        ENGINE_LOG(WARNING) << LOG_DESC("checkSigList, not enough sigatures")
                            << LOG_KV("sigSize", sigList.size())
                            << LOG_KV("requiredSigSize", minValidNodes())
                            << LOG_KV("idx", nodeIdx());
        return false;
    }
    for (auto const& sig : sigList)
    {
        NodeID nodeId;
        if (!getNodeIDByIndex(nodeId, sig.first))
        {
            return false;
        }
        // check sign
        if (!dev::verify(nodeId, sig.second, blockHash))
        {
            return false;
        }
    }
    return true;
}

void ConsensusEngineBase::printNetworkInfo()
{
    uint64_t totalInPacketBytes = 0;
    {
        ReadGuard l(x_inInfo);
        for (auto const& it : m_inInfo)
        {
            totalInPacketBytes += it.second.second;
            ENGINE_LOG(DEBUG) << LOG_DESC("[Network statistic of Consensus Input]")
                              << LOG_KV("packetType", std::to_string(it.first))
                              << LOG_KV("packetCount", it.second.first)
                              << LOG_KV("packetSize", it.second.second);
        }
    }
    ENGINE_LOG(DEBUG) << LOG_DESC("[Network statistic of Consensus Input]")
                      << LOG_KV("totalInPacketBytes", totalInPacketBytes);
    uint64_t totalOutPacketBytes = 0;
    {
        ReadGuard l(x_outInfo);
        for (auto const& it : m_outInfo)
        {
            totalOutPacketBytes += it.second.second;
            ENGINE_LOG(DEBUG) << LOG_DESC("[Network statistic of Consensus Output]")
                              << LOG_KV("packetType", std::to_string(it.first))
                              << LOG_KV("packetCount", it.second.first)
                              << LOG_KV("packetSize", it.second.second);
        }
    }
    ENGINE_LOG(DEBUG) << LOG_DESC("[Network statistic of Consensus Output]")
                      << LOG_KV("totalOutPacketBytes", totalOutPacketBytes);
}

void ConsensusEngineBase::updateInNetworkInfo(uint8_t const& packetType, uint64_t const& length)
{
    WriteGuard l(x_inInfo);
    if (!m_inInfo.count(packetType))
    {
        m_inInfo[packetType] = std::make_pair(1, length);
    }
    else
    {
        m_inInfo[packetType].first++;
        m_inInfo[packetType].second += length;
    }
}

void ConsensusEngineBase::updateOutNetworkInfo(
    uint8_t const& packetType, uint64_t sessionSize, uint64_t const& length)
{
    WriteGuard l(x_outInfo);
    auto dataSize = sessionSize * length;
    if (!m_outInfo.count(packetType))
    {
        m_outInfo[packetType] = std::make_pair(sessionSize, dataSize);
    }
    else
    {
        m_outInfo[packetType].first += sessionSize;
        m_outInfo[packetType].second += dataSize;
    }
}


}  // namespace consensus
}  // namespace dev
