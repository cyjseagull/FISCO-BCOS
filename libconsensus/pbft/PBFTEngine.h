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
 * @file: PBFTEngine.h
 * @author: yujiechen
 * @date: 2018-09-28
 */
#pragma once
#include "Common.h"
#include "PBFTBroadcastCache.h"
#include "PBFTMsgFactory.h"
#include "PBFTReqCache.h"
#include "PBFTReqFactory.h"
#include <libconsensus/ConsensusEngineBase.h>
#include <libconsensus/TimeManager.h>
#include <libdevcore/FileSystem.h>
#include <libdevcore/LevelDB.h>
#include <libdevcore/concurrent_queue.h>
#include <libstorage/Storage.h>
#include <libsync/SyncStatus.h>
#include <sstream>

#include <libp2p/P2PMessageFactory.h>
#include <libp2p/P2PSession.h>
#include <libp2p/Service.h>

namespace dev
{
namespace consensus
{
enum CheckResult
{
    VALID = 0,
    INVALID = 1,
    FUTURE = 2
};
using PBFTMsgQueue = dev::concurrent_queue<PBFTMsgPacket>;
class PBFTEngine : public ConsensusEngineBase
{
public:
    virtual ~PBFTEngine() { stop(); }
    PBFTEngine(std::shared_ptr<dev::p2p::P2PInterface> _service,
        std::shared_ptr<dev::txpool::TxPoolInterface> _txPool,
        std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain,
        std::shared_ptr<dev::sync::SyncInterface> _blockSync,
        std::shared_ptr<dev::blockverifier::BlockVerifierInterface> _blockVerifier,
        dev::PROTOCOL_ID const& _protocolId, KeyPair const& _keyPair,
        h512s const& _sealerList = h512s())
      : ConsensusEngineBase(_service, _txPool, _blockChain, _blockSync, _blockVerifier, _protocolId,
            _keyPair, _sealerList)
    {
        PBFTENGINE_LOG(INFO) << LOG_DESC("Register handler for PBFTEngine");
        m_service->registerHandlerByProtoclID(
            m_protocolId, boost::bind(&PBFTEngine::onRecvPBFTMessage, this, _1, _2, _3));

        /// set thread name for PBFTEngine
        std::string threadName = "PBFT-" + std::to_string(m_groupId);
        setName(threadName);

        /// register checkSealerList to blockSync for check SealerList
        m_blockSync->registerConsensusVerifyHandler(boost::bind(&PBFTEngine::checkBlock, this, _1));
        m_broadcastFilter = boost::bind(&PBFTEngine::getIndexBySealer, this, _1);
    }
    void broadcastPrepareByTree(std::shared_ptr<PrepareReq> prepareReq);
    void setPBFTReqFactory(std::shared_ptr<PBFTReqFactory> pbftReqFactory)
    {
        m_pbftReqFactory = pbftReqFactory;
    }

    void setPBFTMsgFactory(std::shared_ptr<PBFTMsgFactoryInterface> pbftMsgFactory)
    {
        m_pbftMsgFactory = pbftMsgFactory;
    }

    void setBaseDir(std::string const& _path) { m_baseDir = _path; }

    std::string const& getBaseDir() { return m_baseDir; }

    /// set max block generation time
    inline void setEmptyBlockGenTime(unsigned const& _intervalBlockTime)
    {
        m_timeManager.m_emptyBlockGenTime = _intervalBlockTime;
    }

    /// get max block generation time
    inline unsigned const& getEmptyBlockGenTime() const
    {
        return m_timeManager.m_emptyBlockGenTime;
    }

    /// set mininum block generation time
    void setMinBlockGenerationTime(unsigned const& time)
    {
        if (time < m_timeManager.m_emptyBlockGenTime)
        {
            m_timeManager.m_minBlockGenTime = time;
        }
        PBFTENGINE_LOG(INFO) << LOG_KV("minBlockGenerationTime", m_timeManager.m_minBlockGenTime);
    }

    void start() override;
    virtual void initPBFTCacheObject();

