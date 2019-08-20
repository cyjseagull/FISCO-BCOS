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

#include "HotStuffEngine.h"

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::blockchain;
using namespace dev::eth;
using namespace dev::p2p;
using namespace dev::network;
using namespace dev::consensus;

void HotStuffEngine::initHotStuff()
{
    HOTSTUFFENGINE_LOG(INFO) << LOG_DESC("init HotStuffEngine..");
    m_hotStuffMsgCacheFactory = std::make_shared<HotStuffMsgCacheFactory>();
    m_hotStuffMsgFactory = std::make_shared<HotStuffMsgFactory>();
    m_hotStuffMsgCache = m_hotStuffMsgCacheFactory->createHotStuffMsgCache();
    m_timeManager = std::make_shared<TimeManager>();
    // init the timeout
    m_timeManager->initTimerManager(3 * m_timeManager->m_emptyBlockGenTime);
}

// start the hotstuff engine thread
void HotStuffEngine::start()
{
    initHotStuff();
    ConsensusEngineBase::start();
    HOTSTUFFENGINE_LOG(INFO) << LOG_DESC("start HotStuffEngine.");
}


void HotStuffEngine::handleMsg(dev::p2p::P2PMessage::Ptr p2pMessage)
{
    Guard l(m_mutex);
    int packetType = p2pMessage->packetType();
    switch (packetType)
    {
    case HotStuffPacketType::NewViewPacket:
    {
        HotStuffNewViewMsg::Ptr newViewMsg = decodeMessageBuffer<HotStuffNewViewMsg>(p2pMessage);
        handleNewViewMsg(newViewMsg);
        break;
    }
    case HotStuffPacketType::PreparePacket:
    {
        HotStuffPrepareMsg::Ptr preparePacket = decodeMessageBuffer<HotStuffPrepareMsg>(p2pMessage);
        handlePrepareMsg(preparePacket);
        break;
    }
    case HotStuffPacketType::PrepareVotePacket:
    {
        HotStuffMsg::Ptr prepareVoteMsg = decodeMessageBuffer<HotStuffMsg>(p2pMessage);
        handlePrepareVoteMsg(prepareVoteMsg);
        break;
    }
    case HotStuffPacketType::PrepareQCPacekt:
    {
        HotStuffMsg::Ptr prepareQCMsg = decodeMessageBuffer<QuorumCert>(p2pMessage);
        onReceivePrepareQCMsg(prepareQCMsg);
        break;
    }
    case HotStuffPacketType::PrecommitVotePacket:
    {
        HotStuffMsg::Ptr preCommitVoteMsg = decodeMessageBuffer<HotStuffMsg>(p2pMessage);
        handlePreCommitVoteMsg(preCommitVoteMsg);
        break;
    }
    case HotStuffPacketType::PrecommitQCPacket:
    {
        HotStuffMsg::Ptr preCommitQCMsg = decodeMessageBuffer<QuorumCert>(p2pMessage);
        onReceivePrecommitQCMsg(preCommitQCMsg);
        break;
    }
    case HotStuffPacketType::CommitVotePacket:
    {
        HotStuffMsg::Ptr commitVoteMsg = decodeMessageBuffer<HotStuffMsg>(p2pMessage);
        handleCommitVoteMsg(commitVoteMsg);
        break;
    }
    case HotStuffPacketType::CommitQCPacket:
    {
        HotStuffMsg::Ptr commitQCMsg = decodeMessageBuffer<QuorumCert>(p2pMessage);
        onReceiveCommitQCMsg(commitQCMsg);
        break;
    }
    default:
    {
        HOTSTUFFENGINE_LOG(DEBUG) << LOG_DESC("handleMsg:  Err hotstuff message")
                                  << LOG_KV("type", packetType) << LOG_KV("nodeIdx", nodeIdx())
                                  << LOG_KV("myNode", m_keyPair.pub().abridged());
        break;
    }
    }
}


