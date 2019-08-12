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
 * @brief : implementation of Grouped-PBFT consensus engine
 * @file: GroupPBFTEngine.cpp
 * @author: yujiechen
 * @date: 2019-5-28
 */
#include "GroupPBFTEngine.h"
using namespace dev::eth;
using namespace dev::network;
using namespace dev::sync;

namespace dev
{
namespace consensus
{
void GroupPBFTEngine::start()
{
    PBFTEngine::start();
    GPBFTENGINE_LOG(INFO) << LOG_DESC("Start GroupPBFTEngine");
}

void GroupPBFTEngine::initPBFTCacheObject()
{
    PBFTEngine::initPBFTCacheObject();
    m_groupPBFTReqCache = std::dynamic_pointer_cast<GroupPBFTReqCache>(m_reqCache);
    m_groupPBFTMsgFactory = std::dynamic_pointer_cast<GroupPBFTMsgFactory>(m_pbftMsgFactory);
}

/// resetConfig for group PBFTEngine
void GroupPBFTEngine::resetConfig()
{
    updateMaxBlockTransactions();
    m_idx = MAXIDX;
    updateConsensusNodeList();
    {
        ReadGuard l(m_sealerListMutex);
        for (size_t i = 0; i < m_sealerList.size(); i++)
        {
            if (m_sealerList[i] == m_keyPair.pub())
            {
                m_accountType = NodeAccountType::SealerAccount;
                m_idx = i;
                break;
            }
        }
        m_nodeNum = m_sealerList.size();
    }
    if (m_nodeNum < 1)
    {
        PBFTENGINE_LOG(ERROR) << LOG_DESC(
            "Must set at least one pbft sealer, current number of sealers is zero");
        BOOST_THROW_EXCEPTION(
            EmptySealers() << errinfo_comment("Must set at least one pbft sealer!"));
    }
    m_cfgErr = (m_idx == MAXIDX);
    if (m_cfgErr)
    {
        return;
    }
    // obtain zone id of this node
    m_zoneId = m_idx / m_configuredGroupSize;
    // calculate current zone num
    m_zoneNum = (m_nodeNum + m_configuredGroupSize - 1) / m_configuredGroupSize;
    // get zone size
    if (m_nodeNum - m_zoneId * m_configuredGroupSize < m_configuredGroupSize)
    {
        m_zoneSize = m_nodeNum - m_zoneId * m_configuredGroupSize;
    }
    else
    {
        m_zoneSize = int64_t(m_configuredGroupSize);
    }
    // calculate the idx of the node in a given group
    auto origin_groupIdx = int64_t(m_groupIdx);
    m_groupIdx = m_idx % m_zoneSize;
    m_FaultTolerance = (m_zoneSize - 1) / 3;
    m_groupSwitchCycle = m_FaultTolerance + 1;
    m_groupFaultTolerance = (m_zoneNum - 1) / 3;

    // update the node list of other groups that can receive the network packet of this node if the
    // idx has been updated
    if (origin_groupIdx != m_groupIdx)
    {
        WriteGuard l(x_broadCastListAmongGroups);
        ReadGuard l_sealers(m_sealerListMutex);
        m_broadCastListAmongGroups.clear();
        int64_t i = 0;
        for (i = 0; i < m_zoneNum; i++)
        {
            auto nodeIndex = i * m_configuredGroupSize + m_groupIdx;
            if (i == m_zoneId)
            {
                continue;
            }
            if ((uint64_t)(nodeIndex) >= m_sealerList.size())
            {
                break;
            }
            m_broadCastListAmongGroups.insert(m_sealerList[nodeIndex]);
        }
    }
    GPBFTENGINE_LOG(INFO) << LOG_DESC("reset configure") << LOG_KV("totalNode", m_nodeNum)
                          << LOG_KV("globalIdx", m_idx) << LOG_KV("zoneNum", m_zoneNum)
                          << LOG_KV("zoneId", m_zoneId) << LOG_KV("zoneSize", m_zoneSize)
                          << LOG_KV("groupIdx", m_groupIdx)
                          << LOG_KV("faultTolerance", m_FaultTolerance)
                          << LOG_KV("groupFaultTolerance", m_groupFaultTolerance)
                          << LOG_KV("idx", m_idx);
}

/// get consensus zone:
/// consensus_zone = (globalView + currentBlockNumber % m_zoneSwitchBlocks) % zone_size
ZONETYPE GroupPBFTEngine::getConsensusZone(int64_t const& blockNumber) const
{
    /// get consensus zone failed
    if (m_cfgErr || m_highestBlock.sealer() == Invalid256)
    {
        return MAXIDX;
    }
    return (m_globalView + blockNumber / m_zoneSwitchBlocks) % m_zoneNum;
}

// determine the given node located in the consensus zone or not
bool GroupPBFTEngine::locatedInConsensusZone(
    int64_t const& blockNumber, ZONETYPE const& zoneId) const
{
    if (m_cfgErr || m_highestBlock.sealer() == Invalid256)
    {
        return false;
    }
    // get the current consensus zone
    ZONETYPE consZone = getConsensusZone(blockNumber);
    // get consensus zone failed or the node is not in the consens zone
    if (consZone == MAXIDX || consZone != zoneId)
    {
        return false;
    }
    return true;
}

/// get current leader
std::pair<bool, IDXTYPE> GroupPBFTEngine::getLeader() const
{
    // the node is not in the consensus group
    if (!locatedInConsensusZone(m_highestBlock.number(), m_zoneId) || m_leaderFailed)
    {
        return std::make_pair(false, MAXIDX);
    }
    int64_t zoneSize = m_zoneSize;
    int64_t zoneId = m_zoneId;
    int64_t configuredGroupSize = m_configuredGroupSize;
    IDXTYPE idx = (IDXTYPE)(
        (m_currentBlockHash + (u256)m_highestBlock.number() + (u256)m_view) % (u256)zoneSize +
        (u256)(zoneId * configuredGroupSize));
    return std::make_pair(true, idx);
}

/// get next leader
IDXTYPE GroupPBFTEngine::getNextLeader() const
{
    auto expectedBlockNumber = m_highestBlock.number() + 1;
    /// the next leader is not located in this Zone
    if (!locatedInConsensusZone(expectedBlockNumber, m_zoneId))
    {
        return MAXIDX;
    }
    int64_t zoneSize = m_zoneSize;
    return (IDXTYPE)((m_currentBlockHash + (u256)expectedBlockNumber) % (u256)(zoneSize));
}

/// get nodeIdx according to nodeID
/// override the function to make sure the node only broadcast message to the nodes that belongs to
/// m_zoneId
ssize_t GroupPBFTEngine::getGroupIndexBySealer(dev::network::NodeID const& nodeId)
{
    ssize_t nodeIndex = PBFTEngine::getIndexBySealer(nodeId);
    /// observer or not in the zone
    if (nodeIndex == -1)
    {
        return -1;
    }
    ssize_t nodeIdxInGroup = nodeIndex / m_configuredGroupSize;
    if (nodeIdxInGroup != m_zoneId)
    {
        return -1;
    }
    return nodeIdxInGroup;
}


bool GroupPBFTEngine::broadCastMsgAmongGroups(const int packetType, std::string const& key,
    bytesConstRef data, unsigned const& ttl, std::unordered_set<dev::network::NodeID> const& filter)
{
    return PBFTEngine::broadcastMsg(packetType, key, data, filter, ttl, m_groupBroadcastFilter);
}

ssize_t GroupPBFTEngine::filterGroupNodeByNodeID(dev::network::NodeID const& nodeId)
{
    // should broadcast to the node
    if (m_broadCastListAmongGroups.count(nodeId))
    {
        return 1;
    }
    // shouldn't broadcast to the node
    return -1;
}

bool GroupPBFTEngine::isLeader()
{
    auto leader = getLeader();
    if (leader.first && leader.second == m_groupIdx)
    {
        return true;
    }
    return false;
}

bool GroupPBFTEngine::locatedInConsensusZone() const
{
    return locatedInConsensusZone(m_highestBlock.number(), m_zoneId);
}


void GroupPBFTEngine::broadcastPrepareToOtherGroups(std::shared_ptr<PrepareReq> prepareReq)
{
        bytes prepare_data;
        int packetType = GroupPBFTPacketType::PrepareReqPacket;
        prepareReq->encode(prepare_data);
        GPBFTENGINE_LOG(DEBUG) << LOG_DESC("broadcast prepareReq to nodes of other groups")
                               << LOG_KV("height", prepareReq->height)
                               << LOG_KV("hash", prepareReq->block_hash.abridged())
                               << LOG_KV("groupIdx", m_groupIdx) << LOG_KV("zoneId", m_zoneId)
                               << LOG_KV("idx", m_idx);

        auto sessions = m_service->sessionInfosByProtocolID(m_protocolId);
        std::set<dev::network::NodeID> peers;
        for (auto const& session : sessions)
        {
            peers.insert(session.nodeID());
        }
        auto selectedNodes = NodeIdFilterHandler(peers);
        for (auto const& nodeId : selectedNodes)
        {
            broadcastMark(nodeId, packetType, prepareReq->uniqueKey());
        }
        /// send messages according to node id
        m_service->asyncMulticastMessageByNodeIDList(
            selectedNodes, transDataToMessage(ref(prepare_data), packetType, 0));

}


/**
 * @brief
 *
 * @param superSignReq
 * @param endpoint
 * @return true
 * @return false
 */
bool GroupPBFTEngine::handleSuperSignReq(
    std::shared_ptr<SuperSignReq> superSignReq, PBFTMsgPacket const& pbftMsg)
{
    bool valid = decodeToRequests(superSignReq, ref(pbftMsg.data));
    if (!valid)
    {
        return false;
    }
    std::ostringstream oss;
    auto zoneId = getZoneByNodeIndex(superSignReq->idx);
    commonLogWhenHandleMsg(oss, "handleSuperSignReq", zoneId, superSignReq, pbftMsg);
    // check superSignReq
    CheckResult ret = isValidSuperSignReq(superSignReq, zoneId, oss);
    if (ret == CheckResult::INVALID)
    {
        return false;
    }
    // add future superSignReq
    if ((ret == CheckResult::FUTURE) &&
        (m_groupPBFTReqCache->getSuperSignCacheSize(superSignReq->block_hash) <=
            (size_t)(minValidGroups() - 1)))
    {
        m_groupPBFTReqCache->addSuperSignReq(superSignReq, zoneId);
        return true;
    }
    m_groupPBFTReqCache->addSuperSignReq(superSignReq, zoneId);
    checkAndBackupForSuperSignEnough();
    GPBFTENGINE_LOG(INFO) << LOG_DESC("handleSuperSignMsg succ") << LOG_KV("INFO", oss.str());
    return true;
}


void GroupPBFTEngine::printWhenCollectEnoughSuperReq(std::string const& desc, size_t superReqSize)
{
    GPBFTENGINE_LOG(INFO) << LOG_DESC(desc)
                          << LOG_KV(
                                 "hash", m_groupPBFTReqCache->prepareCache()->block_hash.abridged())
                          << LOG_KV("height", m_groupPBFTReqCache->prepareCache()->height)
                          << LOG_KV("SuperReqSize", superReqSize) << LOG_KV("zone", m_zoneId)
                          << LOG_KV("idx", m_idx) << LOG_KV("nodeId", m_keyPair.pub().abridged());
}

/// collect superSignReq and broadcastCommitReq if collected enough superSignReq
void GroupPBFTEngine::checkAndBackupForSuperSignEnough()
{
    // must collectEnough SignReq firstly
    if (!collectEnoughSignReq(false) || !collectEnoughSuperSignReq())
    {
        return;
    }
    // broadcast commit message
    auto superSignSize =
        m_groupPBFTReqCache->getSuperSignCacheSize(m_groupPBFTReqCache->prepareCache()->block_hash);
    printWhenCollectEnoughSuperReq(
        "checkAndBackupForSuperSignEnough: collect enough superSignReq, backup prepare request "
        "to PBFT backup DB",
        superSignSize);

    // backup signReq for non-empty block
    if (m_omitEmptyBlock && m_groupPBFTReqCache->prepareCache()->pBlock->getTransactionSize() > 0)
    {
        GPBFTENGINE_LOG(INFO)
            << LOG_DESC("checkAndBackupForSuperSignEnough: update and backup commitedPrepare")
            << LOG_KV("height", m_groupPBFTReqCache->prepareCache()->height)
            << LOG_KV("hash", m_groupPBFTReqCache->prepareCache()->block_hash.abridged())
            << LOG_KV("zone", m_zoneId) << LOG_KV("idx", m_idx)
            << LOG_KV("nodeId", m_keyPair.pub().abridged());
        m_groupPBFTReqCache->updateCommittedPrepare();
        backupMsg(c_backupKeyCommitted, m_groupPBFTReqCache->committedPrepareCache());
    }
    // broadcast commit message
    if (!broadcastCommitReq(*(m_groupPBFTReqCache->prepareCache())))
    {
        GPBFTENGINE_LOG(WARNING) << LOG_DESC(
            "checkAndBackupForSuperSignEnough: broadcastCommitReq failed");
    }
    checkAndSave();
}

/**
 * @brief: called by handleSignMsg:
 *         1. check the collected signReq inner the group are enough or not
 *         2. broadcast superSignReq if the collected signReq are enough
 */
void GroupPBFTEngine::checkAndCommit()
{
    if (!collectEnoughSignReq())
    {
        return;
    }
    broadcastSuperSignMsg();
    checkAndBackupForSuperSignEnough();
}

bool GroupPBFTEngine::broadcastSuperSignMsg()
{
    // generate the superSignReq
    std::shared_ptr<SuperSignReq> req = std::make_shared<SuperSignReq>(
        m_groupPBFTReqCache->prepareCache(), VIEWTYPE(m_view), IDXTYPE(m_idx));
    // cache superSignReq
    m_groupPBFTReqCache->addSuperSignReq(req, m_zoneId);
    GPBFTENGINE_LOG(INFO) << LOG_DESC("checkAndCommit, broadcast and cache SuperSignReq")
                          << LOG_KV("height", req->height)
                          << LOG_KV("hash", req->block_hash.abridged()) << LOG_KV("idx", m_idx)
                          << LOG_KV("groupIdx", m_groupIdx) << LOG_KV("zoneId", m_zoneId)
                          << LOG_KV("nodeId", m_keyPair.pub().abridged());
    bytes encodedReq;
    req->encode(encodedReq);
    return broadCastMsgAmongGroups(
        GroupPBFTPacketType::SuperSignReqPacket, req->uniqueKey(), ref(encodedReq), 2);
}

/**
 * @brief : called by handleCommitMsg
 *          1. check the collected commitReq inside of the group are enough or not
 *          2. broadcast superCommitReq if the collected commitReq are enough
 */
void GroupPBFTEngine::checkAndSave()
{
    if (!collectEnoughCommitReq())
    {
        return;
    }
    broadcastSuperCommitMsg();
    checkSuperReqAndCommitBlock();
}

bool GroupPBFTEngine::broadcastSuperCommitMsg()
{
    // generate SuperCommitReq
    std::shared_ptr<SuperCommitReq> req = std::make_shared<SuperCommitReq>(
        m_groupPBFTReqCache->prepareCache(), VIEWTYPE(m_view), IDXTYPE(m_idx));
    // cache SuperCommitReq
    m_groupPBFTReqCache->addSuperCommitReq(req, m_zoneId);
    GPBFTENGINE_LOG(INFO) << LOG_DESC("checkAndSave, broadcast and cache SuperCommitReq")
                          << LOG_KV("height", req->height)
                          << LOG_KV("hash", req->block_hash.abridged()) << LOG_KV("idx", m_idx)
                          << LOG_KV("groupIdx", m_groupIdx) << LOG_KV("zoneId", m_zoneId)
                          << LOG_KV("nodeId", m_keyPair.pub().abridged());
    bytes encodedReq;
    req->encode(encodedReq);
    return broadCastMsgAmongGroups(
        GroupPBFTPacketType::SuperCommitReqPacket, req->uniqueKey(), ref(encodedReq), 2);
}

bool GroupPBFTEngine::handleSuperViewChangeReq(
    std::shared_ptr<SuperViewChangeReq> superViewChangeReq, PBFTMsgPacket const& pbftMsg)
{
    bool valid = decodeToRequests(superViewChangeReq, ref(pbftMsg.data));
    if (!valid)
    {
        return false;
    }
    auto zoneId = getZoneByNodeIndex(superViewChangeReq->idx);
    std::ostringstream oss;
    commonLogWhenHandleMsg(oss, "handleSuperViewChangeReq", zoneId, superViewChangeReq, pbftMsg);
    CheckResult result = isValidSuperViewChangeReq(superViewChangeReq, zoneId, oss);
    if (result == CheckResult::INVALID)
    {
        return false;
    }
    // this node is not located in the consensus zone and receive the superViewChange request from
    // consensus zone
    if (!superViewChangeReq->isGlobal() && !locatedInConsensusZone() &&
        locatedInConsensusZone(superViewChangeReq->height, zoneId))
    {
        GPBFTENGINE_LOG(DEBUG)
            << LOG_DESC(
                   "located in non-consensus zone and receive superviewchange from consensus zone")
            << LOG_KV("consensusZone", zoneId) << LOG_KV("height", superViewChangeReq->height)
            << LOG_KV("toView", superViewChangeReq->view)
            << LOG_KV("genIdx", superViewChangeReq->idx)
            << LOG_KV("hash", superViewChangeReq->block_hash.abridged())
            << LOG_KV("curZone", m_zoneId) << LOG_KV("idx", nodeIdx());
        // broadcast viewchange request to other nodes
        m_toView = superViewChangeReq->view;
        // remove the invalid viewchangeReq when receive superViewChangeReq from the consensus zone
        m_groupPBFTReqCache->removeInvalidViewChange(m_toView, m_highestBlock);
        broadcastViewChange(superViewChangeReq);
    }
    m_groupPBFTReqCache->addSuperViewChangeReq(superViewChangeReq, zoneId);
    checkSuperViewChangeAndChangeView();

    GPBFTENGINE_LOG(INFO) << LOG_DESC("handleSuperViewChangeReq succ") << LOG_KV("INFO", oss.str())
                          << LOG_KV("global", superViewChangeReq->isGlobal());
    return true;
}


bool GroupPBFTEngine::broadcastGlobalViewChangeReq()
{
    std::shared_ptr<ViewChangeReq> req = m_pbftMsgFactory->buildViewChangeReq(
        m_keyPair, m_highestBlock.number(), m_toView, nodeIdx(), m_highestBlock.hash());
    std::shared_ptr<GroupViewChangeReq> groupViewChangeReq =
        std::dynamic_pointer_cast<GroupViewChangeReq>(req);
    groupViewChangeReq->setType(1);
    PBFTENGINE_LOG(DEBUG) << LOG_DESC("broadcastGlobalViewChangeReq ") << LOG_KV("v", m_view)
                          << LOG_KV("toV", m_toView) << LOG_KV("curNum", m_highestBlock.number())
                          << LOG_KV("hash", req->block_hash.abridged())
                          << LOG_KV("nodeIdx", nodeIdx())
                          << LOG_KV("myNode", m_keyPair.pub().abridged());
    bytes view_change_data;
    req->encode(view_change_data);
    return broadcastMsg(
        PBFTPacketType::ViewChangeReqPacket, req->uniqueKey(), ref(view_change_data));
}


bool GroupPBFTEngine::broadcastViewChange(std::shared_ptr<SuperViewChangeReq> superViewChangeReq)
{
    std::shared_ptr<ViewChangeReq> req = m_pbftMsgFactory->buildViewChangeReq(m_keyPair,
        m_highestBlock.number(), superViewChangeReq->view, nodeIdx(), m_highestBlock.hash());

    GPBFTENGINE_LOG(DEBUG)
        << LOG_DESC(
               "broadcast viewchange request when receive viewchangeRequest from consensusZone")
        << LOG_KV("curView", m_view) << LOG_KV("toView", req->view) << LOG_KV("genIdx", req->idx)
        << LOG_KV("nodeIdx", nodeIdx()) << LOG_KV("zoneIdx", m_zoneId);
    bytes viewChangeData;
    req->encode(viewChangeData);
    return broadcastMsg(PBFTPacketType::ViewChangeReqPacket, req->uniqueKey(), ref(viewChangeData));
}

// broadcast superviewchange Req
bool GroupPBFTEngine::broadcastSuperViewChangeReq(uint8_t type)
{
    // generate superViewChangeReq
    std::shared_ptr<SuperViewChangeReq> req = m_groupPBFTMsgFactory->buildSuperViewChangeReq(
        m_keyPair, m_highestBlock.number(), m_toView, m_idx, m_highestBlock.hash(), type);
    // cache superViewChangeReq
    m_groupPBFTReqCache->addSuperViewChangeReq(req, m_zoneId);
    GPBFTENGINE_LOG(INFO) << LOG_DESC("broadcast SuperViewChangeReq")
                          << LOG_KV("height", req->height)
                          << LOG_KV("hash", req->block_hash.abridged()) << LOG_KV("view", req->view)
                          << LOG_KV("idx", m_idx) << LOG_KV("groupIdx", m_groupIdx)
                          << LOG_KV("zoneId", m_zoneId);
    bytes encodedData;
    req->encode(encodedData);
    return broadCastMsgAmongGroups(
        GroupPBFTPacketType::SuperViewChangeReqPacket, req->uniqueKey(), ref(encodedData), 2);
}

void GroupPBFTEngine::checkAndChangeView()
{
    size_t count = m_groupPBFTReqCache->getViewChangeSize(m_toView);
    if (count >= (size_t)(minValidNodes() - 1))
    {
        size_t globalViewChangeSize = m_groupPBFTReqCache->getGlobalViewChangeSize(m_toView);
        m_groupPBFTReqCache->removeInvalidViewChange(m_toView, m_highestBlock);
        if (globalViewChangeSize > (size_t)(m_FaultTolerance))
        {
            GPBFTENGINE_LOG(INFO) << LOG_DESC(
                                         "checkAndChangeView: broadcast global super viewchange")
                                  << LOG_KV("toView", m_toView)
                                  << LOG_KV("height", m_highestBlock.number())
                                  << LOG_KV("globalViewChangeSize", globalViewChangeSize)
                                  << LOG_KV("zoneId", m_zoneId) << LOG_KV("idx", nodeIdx());
            broadcastSuperViewChangeReq(1);
        }
        else
        {
            GPBFTENGINE_LOG(INFO) << LOG_DESC("checkAndChangeView: broadcast super viewchange")
                                  << LOG_KV("toView", m_toView)
                                  << LOG_KV("height", m_highestBlock.number())
                                  << LOG_KV("globalViewChangeSize", globalViewChangeSize)
                                  << LOG_KV("zoneId", m_zoneId) << LOG_KV("idx", nodeIdx());
            broadcastSuperViewChangeReq(0);
        }
    }
    checkSuperViewChangeAndChangeView();
}

void GroupPBFTEngine::checkSuperViewChangeAndChangeView()
{
    if (!collectEnoughSuperViewChangReq())
    {
        return;
    }
    auto globalSuperViewChangeSize = m_groupPBFTReqCache->getGlobalSuperViewChangeSize(m_toView);

    if (globalSuperViewChangeSize > (size_t)(m_groupFaultTolerance))
    {
        m_globalView = m_toView;
        updateCurrentHash(m_highestBlock.hash());
        GPBFTENGINE_LOG(INFO)
            << LOG_DESC("update globalView for collect enough global superViewChangeReq")
            << LOG_KV("globalView", m_globalView)
            << LOG_KV("globalSuperViewChangeSize", globalSuperViewChangeSize)
            << LOG_KV("groupFaultTolerance", m_groupFaultTolerance)
            << LOG_KV("currentBlockHash", m_currentBlockHash) << LOG_KV("zoneId", m_zoneId);
    }
    changeView();
    GPBFTENGINE_LOG(INFO) << LOG_DESC("changeView for collect enough SuperViewChangeReq")
                          << LOG_KV("currentView", m_view) << LOG_KV("toView", m_toView)
                          << LOG_KV("curConsZone", getConsensusZone(m_highestBlock.number()))
                          << LOG_KV("curLeader", getLeader().second) << LOG_KV("idx", m_idx)
                          << LOG_KV("zoneId", m_zoneId);
}

CheckResult GroupPBFTEngine::isValidSuperViewChangeReq(
    std::shared_ptr<SuperViewChangeReq> superViewChangeReq, ZONETYPE const& zoneId,
    std::ostringstream const& oss)
{
    // check the viewchangeReq exists or not
    if (m_groupPBFTReqCache->cacheExists(
            m_groupPBFTReqCache->superViewChangeCache(), superViewChangeReq->view, zoneId))
    {
        GPBFTENGINE_LOG(DEBUG) << LOG_DESC("Invalid SuperViewChangeReq: Duplicated")
                               << LOG_KV("INFO", oss.str());
        return CheckResult::INVALID;
    }
    // check view(must be large than the view of this group)
    if (superViewChangeReq->view <= m_view || superViewChangeReq->height < m_highestBlock.number())
    {
        GPBFTENGINE_LOG(DEBUG) << LOG_DESC("Invalid SuperViewChangeReq: invalid view or height")
                               << LOG_KV("INFO", oss.str());
        return CheckResult::INVALID;
    }
    // check blockHash
    if (superViewChangeReq->height == m_highestBlock.number() &&
        (superViewChangeReq->block_hash != m_highestBlock.hash() ||
            m_blockChain->getBlockByHash(superViewChangeReq->block_hash) == nullptr))
    {
        GPBFTENGINE_LOG(DEBUG) << LOG_DESC("Invalid SuperViewChangeReq: invalid hash")
                               << LOG_KV("highHash", m_highestBlock.hash().abridged())
                               << LOG_KV("reqHash", superViewChangeReq->block_hash.abridged())
                               << LOG_KV("height", superViewChangeReq->height)
                               << LOG_KV("INFO", oss.str());
        return CheckResult::INVALID;
    }
    return CheckResult::VALID;
}


bool GroupPBFTEngine::handleSuperCommitReq(
    std::shared_ptr<SuperCommitReq> superCommitReq, PBFTMsgPacket const& pbftMsg)
{
    bool valid = decodeToRequests(superCommitReq, ref(pbftMsg.data));
    if (!valid)
    {
        return false;
    }
    auto zoneId = getZoneByNodeIndex(superCommitReq->idx);
    std::ostringstream oss;
    commonLogWhenHandleMsg(oss, "handleSuperCommitReq", zoneId, superCommitReq, pbftMsg);
    CheckResult ret = isValidSuperCommitReq(superCommitReq, zoneId, oss);
    if (ret == CheckResult::INVALID)
    {
        return false;
    }
    m_groupPBFTReqCache->addSuperCommitReq(superCommitReq, zoneId);
    if (ret == CheckResult::FUTURE)
    {
        return true;
    }
    checkSuperReqAndCommitBlock();
    GPBFTENGINE_LOG(INFO) << LOG_DESC("handleSuperCommitMsg succ") << LOG_KV("INFO", oss.str());
    return true;
}


void GroupPBFTEngine::updateConsensusStatus()
{
    updateCurrentHash(m_highestBlock.hash());
    PBFTEngine::updateBasicStatus();
    GPBFTENGINE_LOG(DEBUG) << LOG_DESC("updateConsensusStatus:")
                           << LOG_KV("leader", getLeader().second)
                           << LOG_KV("consZone", getConsensusZone(m_highestBlock.number()))
                           << LOG_KV("zone", m_zoneId) << LOG_KV("idx", nodeIdx());
}

bool GroupPBFTEngine::reportForEmptyBlock()
{
    if (m_groupPBFTReqCache->prepareCache()->view != m_view)
    {
        PBFTENGINE_LOG(DEBUG) << LOG_DESC("checkSuperReqAndCommitBlock: InvalidView")
                              << LOG_KV("prepView", m_groupPBFTReqCache->prepareCache()->view)
                              << LOG_KV("view", m_view)
                              << LOG_KV("prepHeight", m_groupPBFTReqCache->prepareCache()->height)
                              << LOG_KV("hash",
                                     m_groupPBFTReqCache->prepareCache()->block_hash.abridged())
                              << LOG_KV("nodeIdx", nodeIdx())
                              << LOG_KV("myNode", m_keyPair.pub().abridged());
        return false;
    }
    updateCurrentHash(m_groupPBFTReqCache->prepareCache()->block_hash);
    m_groupSwitchCycle = m_FaultTolerance + 1;
    PBFTEngine::updateBasicStatus();
    // reset m_lastTimeout
    m_lastTimeout = false;
    // delete cache
    m_groupPBFTReqCache->delCache(m_groupPBFTReqCache->prepareCache()->block_hash);
    // delete future cache
    m_groupPBFTReqCache->removeInvalidFutureCache(m_highestBlock);
    return true;
}

void GroupPBFTEngine::checkSuperReqAndCommitBlock()
{
    if (!collectEnoughSuperCommitReq())
    {
        return;
    }
    // empty block
    if (m_groupPBFTReqCache->prepareCache()->pBlock->getTransactionSize() == 0)
    {
        // check view
        bool ret = reportForEmptyBlock();
        if (!ret)
        {
            return;
        }
        m_emptyBlockGenerated();
        GPBFTENGINE_LOG(INFO) << LOG_DESC("CommitEmptyBlock succ")
                              << LOG_KV("updatedBlockHash", m_currentBlockHash)
                              << LOG_KV("curLeader", getLeader().second);
        return;
    }
    auto superCommitSize = m_groupPBFTReqCache->getSizeFromCache(
        m_groupPBFTReqCache->prepareCache()->block_hash, m_groupPBFTReqCache->superCommitCache());
    PBFTEngine::checkAndCommitBlock(superCommitSize);
}

void GroupPBFTEngine::checkTimeout()
{
    bool flag = false;
    {
        Guard l(m_mutex);
        if (!m_timeManager.isTimeout())
        {
            return;
        }
        auto orgChangeCycle = m_timeManager.m_changeCycle;

        // timeout caused by non-fastviewchange
        if (m_timeManager.m_lastConsensusTime != 0)
        {
            m_timeManager.updateChangeCycle();
        }
        // timeout caused by fast view change
        m_toView += 1;
        flag = true;
        m_leaderFailed = true;
        m_groupPBFTReqCache->removeInvalidViewChange(m_toView, m_highestBlock);
        m_blockSync->noteSealingBlockNumber(m_blockChain->number());
        m_timeManager.m_lastConsensusTime = utcTime();
        // trigger global view change
        if (m_timeManager.m_changeCycle > 0 &&
            m_timeManager.m_changeCycle % (m_groupSwitchCycle) == 0 &&
            orgChangeCycle != m_timeManager.m_changeCycle)
        {
            if (m_groupSwitchCycle > 2)
            {
                m_groupSwitchCycle = m_groupSwitchCycle / 2;
            }
            else
            {
                m_groupSwitchCycle = 1;
            }
            broadcastGlobalViewChangeReq();
            checkAndChangeView();
            return;
        }
        else if (locatedInConsensusZone() || m_fastViewChange)
        {
            broadcastViewChangeReq();
        }
        checkAndChangeView();
    }
    if (flag && m_onViewChange)
    {
        m_onViewChange();
    }
}

std::shared_ptr<PBFTMsg> GroupPBFTEngine::handleMsg(std::string& key, PBFTMsgPacket const& pbftMsg)
{
    bool succ = false;
    std::shared_ptr<PBFTMsg> pbftPacket = nullptr;
    switch (pbftMsg.packet_id)
    {
    case GroupPBFTPacketType::SuperSignReqPacket:
    {
        std::shared_ptr<SuperSignReq> superSignReq = std::make_shared<SuperSignReq>();
        succ = handleSuperSignReq(superSignReq, pbftMsg);
        key = superSignReq->uniqueKey();
        pbftPacket = superSignReq;
        break;
    }
    case GroupPBFTPacketType::SuperCommitReqPacket:
    {
        std::shared_ptr<SuperCommitReq> superCommitReq = std::make_shared<SuperCommitReq>();
        succ = handleSuperCommitReq(superCommitReq, pbftMsg);
        key = superCommitReq->uniqueKey();
        pbftPacket = superCommitReq;
        break;
    }
    case GroupPBFTPacketType::SuperViewChangeReqPacket:
    {
        std::shared_ptr<SuperViewChangeReq> superViewChangeReq =
            m_groupPBFTMsgFactory->buildSuperViewChangeReq();
        succ = handleSuperViewChangeReq(superViewChangeReq, pbftMsg);
        key = superViewChangeReq->uniqueKey();
        pbftPacket = superViewChangeReq;
        break;
    }
    default:
    {
        return PBFTEngine::handleMsg(key, pbftMsg);
    }
    }
    if (!succ)
    {
        return nullptr;
    }
    return pbftPacket;
}

bool GroupPBFTEngine::checkBlock(Block const& block)
{
    if (block.blockHeader().number() <= m_blockChain->number())
    {
        return false;
    }
    resetConfig();
    auto sealers = sealerList();
    /// ignore the genesis block
    if (block.blockHeader().number() == 0)
    {
        return true;
    }
    {
        if (sealers != block.blockHeader().sealerList())
        {
            GPBFTENGINE_LOG(ERROR)
                << LOG_DESC("checkBlock: wrong sealers") << LOG_KV("Nsealer", sealers.size())
                << LOG_KV("NBlockSealer", block.blockHeader().sealerList().size())
                << LOG_KV("hash", block.blockHeader().hash().abridged())
                << LOG_KV("nodeIdx", nodeIdx()) << LOG_KV("myNode", m_keyPair.pub().abridged());
            return false;
        }
    }
    return true;
}
}  // namespace consensus
}  // namespace dev