    /// reach the minimum block generation time
    virtual bool reachMinBlockGenTime()
    {
        /// since canHandleBlockForNextLeader has enforced the  next leader sealed block can't be
        /// handled before the current leader generate a new block, it's no need to add other
        /// conditions to enforce this striction
        return (utcTime() - m_timeManager.m_lastConsensusTime) >= m_timeManager.m_minBlockGenTime;
    }

    virtual bool reachBlockIntervalTime()
    {
        /// since canHandleBlockForNextLeader has enforced the  next leader sealed block can't be
        /// handled before the current leader generate a new block, the conditions before can be
        /// deleted
        return (utcTime() - m_timeManager.m_lastConsensusTime) >= m_timeManager.m_emptyBlockGenTime;
    }

    virtual bool shouldPushMsg(byte const& packetType)
    {
        return (packetType <= PBFTPacketType::ViewChangeReqPacket);
    }

    /// in case of the next leader packeted the number of maxTransNum transactions before the last
    /// block is consensused
    /// when sealing for the next leader,  return true only if the last block has been consensused
    /// even if the maxTransNum condition has been meeted
    bool canHandleBlockForNextLeader()
    {
        /// get leader failed
        if (false == getLeader().first)
        {
            return false;
        }
        /// the case that only a node is both the leader and the next leader
        if (getLeader().second == nodeIdx())
        {
            return true;
        }
        if (m_notifyNextLeaderSeal && getNextLeader() == nodeIdx())
        {
            return false;
        }
        return true;
    }
    void rehandleCommitedPrepareCache(std::shared_ptr<PrepareReq> req);
    virtual bool shouldSeal();
    /// broadcast prepare message
    virtual bool generatePrepare(std::shared_ptr<dev::eth::Block> const& block);
    virtual bool triggerViewChangeForEmptyBlock(std::shared_ptr<PrepareReq> prepareReq);

    virtual void updateConsensusStatus();

    /// update the context of PBFT after commit a block into the block-chain
    void reportBlock(dev::eth::Block const& block) override;
    void onViewChange(std::function<void()> const& _f)
    {
        m_onViewChange = _f;
        m_notifyNextLeaderSeal = false;
    }
    void onNotifyNextLeaderReset(std::function<void(dev::h256Hash const& filter)> const& _f)
    {
        m_onNotifyNextLeaderReset = _f;
    }

    void onTimeout(std::function<void(uint64_t const& sealingTxNumber)> const& _f)
    {
        m_onTimeout = _f;
    }

    void onCommitBlock(std::function<void(uint64_t const& blockNumber,
            uint64_t const& sealingTxNumber, unsigned const& changeCycle)> const& _f)
    {
        m_onCommitBlock = _f;
    }

    // notify sealing to reset config after generate empty block(for GroupPBFTEngine, maybe used for
    // PBFTEngine in the future)
    void onEmptyBlockChanged(std::function<void()> const& _f) { m_emptyBlockGenerated = _f; }

    virtual bool shouldReset(std::shared_ptr<dev::eth::Block> const& block)
    {
        return block->getTransactionSize() == 0 && m_omitEmptyBlock;
    }
    const std::string consensusStatus() override;
    void setOmitEmptyBlock(bool setter) { m_omitEmptyBlock = setter; }

    void setMaxTTL(uint8_t const& ttl) { maxTTL = ttl; }

    virtual IDXTYPE getNextLeader() const { return (m_highestBlock.number() + 1) % m_nodeNum; }

    virtual std::pair<bool, IDXTYPE> getLeader() const
    {
        if (m_cfgErr || m_leaderFailed || m_highestBlock.sealer() == Invalid256 || m_nodeNum == 0)
        {
            return std::make_pair(false, MAXIDX);
        }
        return std::make_pair(true, (m_view + m_highestBlock.number()) % m_nodeNum);
    }

    uint64_t sealingTxNumber() const { return m_sealingNumber; }
    virtual bool shouldReportBlock(dev::eth::Block const& block) const;

protected:
    void reportBlockWithoutLock(dev::eth::Block const& block);
    void workLoop() override;
    void handleFutureBlock();
    void collectGarbage();
    virtual void checkTimeout();
    inline void checkBlockValid(dev::eth::Block const& block) override
    {
        ConsensusEngineBase::checkBlockValid(block);
        checkSealerList(block);
    }