void HotStuffEngine::workLoop()
{
    while (isWorking())
    {
        try
        {
            if (nodeIdx() == MAXIDX)
            {
                std::unique_lock<std::mutex> l(x_signalled);
                m_signalled.wait_for(l, std::chrono::milliseconds(5));
                continue;
            }
            auto ret = m_hotstuffMsgQueue.tryPop(c_PopWaitSeconds);
            if (ret.first)
            {
                HOTSTUFFENGINE_LOG(DEBUG)
                    << LOG_DESC("workLoop: handleMsg") << LOG_KV("type", ret.second->packetType())
                    << LOG_KV("nodeIdx", nodeIdx());
                handleMsg(ret.second);
            }
            checkTimeout();
        }
        catch (std::exception& _e)
        {
            LOG(ERROR) << _e.what();
        }
    }
}

bool HotStuffEngine::shouldSaveMessage(dev::p2p::P2PMessage::Ptr msg)
{
    return msg->packetType() <= HotStuffPacketType::CommitQCPacket;
}

/**
 * @brief : receive the hotstuff message and push the valid message into queue
 *
 * @param exception: network exception
 * @param session: the session that receive the message
 * @param message
 */
void HotStuffEngine::onRecvHotStuffMessage(dev::p2p::NetworkException,
    std::shared_ptr<dev::p2p::P2PSession> session, dev::p2p::P2PMessage::Ptr message)
{
    if (nodeIdx() == MAXIDX)
    {
        HOTSTUFFENGINE_LOG(TRACE) << LOG_DESC(
            "onRecvHotStuffMessage: I'm an observer or not in the group, drop the PBFT message "
            "packets "
            "directly");
        return;
    }
    ssize_t peerIdx = 0;
    // invalid message
    if (!isValidReq(message, session, peerIdx))
    {
        return;
    }
    if (shouldSaveMessage(message))
    {
        m_hotstuffMsgQueue.push(message);
        m_signalled.notify_all();
        return;
    }
    HOTSTUFFENGINE_LOG(WARNING) << LOG_DESC("onRecvHotStuffMessage: illegal hotstuff msg")
                                << LOG_KV("fromIdx", peerIdx)
                                << LOG_KV("type", message->packetType())
                                << LOG_KV("nodeIdx", nodeIdx());
}


// broadcast message
bool HotStuffEngine::broadCastMsg(
    HotStuffMsg::Ptr msg, std::function<bool(dev::network::NodeID const&)> const& filter)
{
    auto sessions = m_service->sessionInfosByProtocolID(m_protocolId);
    m_connectedNode = sessions.size();
    if (m_connectedNode <= 0)
    {
        return false;
    }
    NodeIDs nodeIdList;
    for (auto const& session : sessions)
    {
        if (filter && filter(session.nodeID()))
        {
            continue;
        }
        nodeIdList.push_back(session.nodeID());
    }
    m_service->asyncMulticastMessageByNodeIDList(nodeIdList, encodeToP2PMessage(msg));
    return true;
}

// send message to the given node
bool HotStuffEngine::sendMessage(HotStuffMsg::Ptr msg, NodeID const& nodeId)
{
    auto sessions = m_service->sessionInfosByProtocolID(m_protocolId);
    for (auto const& session : sessions)
    {
        if (session.nodeID() == nodeId)
        {
            m_service->asyncSendMessageByNodeID(nodeId, encodeToP2PMessage(msg), nullptr);
            return true;
        }
    }
    return false;
}

void HotStuffEngine::printHotStuffMsgInfo(
    HotStuffMsg::Ptr msg, std::string const& descMsg, LogLevel const& level)
{
    HOTSTUFFENGINE_LOG(level) << LOG_DESC(descMsg) << LOG_KV("type", msg->type())
                              << LOG_KV("idx", msg->idx()) << LOG_KV("msgView", msg->view())
                              << LOG_KV("hash", msg->blockHash().abridged())
                              << LOG_KV("msgHeight", msg->blockHeight())
                              << LOG_KV("curNum", m_highestBlockHeader.number())
                              << LOG_KV("consensusBlockNumber", m_consensusBlockNumber)
                              << LOG_KV("curView", m_view) << LOG_KV("curToView", m_toView)
                              << LOG_KV("expectedLeader", getLeader(msg->view()))
                              << LOG_KV("idx", nodeIdx());
}

