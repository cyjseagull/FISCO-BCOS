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
 * @brief : Sync implementation
 * @author: jimmyshi
 * @date: 2018-10-15
 */

#pragma once
#include "Common.h"
#include "DownloadingTxsQueue.h"
#include "RspBlockReq.h"
#include "SyncInterface.h"
#include "SyncMsgEngine.h"
#include "SyncStatus.h"
#include "SyncTransaction.h"
#include "SyncTreeTopology.h"
#include <libblockchain/BlockChainInterface.h>
#include <libblockverifier/BlockVerifierInterface.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/Worker.h>
#include <libethcore/Common.h>
#include <libethcore/Exceptions.h>
#include <libnetwork/Common.h>
#include <libnetwork/Session.h>
#include <libp2p/P2PInterface.h>
#include <libtxpool/TxPoolInterface.h>
#include <vector>


namespace dev
{
namespace sync
{
class SyncMaster : public SyncInterface, public Worker
{
public:
    SyncMaster(std::shared_ptr<dev::p2p::P2PInterface> _service,
        std::shared_ptr<dev::txpool::TxPoolInterface> _txPool,
        std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain,
        std::shared_ptr<dev::blockverifier::BlockVerifierInterface> _blockVerifier,
        PROTOCOL_ID const& _protocolId, NodeID const& _nodeId, h256 const& _genesisHash,
        unsigned _idleWaitMs = 200)
      : SyncInterface(),
        Worker("Sync-" + std::to_string(_protocolId), _idleWaitMs),
        m_service(_service),
        m_txPool(_txPool),
        m_blockChain(_blockChain),
        m_blockVerifier(_blockVerifier),
        m_txQueue(std::make_shared<DownloadingTxsQueue>(_protocolId, _nodeId)),
        m_protocolId(_protocolId),
        m_groupId(dev::eth::getGroupAndProtocol(_protocolId).first),
        m_nodeId(_nodeId),
        m_genesisHash(_genesisHash)
    {
        m_syncStatus =
            std::make_shared<SyncMasterStatus>(_blockChain, _protocolId, _genesisHash, _nodeId);
        m_msgEngine = std::make_shared<SyncMsgEngine>(_service, _txPool, _blockChain, m_syncStatus,
            m_txQueue, _protocolId, _nodeId, _genesisHash);
        m_msgEngine->onNotifyWorker([&]() { m_signalled.notify_all(); });

        // signal registration
        m_blockSubmitted = m_blockChain->onReady([&](int64_t) { this->noteNewBlocks(); });

        /// set thread name
        std::string threadName = "Sync-" + std::to_string(m_groupId);
        setName(threadName);
        m_syncTrans = std::make_shared<SyncTransaction>(
            _service, _txPool, m_txQueue, _protocolId, _nodeId, m_syncStatus);

        m_syncTreeRouter = std::make_shared<SyncTreeTopology>(_nodeId);
        // update the nodeInfo for syncTreeRouter
        updateNodeInfo();
    }

    virtual ~SyncMaster() { stop(); };
    /// start blockSync
    virtual void start() override;
    /// stop blockSync
    virtual void stop() override;
    /// doWork every idleWaitMs
    virtual void doWork() override;
    virtual void workLoop() override;

    /// get status of block sync
    /// @returns Synchonization status
    virtual SyncStatus status() const override;
    virtual std::string const syncInfo() const override;
    virtual void noteSealingBlockNumber(int64_t _number) override;
    virtual bool isSyncing() const override;
    // is my number is far smaller than max block number of this block chain
    bool isFarSyncing() const override;
    /// protocol id used when register handler to p2p module
    virtual PROTOCOL_ID const& protocolId() const override { return m_protocolId; };
    virtual void setProtocolId(PROTOCOL_ID const _protocolId) override
    {
        m_protocolId = _protocolId;
        m_groupId = dev::eth::getGroupAndProtocol(m_protocolId).first;
    };

    virtual void registerConsensusVerifyHandler(
        std::function<bool(dev::eth::Block const&)> _handler) override
    {
        fp_isConsensusOk = _handler;
    };

    void registerNodeIdFilterHandler(
        std::function<dev::p2p::NodeIDs(std::set<NodeID> const&)> _handler) override
    {
        m_syncTrans->registerNodeIdFilterHandler(_handler);
    }

    void noteNewBlocks()
    {
        m_newBlocks = true;
        m_signalled.notify_all();  // awake doWork
    }

    void noteDownloadingBegin()
    {
        if (m_syncStatus->state == SyncState::Idle)
            m_syncStatus->state = SyncState::Downloading;
    }

    void noteDownloadingFinish()
    {
        if (m_syncStatus->state == SyncState::Downloading)
            m_syncStatus->state = SyncState::Idle;
    }

    int64_t protocolId() { return m_protocolId; }

    NodeID nodeId() { return m_nodeId; }

    h256 genesisHash() { return m_genesisHash; }

    std::shared_ptr<SyncMasterStatus> syncStatus() { return m_syncStatus; }

    std::shared_ptr<SyncMsgEngine> msgEngine() { return m_msgEngine; }