    virtual bool fastViewChangeViewForEmptyBlock(std::shared_ptr<Sealing> sealing);

    void getAllNodesViewStatus(Json::Value& status);

    /// broadcast specified message to all-peers with cache-filter and specified filter
    virtual bool broadcastMsg(unsigned const& packetType, std::string const& key,
        bytesConstRef data, std::unordered_set<dev::network::NodeID> const& filter,
        unsigned const& ttl,
        std::function<ssize_t(dev::network::NodeID const&)> const& filterFunction);

    virtual bool broadcastMsg(unsigned const& packetType, std::string const& key,
        bytesConstRef data,
        std::unordered_set<dev::network::NodeID> const& filter =
            std::unordered_set<dev::network::NodeID>(),
        unsigned const& ttl = 0)
    {
        return broadcastMsg(packetType, key, data, filter, ttl, m_broadcastFilter);
    }

    void sendViewChangeMsg(dev::network::NodeID const& nodeId);
    bool sendMsg(dev::network::NodeID const& nodeId, unsigned const& packetType,
        std::string const& key, bytesConstRef data, unsigned const& ttl = 1);
    /// 1. generate and broadcast signReq according to given prepareReq
    /// 2. add the generated signReq into the cache
    bool broadcastSignReq(std::shared_ptr<PrepareReq> req);

    /// broadcast commit message
    bool broadcastCommitReq(PrepareReq const& req);
    /// broadcast view change message
    bool shouldBroadcastViewChange();
    bool broadcastViewChangeReq();
    /// handler called when receiving data from the network
    void onRecvPBFTMessage(dev::p2p::NetworkException exception,
        std::shared_ptr<dev::p2p::P2PSession> session, dev::p2p::P2PMessage::Ptr message);

    // handle prepare
    virtual bool handlePrepareMsg(
        std::shared_ptr<PrepareReq> prepare_req, std::string const& endpoint = "self");


    /// handler prepare messages
    bool handlePrepareMsg(std::shared_ptr<PrepareReq> prepareReq, PBFTMsgPacket const& pbftMsg);
    /// 1. decode the network-received PBFTMsgPacket to signReq
    /// 2. check the validation of the signReq
    /// add the signReq to the cache and
    /// heck the size of the collected signReq is over 2/3 or not
    bool handleSignMsg(std::shared_ptr<SignReq> signReq, PBFTMsgPacket const& pbftMsg);

    bool handleCommitMsg(std::shared_ptr<CommitReq> commitReq, PBFTMsgPacket const& pbftMsg);
    bool handleViewChangeMsg(
        std::shared_ptr<ViewChangeReq> viewChangeReq, PBFTMsgPacket const& pbftMsg);
    void handleMsg(PBFTMsgPacket const& pbftMsg);
    virtual std::shared_ptr<PBFTMsg> handleMsg(std::string& key, PBFTMsgPacket const& pbftMsg);

    void catchupView(std::shared_ptr<ViewChangeReq> req, std::ostringstream& oss);
    virtual void checkAndCommit();

    /// if collect >= 2/3 SignReq and CommitReq, then callback this function to commit block
    virtual void checkAndSave();
    virtual void checkAndChangeView();
    virtual void changeView();

    virtual bool checkAndCommitBlock(size_t const& commitSize);
    virtual bool locatedInConsensusList() const { return m_idx != MAXIDX; }

    void wait()
    {
        std::unique_lock<std::mutex> l(x_signalled);
        m_signalled.wait_for(l, std::chrono::milliseconds(5));
    }

protected:
    // update basic status
    void updateBasicStatus();

    void initPBFTEnv(unsigned _view_timeout);
    virtual void initBackupDB();
    void reloadMsg(std::string const& _key, std::shared_ptr<PBFTMsg> _msg);
    void backupMsg(std::string const& _key, std::shared_ptr<PBFTMsg> _msg);
    inline std::string getBackupMsgPath() { return m_baseDir + "/" + c_backupMsgDirName; }