// send message to the leader of given view
bool HotStuffEngine::sendMessageToLeader(HotStuffMsg::Ptr msg)
{
    IDXTYPE leaderIdx = getLeader(msg->view());
    NodeID leaderNodeId;
    if (!getNodeIDByIndex(leaderNodeId, leaderIdx))
    {
        printHotStuffMsgInfo(
            msg, "sendMessageToLeader failed for the leader is not in the sealer list", ERROR);
        return false;
    }
    printHotStuffMsgInfo(msg, "sendMessageToLeader");
    return sendMessage(msg, leaderNodeId);
}

// send Next-View message to the leader when timeout
void HotStuffEngine::triggerNextView()
{
    m_view += 1;
    auto curPrepareQC = m_hotStuffMsgCache->prepareQC();
    HotStuffNewViewMsg::Ptr newViewMessage =
        m_hotStuffMsgFactory->buildHotStuffNewViewMsg(m_keyPair, m_idx, curPrepareQC->blockHash(),
            curPrepareQC->blockHeight(), m_view, curPrepareQC->view());
    m_hotStuffMsgCache->addNewViewCache(newViewMessage);
    // send New-View message to the leader
    sendMessageToLeader(newViewMessage);
}

bool HotStuffEngine::isValidNewViewMsg(HotStuffMsg::Ptr newViewMsg)
{
    // check the view
    if (newViewMsg->view() != m_view - 1)
    {
        printHotStuffMsgInfo(newViewMsg, "invalid NewViewMsg", WARNING);
        return false;
    }
    // check blockHash
    if (newViewMsg->blockHash() != m_highestBlockHeader.hash())
    {
        printHotStuffMsgInfo(newViewMsg, "invalid NewViewMsg", WARNING);
        return false;
    }
    // check blockNumber
    if (newViewMsg->blockHeight() != m_highestBlockHeader.number())
    {
        printHotStuffMsgInfo(newViewMsg, "invalid NewViewMsg", WARNING);
        return false;
    }
    return true;
}

// check the node should seal a new block or not
bool HotStuffEngine::shouldSeal()
{
    if (m_accountType != NodeAccountType::SealerAccount)
    {
        return false;
    }
    // check Leader
    if (getLeader(m_view) != m_idx)
    {
        return false;
    }
    return true;
}

// handle NewViewMsg
bool HotStuffEngine::handleNewViewMsg(HotStuffNewViewMsg::Ptr newViewMsg)
{
    if (!isValidNewViewMsg(newViewMsg))
    {
        return false;
    }
    m_hotStuffMsgCache->addNewViewCache(newViewMsg);
    // collect enough new-view message, notify PBFTSealer to generate the prepare message
    if (m_hotStuffMsgCache->getNewViewCacheSize(m_view) == minValidNodes())
    {
        m_canGeneratePrepare();
    }
    return true;
}

// the leader generate prepare message and broadcast it to the replias
void HotStuffEngine::generateAndBroadcastPrepare(std::shared_ptr<dev::eth::Block> block)
{
    // obtain the max justify view
    auto justifyView = m_hotStuffMsgCache->getMaxJustifyView(m_view);
    HotStuffPrepareMsg::Ptr prepareMsg = m_hotStuffMsgFactory->buildHotStuffPrepare(m_keyPair,
        m_idx, m_highestBlockHeader.hash(), m_highestBlockHeader.number(), m_view, justifyView);
    prepareMsg->setBlock(block);
    broadCastMsg(prepareMsg);
    // handle the prepareMsg
    handlePrepareMsg(prepareMsg);
}

HotStuffPrepareMsg::Ptr HotStuffEngine::execBlock(HotStuffPrepareMsg::Ptr rawPrepareMsg)
{
    auto startT = utcTime();
    checkBlockValid(*rawPrepareMsg->getBlock());
    HotStuffPrepareMsg::Ptr executedPrepare =
        std::make_shared<HotStuffPrepareMsg>(m_keyPair, rawPrepareMsg);
    // set sender for prepareMsg
    m_txPool->verifyAndSetSenderForBlock(*executedPrepare->getBlock());
    // executeBlock and set execute context
    executedPrepare->setExecResult(executeBlock(executedPrepare->getBlock()));
    auto execTime = utcTime() - startT;
    HOTSTUFFENGINE_LOG(INFO) << LOG_DESC("execBlock")
                             << LOG_KV("blkNum", executedPrepare->getBlock()->header().number())
                             << LOG_KV(
                                    "transNum", executedPrepare->getBlock()->getTransactionSize())
                             << LOG_KV("reqIdx", executedPrepare->idx())
                             << LOG_KV("nodeIdx", nodeIdx()) << LOG_KV("execTime", execTime);
    return executedPrepare;
}


