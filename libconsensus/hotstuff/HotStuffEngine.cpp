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
            collectGarbage();
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
    HOTSTUFFENGINE_LOG(level) << LOG_DESC(descMsg) << LOG_KV("reqType", msg->type())
                              << LOG_KV("reqHash", msg->blockHash().abridged())
                              << LOG_KV("reqIdx", msg->idx())
                              << LOG_KV("reqHeight", msg->blockHeight())
                              << LOG_KV("reqView", msg->view())
                              << LOG_KV("curBlk", m_highestBlockHeader.number())
                              << LOG_KV("consensusBlockNumber", m_consensusBlockNumber)
                              << LOG_KV("curView", m_view) << LOG_KV("curToView", m_toView)
                              << LOG_KV("msgLeader", getLeader(msg->view()))
                              << LOG_KV("curLeadder", getLeader()) << LOG_KV("idx", nodeIdx());
}

// send message to the leader of given view
bool HotStuffEngine::sendMessageToLeader(HotStuffMsg::Ptr msg)
{
    IDXTYPE leaderIdx = getLeader(msg->view());
    // no need send message to the leader
    if (leaderIdx == nodeIdx())
    {
        return true;
    }
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
    m_toView += 1;
    auto curPrepareQC = m_hotStuffMsgCache->prepareQC();
    HotStuffNewViewMsg::Ptr newViewMessage = nullptr;
    if (curPrepareQC)
    {
        newViewMessage = m_hotStuffMsgFactory->buildHotStuffNewViewMsg(m_keyPair, m_idx,
            curPrepareQC->blockHash(), curPrepareQC->blockHeight(), m_toView, curPrepareQC->view());
    }
    else
    {
        newViewMessage = m_hotStuffMsgFactory->buildHotStuffNewViewMsg(m_keyPair, m_idx,
            m_highestBlockHeader.hash(), m_highestBlockHeader.number(), m_toView, m_view);
    }
    HOTSTUFFENGINE_LOG(DEBUG) << LOG_DESC("triggerNextView: send NewViewMessage to the leader")
                              << LOG_KV("reqHash", newViewMessage->blockHash().abridged())
                              << LOG_KV("reqHeight", newViewMessage->blockHeight())
                              << LOG_KV("leader", getLeader(newViewMessage->view()))
                              << LOG_KV("curBlkNum", m_highestBlockHeader.number())
                              << LOG_KV("curBlkHash", m_highestBlockHeader.number())
                              << LOG_KV("view", m_view) << LOG_KV("toView", m_toView)
                              << LOG_KV("justifyView", newViewMessage->justifyView())
                              << LOG_KV("idx", nodeIdx());
    m_hotStuffMsgCache->addNewViewCache(newViewMessage, minValidNodes());
    triggerGeneratePrepare();
    // send New-View message to the leader
    sendMessageToLeader(newViewMessage);
}