    bool checkSign(std::shared_ptr<PBFTMsg> req) const;
    inline bool broadcastFilter(
        dev::network::NodeID const& nodeId, unsigned const& packetType, std::string const& key)
    {
        return m_broadCastCache->keyExists(nodeId, packetType, key);
    }

    /**
     * @brief: insert specified key into the cache of broadcast
     *         used to filter the broadcasted message(in case of too-many repeated broadcast
     * messages)
     * @param nodeId: the node id of the message broadcasted to
     * @param packetType: the packet type of the broadcast-message
     * @param key: the key of the broadcast-message, is the signature of the broadcast-message in
     * common
     */
    inline void broadcastMark(
        dev::network::NodeID const& nodeId, unsigned const& packetType, std::string const& key)
    {
        /// in case of useless insert
        if (m_broadCastCache->keyExists(nodeId, packetType, key))
            return;
        m_broadCastCache->insertKey(nodeId, packetType, key);
    }
    inline void clearMask() { m_broadCastCache->clearAll(); }
    /// trans data into message
    inline dev::p2p::P2PMessage::Ptr transDataToMessage(bytesConstRef data,
        PACKET_TYPE const& packetType, PROTOCOL_ID const& protocolId, unsigned const& ttl)
    {
        dev::p2p::P2PMessage::Ptr message = std::dynamic_pointer_cast<dev::p2p::P2PMessage>(
            m_service->p2pMessageFactory()->buildMessage());
        // std::shared_ptr<dev::bytes> p_data = std::make_shared<dev::bytes>();
        bytes ret_data;
        PBFTMsgPacket packet;
        packet.data = data.toBytes();
        packet.packet_id = packetType;
        if (ttl == 0)
            packet.ttl = maxTTL;
        else
            packet.ttl = ttl;
        packet.encode(ret_data);
        std::shared_ptr<dev::bytes> p_data = std::make_shared<dev::bytes>(std::move(ret_data));
        message->setBuffer(p_data);
        message->setProtocolID(protocolId);
        return message;
    }

    inline dev::p2p::P2PMessage::Ptr transDataToMessage(
        bytesConstRef data, PACKET_TYPE const& packetType, unsigned const& ttl)
    {
        return transDataToMessage(data, packetType, m_protocolId, ttl);
    }

    /// check the specified prepareReq is valid or not
    virtual CheckResult isValidPrepare(std::shared_ptr<PrepareReq> req, std::ostringstream& oss);