/**
 * @brief: 1. the leader receive the PrepareVotePacket from the replias
 *         2. if the leader collect (n-f) PrepareVotePacket, generate the prepareQC
 *         3. the leader broadcast PrepareQC
 */
bool HotStuffEngine::handlePrepareVoteMsg(HotStuffMsg::Ptr prepareMsg)
{
    // check view
    if (prepareMsg->view() < m_view)
    {
        printHotStuffMsgInfo(
            prepareMsg, "handlePrepareVoteMsg: invalid prepare-vote msg for illegal view", WARNING);
        return false;
    }
    if (!isValidHotStuffMsg(prepareMsg))
    {
        printHotStuffMsgInfo(prepareMsg,
            "inValidRepliaPrepareMsg: not cached in local executedPrepare Cache", WARNING);
        return false;
    }
    m_hotStuffMsgCache->addPrepareCache(prepareMsg);
    checkAndGeneratePrepareQC(prepareMsg);
    return true;
}

void HotStuffEngine::checkAndGenerateQC(
    size_t const& cacheSize, HotStuffMsg::Ptr voteMsg, int const packetType)
{
    if (cacheSize == minValidNodes())
    {
        HOTSTUFFENGINE_LOG(INFO) << LOG_DESC("collect enough vote message")
                                 << LOG_KV("type", voteMsg->type())
                                 << LOG_KV("hash", voteMsg->blockHash())
                                 << LOG_KV("height", voteMsg->blockHeight())
                                 << LOG_KV("reqView", voteMsg->view()) << LOG_KV("view", m_view);
        HotStuffMsg::Ptr QCMsg = m_hotStuffMsgFactory->buildHotStuffMsg(
            m_keyPair, packetType, m_idx, voteMsg->blockHash(), voteMsg->blockHeight(), m_view);
        // broadcast message to the replias
        broadCastMsg(QCMsg);
    }
}

void HotStuffEngine::checkAndGeneratePrepareQC(HotStuffMsg::Ptr prepareMsg)
{
    // check collect enough prepareMessage or not
    size_t prepareVoteSize = m_hotStuffMsgCache->getPrepareCacheSize(prepareMsg->blockHash());
    checkAndGenerateQC(prepareVoteSize, prepareMsg, HotStuffPacketType::PrepareQCPacekt);
}

bool HotStuffEngine::isValidHotStuffMsg(HotStuffMsg::Ptr hotstuffMsg)
{
    // check idx
    NodeID nodeId;
    if (!getNodeIDByIndex(nodeId, hotstuffMsg->idx()))
    {
        printHotStuffMsgInfo(
            hotstuffMsg, "inValid HotStuffMsg: not generated from sealer", WARNING);
        return false;
    }
    // check blockNumber
    if (hotstuffMsg->blockHeight() != m_consensusBlockNumber)
    {
        printHotStuffMsgInfo(
            hotstuffMsg, "inValid HotStuffMsg: not equal to the consensus blockNumber", WARNING);
        return false;
    }
    return true;
}

/**
 * @brief : 1. the replias receive PreparePacket from the leader
 *          2. the replias exeucte the valid PreparePacket and cache the result
 *          3. the replias send prepare-vote message to the leader
 * @param prepareMsg: the PreparePacket received from the leader
 * @return true : handle a valid PreparePacket
 * @return false : handle a invalid PreparePacket
 */
bool HotStuffEngine::handlePrepareMsg(HotStuffPrepareMsg::Ptr prepareMsg)
{
    if (!isValidPrepareMsg(prepareMsg))
    {
        return false;
    }
    // cache the rawprepare received from leader
    m_hotStuffMsgCache->addRawPrepare(prepareMsg);
    // execute block
    auto executedPrepare = execBlock(prepareMsg);
    m_hotStuffMsgCache->addExecutedPrepare(executedPrepare);

    // add executed block into the cache
    m_hotStuffMsgCache->addPrepareCache(executedPrepare);
    printHotStuffMsgInfo(prepareMsg, "handlePrepareMsg: send prepareMsg to the leader", INFO);

    int packetType = HotStuffPacketType::PrepareVotePacket;
    // send the prepare-vote message to the leader
    HotStuffMsg::Ptr prepareVoteMsg = m_hotStuffMsgFactory->buildHotStuffMsg(m_keyPair, packetType,
        m_idx, executedPrepare->blockHash(), executedPrepare->blockHeight(), m_view);
    sendMessageToLeader(prepareVoteMsg);
    // check the prepareCache
    checkAndGeneratePrepareQC(prepareMsg);
    return true;
}

