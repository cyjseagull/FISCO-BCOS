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
#include "HotStuffMsg.h"
#include "HotStuffMsgCacheFactory.h"
#include "HotStuffMsgFactory.h"
#include <libconsensus/ConsensusEngineBase.h>
#include <libconsensus/TimeManager.h>
#include <libdevcore/concurrent_queue.h>
#include <tbb/concurrent_queue.h>
namespace dev
{
namespace consensus
{
class HotStuffEngine : public ConsensusEngineBase
{
public:
    using Ptr = std::shared_ptr<HotStuffEngine>;
    HotStuffEngine(std::shared_ptr<dev::p2p::P2PInterface> _service,
        std::shared_ptr<dev::txpool::TxPoolInterface> _txPool,
        std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain,
        std::shared_ptr<dev::sync::SyncInterface> _blockSync,
        std::shared_ptr<dev::blockverifier::BlockVerifierInterface> _blockVerifier,
        dev::PROTOCOL_ID const& _protocolId, KeyPair const& _keyPair,
        h512s const& _sealerList = h512s())
      : ConsensusEngineBase(_service, _txPool, _blockChain, _blockSync, _blockVerifier, _protocolId,
            _keyPair, _sealerList)
    {
        // set thread name
        setName("HOTSTUFF[" + std::to_string(m_groupId));
        // bind the handler to solve hotstuff message
        m_service->registerHandlerByProtoclID(
            m_protocolId, boost::bind(&HotStuffEngine::onRecvHotStuffMessage, this, _1, _2, _3));
        // register consensusVerifyHandler
        m_blockSync->registerConsensusVerifyHandler([](dev::eth::Block const&) { return true; });
    }

    // start the hotstuff engine
    void start() override;
    virtual bool shouldSeal();

    // notify HotStuffSealer to generate prepare message
    void onNotifyGeneratePrepare(std::function<void(bool const&)> const& _f)
    {
        m_canGeneratePrepare = _f;
    }
    void reportBlock(dev::eth::Block const&) override;

    // get leader according to view
    virtual IDXTYPE getLeader() const { return getLeader(m_toView); }

    virtual bool reachBlockIntervalTime();
    virtual bool reachMinBlockGenTime();
    void generateAndBroadcastPrepare(std::shared_ptr<dev::eth::Block> block);

    // empty block
    virtual bool shouldReset(std::shared_ptr<dev::eth::Block> block)
    {
        return block->getTransactionSize() == 0 && m_omitEmptyBlock;
    }

protected:
    // reset view to the highest when receive valid preparePacket from the leader
    void resetView(VIEWTYPE const& view);

    // handle the received message
    void workLoop() override;

    // handler called when receive hotstuff message
    void onRecvHotStuffMessage(dev::p2p::NetworkException exception,
        std::shared_ptr<dev::p2p::P2PSession> session, dev::p2p::P2PMessage::Ptr message);

    virtual bool isValidNewViewMsg(HotStuffNewViewMsg::Ptr hotStuffMsg);
    virtual bool handleNewViewMsg(HotStuffNewViewMsg::Ptr newViewMsg);
    virtual void triggerGeneratePrepare();

    void collectGarbage();
    virtual bool isValidHotStuffMsg(HotStuffMsg::Ptr prepareMsg);
    /// prepare phase
    virtual bool isValidPrepareMsg(HotStuffPrepareMsg::Ptr prepareMsg);
    // handle prepare message received from the leader
    virtual bool handlePrepareMsg(HotStuffPrepareMsg::Ptr prepareMsg);
    // execute the block
    HotStuffPrepareMsg::Ptr execBlock(HotStuffPrepareMsg::Ptr rawPrepareMsg);

    virtual bool handlePrepareVoteMsg(HotStuffMsg::Ptr prepareMsg);
    virtual QuorumCert::Ptr checkAndGenerateQC(
        size_t const& cacheSize, HotStuffMsg::Ptr voteMsg, int const packetType);

    void checkAndGeneratePrepareQC(HotStuffMsg::Ptr prepareMsg);

    virtual bool isValidVoteMsg(HotStuffMsg::Ptr voteMsg);
    // check QC message
    virtual bool checkQCMsg(QuorumCert::Ptr QCMsg);

    virtual bool onReceiveQCMsg(QuorumCert::Ptr QCMsg, int const packetType);
    /// pre-commit phase
    // the replias receive the parepareQC Message, and
    // send pre-commit message to the leader
    virtual bool onReceivePrepareQCMsg(QuorumCert::Ptr prepareQC);
    // the leader collect the pre-commit message and
    // broadcast preCommitQC
    virtual bool handlePreCommitVoteMsg(HotStuffMsg::Ptr preCommitMsg);