    template <class T>
    inline CheckResult checkBasic(
        std::shared_ptr<T> req, std::ostringstream& oss, bool const& needCheckSign) const
    {
        if (isSyncingHigherBlock(req))
        {
            PBFTENGINE_LOG(DEBUG) << LOG_DESC("checkReq: Is Syncing higher number")
                                  << LOG_KV("ReqNumber", req->height)
                                  << LOG_KV(
                                         "syncingNumber", m_blockSync->status().knownHighestNumber)
                                  << LOG_KV("INFO", oss.str());
            return CheckResult::INVALID;
        }

        if (m_reqCache->prepareCache()->block_hash != req->block_hash)
        {
            PBFTENGINE_LOG(TRACE) << LOG_DESC("checkReq: sign or commit Not exist in prepare cache")
                                  << LOG_KV("prepHash",
                                         m_reqCache->prepareCache()->block_hash.abridged())
                                  << LOG_KV("hash", req->block_hash.abridged())
                                  << LOG_KV("INFO", oss.str());
            /// is future ?
            bool is_future = isFutureBlock(req);
            bool signValid = true;
            if (needCheckSign)
            {
                signValid = checkSign(req);
            }
            if (is_future && signValid)
            {
                PBFTENGINE_LOG(INFO)
                    << LOG_DESC("checkReq: Recv future request")
                    << LOG_KV("prepHash", m_reqCache->prepareCache()->block_hash.abridged())
                    << LOG_KV("INFO", oss.str());
                return CheckResult::FUTURE;
            }
            return CheckResult::INVALID;
        }
        return CheckResult::VALID;
    }
    /**
     * @brief: common check process when handle SignReq and CommitReq
     *         1. the request should be existed in prepare cache,
     *            if the request is the future request, should add it to the prepare cache
     *         2. the sealer of the request shouldn't be the node-self
     *         3. the view of the request must be equal to the view of the prepare cache
     *         4. the signature of the request must be valid
     * @tparam T: the type of the request
     * @param req: the request should be checked
     * @param oss: information to debug
     * @return CheckResult:
     *  1. CheckResult::FUTURE: the request is the future req;
     *  2. CheckResult::INVALID: the request is invalid
     *  3. CheckResult::VALID: the request is valid
     */
    template <class T>
    inline CheckResult checkReq(
        std::shared_ptr<T> req, std::ostringstream& oss, bool const& needCheckSign = true) const
    {
        CheckResult result = checkBasic(req, oss, needCheckSign);
        if (result == CheckResult::FUTURE)
        {
            return CheckResult::FUTURE;
        }
        if (result == CheckResult::INVALID)
        {
            return CheckResult::INVALID;
        }
        /// check the sealer of this request
        if (req->idx == nodeIdx())
        {
            PBFTENGINE_LOG(TRACE) << LOG_DESC("checkReq: Recv own req")
                                  << LOG_KV("INFO", oss.str());
            return CheckResult::INVALID;
        }
        /// check view
        if (m_reqCache->prepareCache()->view != req->view)
        {
            PBFTENGINE_LOG(TRACE) << LOG_DESC("checkReq: Recv req with unconsistent view")
                                  << LOG_KV("prepView", m_reqCache->prepareCache()->view)
                                  << LOG_KV("view", req->view) << LOG_KV("INFO", oss.str());
            return CheckResult::INVALID;
        }
        if (!checkSign(req))
        {
            PBFTENGINE_LOG(TRACE) << LOG_DESC("checkReq:  invalid sign")
                                  << LOG_KV("INFO", oss.str());
            return CheckResult::INVALID;
        }
        return result;
    }


    CheckResult isValidSignReq(std::shared_ptr<SignReq> req, std::ostringstream& oss) const;
    CheckResult isValidCommitReq(std::shared_ptr<CommitReq> req, std::ostringstream& oss) const;
    bool isValidViewChangeReq(
        std::shared_ptr<ViewChangeReq> req, IDXTYPE const& source, std::ostringstream& oss);

    template <class T>
    inline bool hasConsensused(std::shared_ptr<T> req) const
    {
        if (req->height < m_consensusBlockNumber ||
            (req->height == m_consensusBlockNumber && req->view < m_view))
        {
            return true;
        }
        return false;
    }

    /// in case of con-current execution of block
    template <class T>
    inline bool isSyncingHigherBlock(std::shared_ptr<T> req) const
    {
        if (m_blockSync->isSyncing() && req->height <= m_blockSync->status().knownHighestNumber)
        {
            return true;
        }
        return false;
    }
    /**
     * @brief : decide the sign or commit request is the future request or not
     *          1. the block number is no smalller than the current consensused block number
     *          2. or the view is no smaller than the current consensused block number
     */
    template <typename T>
    inline bool isFutureBlock(std::shared_ptr<T> req) const
    {
        /// to ensure that the signReq can reach to consensus even if the view has been changed
        if (req->height >= m_consensusBlockNumber || req->view > m_view)
        {
            return true;
        }
        return false;
    }

    template <typename T>
    inline bool isFuturePrepare(std::shared_ptr<T> req) const
    {
        if (req->height > m_consensusBlockNumber ||
            (req->height == m_consensusBlockNumber && req->view > m_view))
        {
            return true;
        }
        return false;
    }

    inline bool isHashSavedAfterCommit(std::shared_ptr<PrepareReq> const& req) const
    {
        if (req->height == m_reqCache->committedPrepareCache()->height &&
            req->block_hash != m_reqCache->committedPrepareCache()->block_hash)
        {
            /// TODO: remove these logs in the atomic functions
            PBFTENGINE_LOG(DEBUG)
                << LOG_DESC("isHashSavedAfterCommit: hasn't been cached after commit")
                << LOG_KV("height", req->height)
                << LOG_KV("cacheHeight", m_reqCache->committedPrepareCache()->height)
                << LOG_KV("hash", req->block_hash.abridged())
                << LOG_KV("cacheHash", m_reqCache->committedPrepareCache()->block_hash.abridged())
                << LOG_KV("currentLeader", getLeader().second) << LOG_KV("idx", nodeIdx());
            return false;
        }
        return true;
    }