bool HotStuffEngine::isValidPrepareMsg(HotStuffPrepareMsg::Ptr prepareMsg)
{
    // check leader
    if (getLeader() != prepareMsg->idx())
    {
        printHotStuffMsgInfo(
            prepareMsg, "InvalidPrepareMsg: not generated from the leader", WARNING);
        return false;
    }
    if (!isValidHotStuffMsg(prepareMsg))
    {
        return false;
    }
    if (prepareMsg->view() != m_view)
    {
        printHotStuffMsgInfo(prepareMsg, "InvalidPrepareMsg: illegal view", WARNING);
        return false;
    }
    // check existence
    if (m_hotStuffMsgCache->existedRawPrepare(prepareMsg->blockHash()))
    {
        printHotStuffMsgInfo(prepareMsg, "InvalidPrepareMsg: already cached", WARNING);
        return false;
    }
// check parentBlockHash
#if 0
    if (prepareMsg->getBlock()->blockHeader().parentHash() != m_highestBlockHeader.hash())
    {
        printHotStuffMsgInfo(
            prepareMsg, "InvalidPrepareMsg: inconsistent parent block hash", WARNING);
        return false;
    }
#endif
    // check safeNode, the prepareMsg must be extended from lockedQC
    auto curLockedQC = m_hotStuffMsgCache->lockedQC();
    if (prepareMsg->getBlock()->blockHeader().parentHash() != curLockedQC->blockHash() &&
        prepareMsg->justifyView() < curLockedQC->view())
    {
        HOTSTUFFENGINE_LOG(WARNING) << LOG_DESC("InvalidPrepareMsg: safeNode check failed")
                                    << LOG_KV("hash", prepareMsg->blockHash().abridged())
                                    << LOG_KV("height", prepareMsg->blockHeight())
                                    << LOG_KV("justifyView", prepareMsg->justifyView())
                                    << LOG_KV("blkNum", m_highestBlockHeader.number())
                                    << LOG_KV("lockedQCHash", curLockedQC->blockHash().abridged())
                                    << LOG_KV("lockedQCView", curLockedQC->view());
        return false;
    }
    return true;
}

bool HotStuffEngine::checkQCMsg(QuorumCert::Ptr QCMsg)
{
    if (!isValidHotStuffMsg(QCMsg))
    {
        return false;
    }
    // check view
    if (QCMsg->view() != m_view)
    {
        return false;
    }
    // TODO: check sigList
    return true;
}

/**
 * @brief : 1. receive prepareQCPacket from the leader
 *          2. check the prepareQCPacket
 *          3. send pre-commit message to the leader
 * @param prepareQC : the prepareQCPacket received from the leader
 * @return true
 * @return false
 */
bool HotStuffEngine::onReceivePrepareQCMsg(QuorumCert::Ptr prepareQC)
{
    if (!onReceiveQCMsg(prepareQC, HotStuffPacketType::PrecommitVotePacket))
    {
        return false;
    }
    m_hotStuffMsgCache->setPrepareQC(prepareQC);
    return true;
}

bool HotStuffEngine::isValidVoteMsg(HotStuffMsg::Ptr voteMsg)
{
    if (getLeader() != m_idx)
    {
        return false;
    }
    if (!isValidHotStuffMsg(voteMsg))
    {
        return false;
    }
    if (!m_hotStuffMsgCache->existedExecutedPrepare(voteMsg->blockHash()))
    {
        return false;
    }
    return true;
}

// the leader collect the pre-commit message and
// broadcast preCommitQC
bool HotStuffEngine::handlePreCommitVoteMsg(HotStuffMsg::Ptr preCommitMsg)
{
    if (!isValidVoteMsg(preCommitMsg))
    {
        return false;
    }
    m_hotStuffMsgCache->addPreCommitCache(preCommitMsg);
    size_t cachedPrecommitSize =
        m_hotStuffMsgCache->getPreCommitCacheSize(preCommitMsg->blockHash()) + 1;
    checkAndGenerateQC(cachedPrecommitSize, preCommitMsg, HotStuffPacketType::PrecommitQCPacket);
    return true;
}