bool HotStuffEngine::isValidNewViewMsg(HotStuffMsg::Ptr newViewMsg)
{
    // check the view, maybe the future NewViewMsg
    if (newViewMsg->view() < m_view)
    {
        printHotStuffMsgInfo(newViewMsg, "invalid NewViewMsg: lower view of the request", WARNING);
        return false;
    }
    // check blockHash
    if (newViewMsg->blockHeight() == m_highestBlockHeader.number() &&
        newViewMsg->blockHash() != m_highestBlockHeader.hash())
    {
        printHotStuffMsgInfo(newViewMsg, "invalid NewViewMsg: inconsistent block hash", WARNING);
        return false;
    }
    // check blockNumber
    if (newViewMsg->blockHeight() < m_highestBlockHeader.number())
    {
        printHotStuffMsgInfo(newViewMsg, "invalid NewViewMsg: consensused block", WARNING);
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
    HOTSTUFFENGINE_LOG(INFO) << LOG_DESC("handleNewViewMsg")
                             << LOG_KV("hash", newViewMsg->blockHash().abridged())
                             << LOG_KV("height", newViewMsg->blockHeight())
                             << LOG_KV("idx", newViewMsg->idx())
                             << LOG_KV("view", newViewMsg->view())
                             << LOG_KV("justifyView", newViewMsg->justifyView())
                             << LOG_KV("nodeIdx", nodeIdx());
    m_hotStuffMsgCache->addNewViewCache(newViewMsg, minValidNodes());
    triggerGeneratePrepare();
    return true;
}

void HotStuffEngine::triggerGeneratePrepare()
{
    // collect enough new-view message, notify PBFTSealer to generate the prepare message
    auto newViewCacheSize = m_hotStuffMsgCache->getNewViewCacheSize(m_toView);
    if (newViewCacheSize == minValidNodes())
    {
        HOTSTUFFENGINE_LOG(INFO)
            << LOG_DESC(
                   "handleNewViewMsg: collect enough newView, can generate prepare message now")
            << LOG_KV("curHash", m_highestBlockHeader.hash().abridged())
            << LOG_KV("curBlk", m_highestBlockHeader.number()) << LOG_KV("view", m_view)
            << LOG_KV("newViewSize", newViewCacheSize) << LOG_KV("idx", nodeIdx());
        resetView(m_toView);
        // obtain the max justify view
        m_justifyView = m_hotStuffMsgCache->getMaxJustifyView(m_toView);
        HOTSTUFFENGINE_LOG(INFO) << LOG_DESC(
                                        "reset view to toView after collect most new-view requests")
                                 << LOG_KV("updatedView", m_view) << LOG_KV("idx", nodeIdx());
        m_canGeneratePrepare(true);
    }
}

bool HotStuffEngine::omitEmptyBlock(HotStuffPrepareMsg::Ptr prepareMsg)
{
    if (!m_omitEmptyBlock)
    {
        return false;
    }
    // empty block
    if (0 == prepareMsg->getBlock()->getTransactionSize())
    {
        HOTSTUFFENGINE_LOG(INFO) << LOG_DESC("omit empty block")
                                 << LOG_KV("hash", prepareMsg->blockHash().abridged())
                                 << LOG_KV("height", prepareMsg->blockHeight())
                                 << LOG_KV("view", prepareMsg->view())
                                 << LOG_KV("idx", prepareMsg->idx());
        // update the consensus time
        m_timeManager->m_lastConsensusTime = utcTime();
        // update changeCycle
        m_timeManager->m_changeCycle = 0;
        m_hotStuffMsgCache->resetCacheAfterCommit(
            m_hotStuffMsgCache->executedPrepareCache()->blockHash());
        m_hotStuffMsgCache->removeInvalidViewChange(m_view);
        // trigger next view
        triggerNextView();
        return true;
    }
    return false;
}

// the leader generate prepare message and broadcast it to the replias
void HotStuffEngine::generateAndBroadcastPrepare(std::shared_ptr<dev::eth::Block> block)
{
    m_canGeneratePrepare(false);
    HotStuffPrepareMsg::Ptr prepareMsg = m_hotStuffMsgFactory->buildHotStuffPrepare(m_keyPair,
        m_idx, block->blockHeader().hash(), block->blockHeader().number(), m_view, m_justifyView);
    HOTSTUFFENGINE_LOG(INFO) << LOG_DESC("generateAndBroadcastPrepare")
                             << LOG_KV("rawPrepareHash", block->blockHeader().hash().abridged())
                             << LOG_KV("blockHeight", block->blockHeader().number())
                             << LOG_KV("justifyView", m_justifyView) << LOG_KV("view", m_view)
                             << LOG_KV("idx", m_idx);
    prepareMsg->setBlock(block);
    broadCastMsg(prepareMsg);

    Guard l(m_mutex);
    // handle the prepareMsg
    handlePrepareMsg(prepareMsg);
}

HotStuffPrepareMsg::Ptr HotStuffEngine::execBlock(HotStuffPrepareMsg::Ptr rawPrepareMsg)
{
    auto startT = utcTime();
    checkBlockValid(*rawPrepareMsg->getBlock());
    std::shared_ptr<dev::eth::Block> pBlock =
        std::make_shared<dev::eth::Block>(*rawPrepareMsg->getBlock());

    // set sender for prepareMsg
    m_txPool->verifyAndSetSenderForBlock(*pBlock);
    // executeBlock
    auto pexecContext = executeBlock(pBlock);
    HotStuffPrepareMsg::Ptr executedPrepare =
        std::make_shared<HotStuffPrepareMsg>(m_keyPair, pBlock, rawPrepareMsg);
    executedPrepare->setExecResult(pexecContext);
    auto execTime = utcTime() - startT;
    HOTSTUFFENGINE_LOG(INFO) << LOG_DESC("execBlock")
                             << LOG_KV("rawPrepareHash", rawPrepareMsg->blockHash())
                             << LOG_KV("executedHash", executedPrepare->blockHash())
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
    if (prepareMsg->type() != HotStuffPacketType::PrepareVotePacket)
    {
        return false;
    }
    // check view
    if (prepareMsg->view() < m_view)
    {
        printHotStuffMsgInfo(
            prepareMsg, "handlePrepareVoteMsg: invalid prepare-vote msg for illegal view", WARNING);
        return false;
    }
    if (!isValidHotStuffMsg(prepareMsg))
    {
        printHotStuffMsgInfo(
            prepareMsg, "handlePrepareVoteMsg: not cached in local executedPrepare Cache", WARNING);
        return false;
    }
    HOTSTUFFENGINE_LOG(DEBUG) << LOG_DESC("handlePrepareVoteMsg and add to prepareCache")
                              << LOG_KV("reqHash", prepareMsg->blockHash().abridged())
                              << LOG_KV("reqHeight", prepareMsg->blockHeight())
                              << LOG_KV("reqIdx", prepareMsg->idx())
                              << LOG_KV("reqView", prepareMsg->view()) << LOG_KV("curView", m_view)
                              << LOG_KV("idx", nodeIdx());
    m_hotStuffMsgCache->addPrepareCache(prepareMsg, minValidNodes());
    checkAndGeneratePrepareQC(prepareMsg);
    return true;
}

QuorumCert::Ptr HotStuffEngine::checkAndGenerateQC(
    size_t const& cacheSize, HotStuffMsg::Ptr voteMsg, int const packetType)
{
    if (cacheSize == minValidNodes())
    {
        HOTSTUFFENGINE_LOG(INFO) << LOG_DESC("collect enough vote message and broadcast QCMsg")
                                 << LOG_KV("reqType", voteMsg->type())
                                 << LOG_KV("reqHash", voteMsg->blockHash())
                                 << LOG_KV("reqHeight", voteMsg->blockHeight())
                                 << LOG_KV("reqView", voteMsg->view())
                                 << LOG_KV("cacheSize", cacheSize) << LOG_KV("view", m_view)
                                 << LOG_KV("idx", nodeIdx());
        HotStuffMsg::Ptr QCMsg = m_hotStuffMsgFactory->buildQuorumCert(
            m_keyPair, packetType, m_idx, voteMsg->blockHash(), voteMsg->blockHeight(), m_view);
        // broadcast message to the replias
        broadCastMsg(QCMsg);
        return QCMsg;
    }
    return nullptr;
}

void HotStuffEngine::checkAndGeneratePrepareQC(HotStuffMsg::Ptr prepareMsg)
{
    // check collect enough prepareMessage or not
    size_t prepareVoteSize = m_hotStuffMsgCache->getPrepareCacheSize(prepareMsg->blockHash());
    auto prepareQCMsg =
        checkAndGenerateQC(prepareVoteSize, prepareMsg, HotStuffPacketType::PrepareQCPacekt);
    if (prepareQCMsg)
    {
        onReceiveQCMsg(prepareQCMsg, HotStuffPacketType::PrecommitVotePacket);
    }
}

bool HotStuffEngine::isValidHotStuffMsg(HotStuffMsg::Ptr hotstuffMsg)
{
    // check idx
    NodeID nodeId;
    if (!getNodeIDByIndex(nodeId, hotstuffMsg->idx()))
    {
        printHotStuffMsgInfo(
            hotstuffMsg, "InValid HotStuffMsg: not generated from sealer", WARNING);
        return false;
    }
    if (hotstuffMsg->blockHeight() < m_highestBlockHeader.number())
    {
        printHotStuffMsgInfo(hotstuffMsg, "InValid HotStuffMsg: has consensused", WARNING);
        return false;
    }
    if (hotstuffMsg->blockHeight() == m_highestBlockHeader.number() &&
        hotstuffMsg->blockHash() != m_highestBlockHeader.hash())
    {
        printHotStuffMsgInfo(hotstuffMsg, "InValid HotStuffMsg: inconsistent block hash", WARNING);
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
    printHotStuffMsgInfo(prepareMsg, "handlePrepareMsg and addRawPrepare", INFO);
    // cache the rawprepare received from leader
    m_hotStuffMsgCache->addRawPrepare(prepareMsg);
    // execute block
    auto executedPrepare = execBlock(prepareMsg);
    m_hotStuffMsgCache->addExecutedPrepare(executedPrepare);

    // add executed block into the cache
    m_hotStuffMsgCache->addPrepareCache(executedPrepare, minValidNodes());
    printHotStuffMsgInfo(
        executedPrepare, "handlePrepareMsg succ: send executedPrepare to the leader", INFO);

    int packetType = HotStuffPacketType::PrepareVotePacket;
    // send the prepare-vote message to the leader
    HotStuffMsg::Ptr prepareVoteMsg = m_hotStuffMsgFactory->buildHotStuffMsg(m_keyPair, packetType,
        m_idx, executedPrepare->blockHash(), executedPrepare->blockHeight(), m_view);
    sendMessageToLeader(prepareVoteMsg);
    // check the prepareCache
    checkAndGeneratePrepareQC(executedPrepare);
    return true;
}

bool HotStuffEngine::isValidPrepareMsg(HotStuffPrepareMsg::Ptr prepareMsg)
{
    if (prepareMsg->getBlock()->blockHeader().parentHash() != m_highestBlockHeader.hash())
    {
        printHotStuffMsgInfo(
            prepareMsg, "InvalidPrepareMsg: inconsistent parent block hash", WARNING);
        return false;
    }
    if (!isValidHotStuffMsg(prepareMsg))
    {
        return false;
    }
    // check view
    if (prepareMsg->view() < m_view)
    {
        printHotStuffMsgInfo(prepareMsg, "InvalidPrepareMsg: invalid view", WARNING);
        return false;
    }
    // check existence
    if (m_hotStuffMsgCache->existedRawPrepare(prepareMsg->blockHash()))
    {
        printHotStuffMsgInfo(prepareMsg, "InvalidPrepareMsg: already cached", WARNING);
        return false;
    }
    // check safeNode, the prepareMsg must be extended from lockedQC
    auto curLockedQC = m_hotStuffMsgCache->lockedQC();
    if (curLockedQC &&
        prepareMsg->getBlock()->blockHeader().parentHash() != curLockedQC->blockHash() &&
        prepareMsg->justifyView() < curLockedQC->view())
    {
        HOTSTUFFENGINE_LOG(WARNING)
            << LOG_DESC("InvalidPrepareMsg: safeNode check failed")
            << LOG_KV("reqHash", prepareMsg->blockHash().abridged())
            << LOG_KV("reqIdx", prepareMsg->idx()) << LOG_KV("reqHeight", prepareMsg->blockHeight())
            << LOG_KV("justifyView", prepareMsg->justifyView())
            << LOG_KV("curBlk", m_highestBlockHeader.number())
            << LOG_KV("lockedQCHash", curLockedQC->blockHash().abridged())
            << LOG_KV("lockedQCView", curLockedQC->view()) << LOG_KV("curView", m_view)
            << LOG_KV("idx", nodeIdx());
        return false;
    }
    // check leader
    if (getLeader(prepareMsg->view()) != prepareMsg->idx())
    {
        printHotStuffMsgInfo(
            prepareMsg, "InvalidPrepareMsg: not generated from the leader", WARNING);
        return false;
    }
    // update the view to the highest
    resetView(prepareMsg->view());
    return true;
}


void HotStuffEngine::resetView(VIEWTYPE const& view)
{
    if (m_view < view)
    {
        HOTSTUFFENGINE_LOG(DEBUG) << LOG_DESC("reset current view and toView")
                                  << LOG_KV("curView", m_view) << LOG_KV("curToView", m_toView)
                                  << LOG_KV("updatedView", view);
        m_view = m_toView = view;
        m_timeManager->m_lastConsensusTime = utcTime();
        m_timeManager->m_changeCycle = 0;
    }
}

bool HotStuffEngine::checkQCMsg(QuorumCert::Ptr QCMsg)
{
    if (!isValidHotStuffMsg(QCMsg))
    {
        return false;
    }
    // catchup the view after block sync
    if (QCMsg->view() > m_view && QCMsg->blockHeight() >= m_highestBlockHeader.number())
    {
        resetView(QCMsg->view());
        printHotStuffMsgInfo(QCMsg, "catchup the view to the QCView");
    }
    // require to be from the leader
    if (getLeader() != QCMsg->idx())
    {
        printHotStuffMsgInfo(QCMsg, "invalid QCMsg for not from the leader");
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
    printHotStuffMsgInfo(prepareQC, "onReceivePrepareQCMsg: set the prepareQC", INFO);
    m_hotStuffMsgCache->setPrepareQC(prepareQC);
    return true;
}

bool HotStuffEngine::isValidVoteMsg(HotStuffMsg::Ptr voteMsg)
{
    if (getLeader() != m_idx)
    {
        printHotStuffMsgInfo(voteMsg, "invalid voteMsg for this node is not the leader", WARNING);
        return false;
    }
    if (!isValidHotStuffMsg(voteMsg))
    {
        return false;
    }
    if (!m_hotStuffMsgCache->existedInPrepareQC(voteMsg->blockHash()))
    {
        auto curPrepareQC = m_hotStuffMsgCache->prepareQC();
        HOTSTUFFENGINE_LOG(WARNING)
            << LOG_DESC("invalid voteMsg for not cached in prepareQC")
            << LOG_KV("reqHash", voteMsg->blockHash().abridged())
            << LOG_KV("prepareQCHash", curPrepareQC->blockHash().abridged())
            << LOG_KV("reqHeight", voteMsg->blockHeight())
            << LOG_KV("prepareQCHeight", curPrepareQC->blockHeight())
            << LOG_KV("reqView", voteMsg->view()) << LOG_KV("prepareQCView", curPrepareQC->view())
            << LOG_KV("reqIdx", voteMsg->idx()) << LOG_KV("prepareQCIdx", curPrepareQC->idx())
            << LOG_KV("curView", m_view) << LOG_KV("idx", nodeIdx());
    }
    return true;
}

// the leader collect the pre-commit message and
// broadcast preCommitQC
bool HotStuffEngine::handlePreCommitVoteMsg(HotStuffMsg::Ptr preCommitMsg)
{
    if (preCommitMsg->type() != HotStuffPacketType::PrecommitVotePacket)
    {
        return false;
    }
    if (!isValidVoteMsg(preCommitMsg))
    {
        return false;
    }
    printHotStuffMsgInfo(preCommitMsg, "handle pre-commit vote message", INFO);
    m_hotStuffMsgCache->addPreCommitCache(preCommitMsg, minValidNodes());
    size_t cachedPrecommitSize =
        m_hotStuffMsgCache->getPreCommitCacheSize(preCommitMsg->blockHash());
    auto lockedQCMsg = checkAndGenerateQC(
        cachedPrecommitSize, preCommitMsg, HotStuffPacketType::PrecommitQCPacket);
    if (lockedQCMsg)
    {
        onReceiveQCMsg(lockedQCMsg, HotStuffPacketType::CommitVotePacket);
    }

    return true;
}

bool HotStuffEngine::onReceiveQCMsg(QuorumCert::Ptr QCMsg, int const packetType)
{
    if (!checkQCMsg(QCMsg))
    {
        printHotStuffMsgInfo(QCMsg, "Invalid QCMsg: check failed", WARNING);
        return false;
    }
    // check hash
    if (!m_hotStuffMsgCache->existedExecutedPrepare(QCMsg->blockHash()))
    {
        printHotStuffMsgInfo(QCMsg, "Invalid QCMsg: not exist in executedPrepare", WARNING);
        return false;
    }
    // omitEmptyBlock directly
    if (omitEmptyBlock(m_hotStuffMsgCache->executedPrepareCache()))
    {
        printHotStuffMsgInfo(QCMsg, "omit empty block when receive QCMsg");
        return false;
    }
    // send the prepare-vote message to the leader
    HotStuffMsg::Ptr voteMsg = m_hotStuffMsgFactory->buildHotStuffMsg(
        m_keyPair, packetType, m_idx, QCMsg->blockHash(), QCMsg->blockHeight(), m_view);
    // send commit-vote to the leader
    sendMessageToLeader(voteMsg);
    if (getLeader() != m_idx)
    {
        return true;
    }
    // the node is the leader
    if (QCMsg->type() == HotStuffPacketType::PrepareQCPacekt)
    {
        m_hotStuffMsgCache->setPrepareQC(QCMsg);
    }
    if (QCMsg->type() == HotStuffPacketType::PrecommitQCPacket)
    {
        m_hotStuffMsgCache->addLockedQC(QCMsg);
    }
    // try to handle the vote message in consideration of there is only one node
    handlePrepareVoteMsg(voteMsg);
    handlePreCommitVoteMsg(voteMsg);
    handleCommitVoteMsg(voteMsg);
    return true;
}

bool HotStuffEngine::onReceivePrecommitQCMsg(QuorumCert::Ptr preCommitQC)
{
    if (!onReceiveQCMsg(preCommitQC, HotStuffPacketType::CommitVotePacket))
    {
        return false;
    }
    printHotStuffMsgInfo(preCommitQC, "onReceivePrecommitQCMsg: modify lockedQC");
    m_hotStuffMsgCache->addLockedQC(preCommitQC);
    return true;
}

bool HotStuffEngine::handleCommitVoteMsg(HotStuffMsg::Ptr commitMsg)
{
    if (commitMsg->type() != HotStuffPacketType::CommitVotePacket)
    {
        return false;
    }
    if (!isValidVoteMsg(commitMsg))
    {
        return false;
    }
    printHotStuffMsgInfo(commitMsg, "handleCommitVoteMsg: add commit vote to the cache", INFO);
    m_hotStuffMsgCache->addCommitCache(commitMsg, minValidNodes());
    size_t commitMsgSize = m_hotStuffMsgCache->getCommitCacheSize(commitMsg->blockHash());
    if (checkAndGenerateQC(commitMsgSize, commitMsg, HotStuffPacketType::CommitQCPacket))
    {
        // collect enough commit-vote message, broadcast commitQC message and commit block
        return commitBlock();
    }
    return true;
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
    if (m_hotStuffMsgCache->executedPrepareCache()->blockHeight() !=
        m_highestBlockHeader.number() + 1)
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
            << LOG_KV("curNum", m_highestBlockHeader.number())
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
    printHotStuffMsgInfo(commitQC, "onReceiveCommitQCMsg and commitBlock", INFO);
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
    if (m_blockChain->number() == 0 || m_highestBlockHeader.number() < block.blockHeader().number())
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
                                 << LOG_KV("num", m_highestBlockHeader.number())
                                 << LOG_KV("sealerIdx", m_highestBlockHeader.sealer())
                                 << LOG_KV("hash", m_highestBlockHeader.hash().abridged())
                                 << LOG_KV("next", m_consensusBlockNumber)
                                 << LOG_KV("tx", block.getTransactionSize())
                                 << LOG_KV("nodeIdx", nodeIdx());
        triggerNextView();
    }
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
                             << LOG_KV("toView", m_toView)
                             << LOG_KV("changeCycle", m_timeManager->m_changeCycle)
                             << LOG_KV("nodeIdx", nodeIdx())
                             << LOG_KV("myNode", m_keyPair.pub().abridged());
}

bool HotStuffEngine::reachMinBlockGenTime()
{
    /// since canHandleBlockForNextLeader has enforced the  next leader sealed block can't be
    /// handled before the current leader generate a new block, it's no need to add other
    /// conditions to enforce this striction
    return (utcTime() - m_timeManager->m_lastConsensusTime) >= m_timeManager->m_minBlockGenTime;
}


void HotStuffEngine::collectGarbage()
{
    Guard l(m_mutex);
    if (!m_highestBlockHeader)
    {
        return;
    }
    Timer t;
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    if (now - m_timeManager->m_lastGarbageCollection >
        std::chrono::seconds(m_timeManager->CollectInterval))
    {
        m_hotStuffMsgCache->collectCache(m_highestBlockHeader);
        m_timeManager->m_lastGarbageCollection = now;
        HOTSTUFFENGINE_LOG(DEBUG) << LOG_DESC("collectGarbage")
                                  << LOG_KV("Timecost", 1000 * t.elapsed());
    }
}