    virtual bool isValidLeader(std::shared_ptr<PrepareReq> const& req) const
    {
        auto leader = getLeader();
        /// get leader failed or this prepareReq is not broadcasted from leader
        if (!leader.first || req->idx != leader.second)
        {
            return false;
        }

        return true;
    }

    void checkSealerList(dev::eth::Block const& block);
    /// check block
    virtual bool checkBlock(dev::eth::Block const& block);
    void execBlock(
        std::shared_ptr<Sealing> sealing, std::shared_ptr<PrepareReq> req, std::ostringstream& oss);
    virtual void changeViewForFastViewChange()
    {
        m_timeManager.changeView();
        m_fastViewChange = true;
        m_signalled.notify_all();
    }
    void notifySealing(std::shared_ptr<dev::eth::Block> block);
    /// to ensure at least 100MB available disk space
    virtual bool isDiskSpaceEnough(std::string const& path)
    {
        return boost::filesystem::space(path).available > 1024 * 1024 * 100;
    }

    void updateViewMap(IDXTYPE const& idx, VIEWTYPE const& view)
    {
        WriteGuard l(x_viewMap);
        m_viewMap[idx] = view;
    }

    template <typename T, typename S>
    bool filterSource(std::shared_ptr<T> req, S const& pbftMsg)
    {
        dev::network::NodeID genNodeId;
        broadcastMark(pbftMsg.node_id, pbftMsg.packet_id, req->uniqueKey());
        if (!getNodeIDByIndex(genNodeId, req->idx))
        {
            return false;
        }
        broadcastMark(genNodeId, pbftMsg.packet_id, req->uniqueKey());
        return true;
    }

protected:
    VIEWTYPE m_view = 0;
    VIEWTYPE m_toView = 0;
    std::string m_baseDir;
    std::atomic_bool m_leaderFailed = {false};
    std::atomic_bool m_notifyNextLeaderSeal = {false};

    // backup msg
    std::shared_ptr<dev::db::LevelDB> m_backupDB = nullptr;

    /// static vars
    static const std::string c_backupKeyCommitted;
    static const std::string c_backupMsgDirName;
    static const unsigned c_PopWaitSeconds = 5;

    std::shared_ptr<PBFTBroadcastCache> m_broadCastCache;
    std::shared_ptr<PBFTReqCache> m_reqCache = nullptr;

    std::shared_ptr<PBFTReqFactory> m_pbftReqFactory = nullptr;
    std::shared_ptr<PBFTMsgFactoryInterface> m_pbftMsgFactory = nullptr;

    TimeManager m_timeManager;
    PBFTMsgQueue m_msgQueue;
    mutable Mutex m_mutex;

    std::condition_variable m_signalled;
    Mutex x_signalled;


    std::function<void()> m_onViewChange = nullptr;
    std::function<void(dev::h256Hash const& filter)> m_onNotifyNextLeaderReset = nullptr;
    std::function<void(uint64_t const& sealingTxNumber)> m_onTimeout = nullptr;
    std::function<void(
        uint64_t const& blockNumber, uint64_t const& sealingTxNumber, unsigned const& changeCycle)>
        m_onCommitBlock = nullptr;

    std::function<ssize_t(dev::network::NodeID const&)> m_broadcastFilter = nullptr;

    std::function<void()> m_emptyBlockGenerated = nullptr;

    /// for output time-out caused viewchange
    /// m_fastViewChange is false: output viewchangeWarning to indicate PBFT consensus timeout
    std::atomic_bool m_fastViewChange = {false};

    uint8_t maxTTL = MAXTTL;

    /// map between nodeIdx to view
    mutable SharedMutex x_viewMap;
    std::map<IDXTYPE, VIEWTYPE> m_viewMap;

    std::atomic<uint64_t> m_sealingNumber = {0};

    bool m_broadcastPrepareByTree = true;
};
}  // namespace consensus
}  // namespace dev