bool HotStuffEngine::onReceiveQCMsg(QuorumCert::Ptr QCMsg, int const packetType)
{
    if (!checkQCMsg(QCMsg))
    {
        return false;
    }
    // check hash
    if (!m_hotStuffMsgCache->existedExecutedPrepare(QCMsg->blockHash()))
    {
        return false;
    }
    // send the prepare-vote message to the leader
    HotStuffMsg::Ptr voteMsg = m_hotStuffMsgFactory->buildHotStuffMsg(
        m_keyPair, packetType, m_idx, QCMsg->blockHash(), QCMsg->blockHeight(), m_view);
    // send commit-vote to the leader
    sendMessageToLeader(voteMsg);
    return true;
}

bool HotStuffEngine::onReceivePrecommitQCMsg(QuorumCert::Ptr preCommitQC)
{
    if (!onReceiveQCMsg(preCommitQC, HotStuffPacketType::CommitVotePacket))
    {
        return false;
    }
    m_hotStuffMsgCache->addLockedQC(preCommitQC);
    return true;
}

bool HotStuffEngine::handleCommitVoteMsg(HotStuffMsg::Ptr commitMsg)
{
    if (!isValidVoteMsg(commitMsg))
    {
        return false;
    }
    m_hotStuffMsgCache->addCommitCache(commitMsg);
    size_t commitMsgSize = m_hotStuffMsgCache->getCommitCacheSize(commitMsg->blockHash()) + 1;
    checkAndGenerateQC(commitMsgSize, commitMsg, HotStuffPacketType::CommitQCPacket);
    return commitBlock();
}

bool HotStuffEngine::commitBlock()
{
    if (!m_hotStuffMsgCache->executedPrepareCache())
    {
        HOTSTUFFENGINE_LOG(WARNING)
            << LOG_DESC("commitBlock Failed: empty executedPrepareCache")
            << LOG_KV("highestNum", m_highestBlockHeader.number())
            << LOG_KV("highestHash", m_highestBlockHeader.number()) << LOG_KV("view", m_view);
        return false;
    }
    if (m_hotStuffMsgCache->executedPrepareCache()->view() != m_view)
    {
        HOTSTUFFENGINE_LOG(WARNING)
            << LOG_DESC("CommitBlock Failed: invalidView")
            << LOG_KV("prepView", m_hotStuffMsgCache->executedPrepareCache()->view())
            << LOG_KV("curView", m_view)
            << LOG_KV("prepHeight", m_hotStuffMsgCache->executedPrepareCache()->blockHeight())
            << LOG_KV(
                   "prepHash", m_hotStuffMsgCache->executedPrepareCache()->blockHash().abridged())
            << LOG_KV("idx", nodeIdx());
        return false;
    }
    if (m_hotStuffMsgCache->executedPrepareCache()->blockHeight() != m_highestBlock.number() + 1)
    {
        HOTSTUFFENGINE_LOG(WARNING)
            << LOG_DESC("CommitBlock Failed: invalid height")
            << LOG_KV("highestNum", m_highestBlockHeader.number())
            << LOG_KV("prepHeight", m_hotStuffMsgCache->executedPrepareCache()->blockHeight())
            << LOG_KV("highestHash", m_highestBlockHeader.number())
            << LOG_KV(
                   "prepHash", m_hotStuffMsgCache->executedPrepareCache()->blockHash().abridged())
            << LOG_KV("view", m_view);
        return false;
    }

    CommitResult ret =
        m_blockChain->commitBlock(m_hotStuffMsgCache->executedPrepareCache()->getBlock(),
            m_hotStuffMsgCache->executedPrepareCache()->getExecContext());
    if (CommitResult::OK == ret)
    {
        dropHandledTransactions(*m_hotStuffMsgCache->executedPrepareCache()->getBlock());
        m_blockSync->noteSealingBlockNumber(
            m_hotStuffMsgCache->executedPrepareCache()->blockHeight());
        HOTSTUFFENGINE_LOG(INFO)
            << LOG_DESC("CommitBlock Succ")
            << LOG_KV("prepHeight", m_hotStuffMsgCache->executedPrepareCache()->blockHeight())
            << LOG_KV("prepIdx", m_hotStuffMsgCache->executedPrepareCache()->idx())
            << LOG_KV("hash", m_hotStuffMsgCache->executedPrepareCache()->blockHash().abridged())
            << LOG_KV("nodeIdx", nodeIdx()) << LOG_KV("myNode", m_keyPair.pub().abridged());
        // delete the cache
        m_hotStuffMsgCache->resetCacheAfterCommit(
            m_hotStuffMsgCache->executedPrepareCache()->blockHash());
        m_hotStuffMsgCache->removeInvalidViewChange(m_view);
        return true;
    }
    else
    {
        HOTSTUFFENGINE_LOG(WARNING)
            << LOG_DESC("CommitBlock Failed")
            << LOG_KV("reqNum", m_hotStuffMsgCache->executedPrepareCache()->blockHeight())
            << LOG_KV("curNum", m_highestBlock.number())
            << LOG_KV("reqIdx", m_hotStuffMsgCache->executedPrepareCache()->idx())
            << LOG_KV("hash", m_hotStuffMsgCache->executedPrepareCache()->blockHash().abridged())
            << LOG_KV("nodeIdx", nodeIdx()) << LOG_KV("myNode", m_keyPair.pub().abridged());
        return false;
    }
}