    void maintainTransactions() { m_syncTrans->maintainTransactions(); }
    void maintainDownloadingTransactions() { m_syncTrans->maintainDownloadingTransactions(); }

    void printNetworkStatisticInfo() override
    {
        SYNC_LOG(DEBUG) << LOG_DESC("CONS: printSendedBlockInfo")
                        << LOG_KV("sendedBlockCount", m_sendedBlockCount)
                        << LOG_KV("sendedBlockBytes", m_sendedBlockBytes);
        m_syncTrans->printSendedTxsInfo();
        m_txQueue->printDownloadingTxsInfo();
        m_syncStatus->bq().printDownloadingBlockInfo();
    }

    void sendSyncStatusByTree(
        dev::eth::BlockNumber const& blockNumber, dev::h256 const& currentHash);

    void broadcastSyncStatus(
        dev::eth::BlockNumber const& blockNumber, dev::h256 const& currentHash);

    void gossipSyncStatus();

    bool sendSyncStatusByNodeId(dev::eth::BlockNumber const& blockNumber,
        dev::h256 const& currentHash, dev::network::NodeID const& nodeId);

    void updateNodeListInfo(dev::h512s const& _nodeList) override
    {
        m_syncTreeRouter->updateNodeListInfo(_nodeList);
    }

    void updateConsensusNodeInfo(dev::h512s const& _consensusNodes) override
    {
        m_syncTreeRouter->updateConsensusNodeInfo(_consensusNodes);
    }

    // init via blockchain when the sync thread started
    void updateNodeInfo()
    {
        auto sealerList = m_blockChain->sealerList();
        std::sort(sealerList.begin(), sealerList.end());
        auto observerList = m_blockChain->observerList();
        auto nodeList = sealerList + observerList;
        std::sort(nodeList.begin(), nodeList.end());
        updateNodeListInfo(nodeList);
        updateConsensusNodeInfo(sealerList);
    }

    void setEachBlockDownloadingRequestTimeout(int64_t const& _eachBlockDownloadingRequestTimeout)
    {
        m_eachBlockDownloadingRequestTimeout = _eachBlockDownloadingRequestTimeout;
    }

protected:
    virtual void startGossipThread();
    virtual void stopGossipThread();

private:
    /// p2p service handler
    std::shared_ptr<dev::p2p::P2PInterface> m_service;
    /// transaction pool handler
    std::shared_ptr<dev::txpool::TxPoolInterface> m_txPool;
    /// handler of the block chain module
    std::shared_ptr<dev::blockchain::BlockChainInterface> m_blockChain;
    /// block verifier
    std::shared_ptr<dev::blockverifier::BlockVerifierInterface> m_blockVerifier;

    /// Downloading txs queue
    std::shared_ptr<DownloadingTxsQueue> m_txQueue;

    /// Block queue and peers
    std::shared_ptr<SyncMasterStatus> m_syncStatus;

    /// Message handler of p2p
    std::shared_ptr<SyncMsgEngine> m_msgEngine;

    // Internal data
    PROTOCOL_ID m_protocolId;
    GROUP_ID m_groupId;
    NodeID m_nodeId;  ///< Nodeid of this node
    h256 m_genesisHash;

    int64_t m_maxRequestNumber = 0;
    uint64_t m_lastDownloadingRequestTime = 0;
    int64_t m_lastDownloadingBlockNumber = 0;
    int64_t m_currentSealingNumber = 0;
    int64_t m_eachBlockDownloadingRequestTimeout = 1000;

    // Internal coding variable
    /// mutex to access m_signalled
    Mutex x_signalled;
    /// mutex to protect m_currentSealingNumber
    mutable SharedMutex x_currentSealingNumber;

    /// signal to notify all thread to work
    std::condition_variable m_signalled;

    // sync state
    std::atomic_bool m_newBlocks = {false};
    uint64_t m_maintainBlocksTimeout = 0;
    bool m_needSendStatus = true;

    // sync transactions
    SyncTransaction::Ptr m_syncTrans = nullptr;

    // handler for find the tree router
    SyncTreeTopology::Ptr m_syncTreeRouter = nullptr;

    // maintain peer connections
    std::shared_ptr<std::thread> m_gossipSyncStatusThread;
    std::atomic_bool m_running = {false};
    std::atomic<int64_t> m_gossipInterval = {3000};
    std::int64_t m_gossipPeersNumber = 2;

    // settings
    dev::eth::Handler<int64_t> m_blockSubmitted;

    // verify handler to check downloading block
    std::function<bool(dev::eth::Block const&)> fp_isConsensusOk = nullptr;

    std::atomic<uint64_t> m_sendedBlockCount = {0};
    std::atomic<uint64_t> m_sendedBlockBytes = {0};


public:
    void maintainBlocks();
    void maintainPeersStatus();
    bool maintainDownloadingQueue();  /// return true if downloading finish
    void maintainDownloadingQueueBuffer();
    void maintainPeersConnection();
    void maintainBlockRequest();

private:
    bool isNextBlock(BlockPtr _block);
    void printSyncInfo();
};

}  // namespace sync
}  // namespace dev