    ///  commit phase
    // the replias receive the preCommitQC Message, and send
    // commit-vote message to the leader
    virtual bool onReceivePrecommitQCMsg(QuorumCert::Ptr preCommitQC);
    // the leader collect the commit-vote message, generate and broadcast
    // CommitQC
    virtual bool handleCommitVoteMsg(HotStuffMsg::Ptr commitMsg);

    /// decide phash
    // the replias receive the commitQC Message, send decide-vote message
    // to the leader,commit the block
    virtual bool onReceiveCommitQCMsg(QuorumCert::Ptr commitQC);

    // send NextView message to the new leader when timeout
    virtual void triggerNextView();

    virtual bool shouldSaveMessage(dev::p2p::P2PMessage::Ptr msg);

    // broadcast message(used by the leader)
    virtual bool broadCastMsg(HotStuffMsg::Ptr msg,
        std::function<bool(dev::network::NodeID const&)> const& filter = nullptr);

    virtual bool deliverMessage(HotStuffMsg::Ptr msg);

    // sendMessage(used by the replias)
    virtual bool sendMessage(HotStuffMsg::Ptr msg, dev::network::NodeID const& nodeId);
    virtual bool sendMessageToLeader(HotStuffMsg::Ptr msg);

    virtual bool tryToCommitBlock();

    // get leader according to view
    virtual IDXTYPE getLeader(VIEWTYPE const& view) const { return view % m_nodeNum; }

    void printHotStuffMsgInfo(
        HotStuffMsg::Ptr msg, std::string const& descMsg, LogLevel const& level = INFO);
    virtual void initHotStuff();
    virtual void handleMsg(dev::p2p::P2PMessage::Ptr p2pMessage);
    virtual void checkTimeout();

    // decode P2P message into the given message
    template <typename T>
    std::shared_ptr<T> decodeMessageBuffer(dev::p2p::P2PMessage::Ptr p2pMessage)
    {
        std::shared_ptr<T> decodedMsg = std::make_shared<T>();
        decodedMsg->decode(ref(*p2pMessage->buffer()));
        return decodedMsg;
    }

    virtual bool omitEmptyBlock(HotStuffPrepareMsg::Ptr prepareMsg);

    virtual void handleFuturePreparePacket();

    bool checkSign(HotStuffMsg::Ptr hotStuffReq) const;

    bool broadcastFilter(dev::network::NodeID const& nodeId, int packetType, std::string const& key)
    {
        if (!m_broadcastCache.count(nodeId))
        {
            return false;
        }
        if (!m_broadcastCache[nodeId].count(packetType))
        {
            return false;
        }
        return m_broadcastCache[nodeId][packetType].count(key);
    }
    void broadcastMark(dev::network::NodeID const& nodeId, int packetType, std::string const& key)
    {
        if (!m_broadcastCache.count(nodeId))
        {
            BroadcastCache cache;
            m_broadcastCache[nodeId] = cache;
        }
        if (!m_broadcastCache[nodeId].count(packetType))
        {
            std::unordered_set<std::string> cache;
            m_broadcastCache[nodeId][packetType] = cache;
        }
        m_broadcastCache[nodeId][packetType].insert(key);
    }

protected:
    // the highest blockHeader
    dev::eth::BlockHeader m_highestBlockHeader;

    // current view
    std::atomic<VIEWTYPE> m_view = {0};
    std::atomic<VIEWTYPE> m_toView = {0};
    QuorumCert::Ptr m_justifyQC = nullptr;

    std::atomic<int64_t> m_consensusBlockNumber = {0};
    // commit empty block or not
    std::atomic_bool m_omitEmptyBlock = {true};

    dev::concurrent_queue<dev::p2p::P2PMessage::Ptr> m_hotstuffMsgQueue;
    std::condition_variable m_signalled;
    Mutex x_signalled;

    // factory used to create HotStuffMsg
    HotStuffMsgFactory::Ptr m_hotStuffMsgFactory;
    // factory used to create HotStuffMsgCache
    HotStuffMsgCacheFactory::Ptr m_hotStuffMsgCacheFactory;

    // HotStuffMsg cache
    HotStuffMsgCache::Ptr m_hotStuffMsgCache;
    TimeManager::Ptr m_timeManager;

    mutable Mutex m_mutex;

    // handlers
    std::function<void(bool const&)> m_canGeneratePrepare;
    static const unsigned c_PopWaitSeconds = 5;

    using BroadcastCache = std::unordered_map<int, std::unordered_set<std::string>>;
    std::unordered_map<dev::network::NodeID, BroadcastCache> m_broadcastCache;
};
}  // namespace consensus
}  // namespace dev