bool HotStuffEngine::onReceiveCommitQCMsg(QuorumCert::Ptr commitQC)
{
    if (!onReceiveQCMsg(commitQC, HotStuffPacketType::CommitQCPacket))
    {
        return false;
    }
    // commit the block
    return commitBlock();
}

bool HotStuffEngine::reachBlockIntervalTime()
{
    return (utcTime() - m_timeManager->m_lastConsensusTime) >= m_timeManager->m_emptyBlockGenTime;
}

void HotStuffEngine::reportBlock(dev::eth::Block const& block)
{
    Guard l(m_mutex);
    if (m_blockChain->number() == 0 || m_highestBlock.number() < block.blockHeader().number())
    {
        m_highestBlockHeader = block.blockHeader();
        m_hotStuffMsgCache->resetCacheAfterCommit(block.blockHeader().hash());
        m_hotStuffMsgCache->removeInvalidViewChange(m_view);
        if (m_highestBlockHeader.number() >= m_consensusBlockNumber)
        {
            m_consensusBlockNumber = m_highestBlockHeader.number() + 1;
            m_timeManager->m_lastConsensusTime = utcTime();
        }
        resetConfig();
        m_timeManager->m_changeCycle = 0;
        HOTSTUFFENGINE_LOG(INFO) << LOG_DESC("^^^^^^^^Report")
                                 << LOG_KV("num", m_highestBlock.number())
                                 << LOG_KV("sealerIdx", m_highestBlock.sealer())
                                 << LOG_KV("hash", m_highestBlock.hash().abridged())
                                 << LOG_KV("next", m_consensusBlockNumber)
                                 << LOG_KV("tx", block.getTransactionSize())
                                 << LOG_KV("nodeIdx", nodeIdx());
    }
    triggerNextView();
}

void HotStuffEngine::checkTimeout()
{
    if (!m_timeManager->isTimeout())
    {
        return;
    }
    m_blockSync->noteSealingBlockNumber(m_blockChain->number());
    m_timeManager->m_lastConsensusTime = utcTime();
    m_timeManager->m_changeCycle += 1;
    triggerNextView();
    HOTSTUFFENGINE_LOG(INFO) << LOG_DESC("checkTimeout") << LOG_KV("view", m_view)
                             << LOG_KV("toView", m_toView) << LOG_KV("nodeIdx", nodeIdx())
                             << LOG_KV("myNode", m_keyPair.pub().abridged());
}

bool HotStuffEngine::reachMinBlockGenTime()
{
    /// since canHandleBlockForNextLeader has enforced the  next leader sealed block can't be
    /// handled before the current leader generate a new block, it's no need to add other
    /// conditions to enforce this striction
    return (utcTime() - m_timeManager->m_lastConsensusTime) >= m_timeManager->m_minBlockGenTime;
}
