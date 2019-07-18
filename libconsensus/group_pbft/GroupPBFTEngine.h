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
 * @file: GroupPBFTEngine.h
 * @author: yujiechen
 * @date: 2019-5-28
 */
#pragma once
#include "Common.h"
#include "GroupPBFTMsg.h"
#include "GroupPBFTMsgFactory.h"
#include "GroupPBFTReqCache.h"
#include <libconsensus/pbft/PBFTEngine.h>
namespace dev
{
namespace consensus
{
class GroupPBFTReqCache;
class GroupPBFTEngine : public PBFTEngine
{
public:
    GroupPBFTEngine(std::shared_ptr<dev::p2p::P2PInterface> service,
        std::shared_ptr<dev::txpool::TxPoolInterface> txPool,
        std::shared_ptr<dev::blockchain::BlockChainInterface> blockchain,
        std::shared_ptr<dev::sync::SyncInterface> sync,
        std::shared_ptr<dev::blockverifier::BlockVerifierInterface> blockVerifier,
        dev::PROTOCOL_ID const& protocolId, std::string const& baseDir, KeyPair const& keyPair,
        h512s const& sealerList = h512s())
      : PBFTEngine(service, txPool, blockchain, sync, blockVerifier, protocolId, baseDir, keyPair,
            sealerList)
    {
        // filter for broadcastMsgAmongGroup
        m_groupBroadcastFilter = boost::bind(&GroupPBFTEngine::filterGroupNodeByNodeID, this, _1);
        // filter for broadcastMsg and sendMsg
        m_broadcastFilter = boost::bind(&GroupPBFTEngine::getGroupIndexBySealer, this, _1);
        /// register checkSealerList to blockSync for check SealerList
        m_blockSync->registerConsensusVerifyHandler(
            boost::bind(&GroupPBFTEngine::checkBlock, this, _1));
        m_blockSync->registerNodeIdFilterHandler(boost::bind(
            &GroupPBFTEngine::NodeIdFilterHandler<std::set<dev::p2p::NodeID> const&>, this, _1));
    }

    void setGroupSize(int64_t const& groupSize)
    {
        m_configuredGroupSize = groupSize;
        GPBFTENGINE_LOG(INFO) << LOG_KV("configured groupSize", m_configuredGroupSize);
    }

    void setConsensusZoneSwitchBlockNumber(int64_t const& zoneSwitchBlocks)
    {
        m_zoneSwitchBlocks = zoneSwitchBlocks;
    }

    void start() override;

    bool broadcastViewChange(std::shared_ptr<SuperViewChangeReq> superViewChangeReq);

protected:
    /// below for grouping nodes into groups
    // reset config after commit a new block
    void resetConfig() override;

    /// below for shouldSeal
    // get current consensus zone
    virtual ZONETYPE getConsensusZone(int64_t const& blockNumber) const;
    // get leader if the current zone is the consensus zone
    std::pair<bool, IDXTYPE> getLeader() const override;
    bool isLeader();

    bool isValidLeader(std::shared_ptr<PrepareReq> const& req) const override
    {
        // this node is not located in consensus zone
        if (!locatedInConsensusZone())
        {
            ZONETYPE zoneId = req->idx / m_configuredGroupSize;
            if (locatedInConsensusZone(m_highestBlock.number(), zoneId))
            {
                return true;
            }

            GPBFTENGINE_LOG(DEBUG)
                << LOG_BADGE("isValidLeader: Prepare request is not generated by a valid leader")
                << LOG_DESC("check leader failed for the node is not in consensus zone")
                << LOG_KV("genIdx", req->idx) << LOG_KV("zoneId", zoneId)
                << LOG_KV("curBlk", m_highestBlock.number()) << LOG_KV("reqHeight", req->height)
                << LOG_KV("reqIdx", req->idx) << LOG_KV("currentGlobalView", m_globalView)
                << LOG_KV("currentLeader", getLeader().second)
                << LOG_KV("reqHash", req->block_hash.abridged());
            return false;
        }
        // this node is located in consensus zone
        return PBFTEngine::isValidLeader(req);
    }


    // forbid trigger fast viewchange when generate empty block
    bool triggerViewChangeForEmptyBlock(std::shared_ptr<PrepareReq>) override { return false; }

    // forbid trigger fast view change when handle prepare packet with empty block
    bool fastViewChangeViewForEmptyBlock(std::shared_ptr<Sealing>) override { return false; }

    bool shouldReset(std::shared_ptr<dev::eth::Block> const&) override { return false; }

    virtual bool locatedInConsensusZone(int64_t const& blockNumber, ZONETYPE const& zoneId) const;
    bool locatedInConsensusZone() const;

    // get node idx by nodeID
    virtual ssize_t getGroupIndexBySealer(dev::network::NodeID const& nodeId);
    IDXTYPE getNextLeader() const override;

    bool shouldPushMsg(byte const& packetType) override
    {
        return (packetType <= GroupPBFTPacketType::SuperViewChangeReqPacket);
    }

    void updateConsensusStatus() override;
    void updateCurrentHash(dev::h256 const& blockHash)
    {
        std::stringstream ss;
        ss << std::hex << toHex(blockHash);
        ss >> m_currentBlockHash;
    }

    bool reportForEmptyBlock();

    virtual void broadcastPrepareToOtherGroups(std::shared_ptr<PrepareReq> prepareReq);

    // broadcast message among groups
    bool broadCastMsgAmongGroups(const int& packetType, std::string const& key, bytesConstRef data,
        unsigned const& ttl = 0,
        std::unordered_set<dev::network::NodeID> const& filter =
            std::unordered_set<dev::network::NodeID>());
    // filter function when broad cast among groups
    virtual ssize_t filterGroupNodeByNodeID(dev::network::NodeID const& nodeId);


    IDXTYPE minValidNodes() const override { return m_zoneSize - m_FaultTolerance; }

    virtual ZONETYPE minValidGroups() const { return m_zoneNum - m_groupFaultTolerance; }

    virtual bool collectEnoughSignReq(bool const& equalCondition = true)
    {
        return collectEnoughReq(
            m_groupPBFTReqCache->signCache(), minValidNodes(), "collectSignReq", equalCondition);
    }
    virtual bool collectEnoughSuperSignReq(bool const& equalCondition = true)
    {
        return collectEnoughReq(m_groupPBFTReqCache->superSignCache(), minValidGroups(),
            "collectSuperSignReq", equalCondition);
    }
    virtual bool collectEnoughCommitReq(bool const& equalCondition = false)
    {
        bool ret = collectEnoughSuperSignReq(false);
        if (!ret)
        {
            return false;
        }
        return collectEnoughReq(m_groupPBFTReqCache->commitCache(), minValidNodes(),
            "collectCommitReq", equalCondition);
    }
    virtual bool collectEnoughSuperCommitReq()
    {
        bool ret = collectEnoughCommitReq(false);
        if (!ret)
        {
            return false;
        }
        return collectEnoughReq(m_groupPBFTReqCache->superCommitCache(), minValidGroups(),
            "collectSuperCommitReq", false);
    }

    virtual bool collectEnoughSuperViewChangReq()
    {
        size_t viewChangeSize = m_groupPBFTReqCache->getViewChangeSize(m_toView);
        if (viewChangeSize >= (size_t)(minValidNodes() - 1))
        {
            if (m_groupPBFTReqCache->getSuperViewChangeSize(m_toView) >= (size_t)minValidGroups())
            {
                GPBFTENGINE_LOG(INFO) << LOG_DESC(
                    "collectEnoughSuperViewChangReq: collect enough superViewChange req");
                return true;
            }
            GPBFTENGINE_LOG(DEBUG)
                << LOG_DESC("collectEnoughSuperViewChangReq: unenough superViewChangeReq")
                << LOG_KV("toView", m_toView)
                << LOG_KV("curSuperViewChangeReq",
                       m_groupPBFTReqCache->getSuperViewChangeSize(m_toView))
                << LOG_KV("requiredSuperViewChangeReq", minValidGroups())
                << LOG_KV("curZone", m_zoneId) << LOG_KV("idx", nodeIdx());
        }
        return false;
    }


    void printCollectEnoughReqLog(size_t const& cacheSize, std::string const& desc)
    {
        GPBFTENGINE_LOG(INFO) << LOG_DESC(desc) << " SUCC: "
                              << LOG_KV("hash",
                                     m_groupPBFTReqCache->prepareCache()->block_hash.abridged())
                              << LOG_KV("collectedSize", cacheSize)
                              << LOG_KV("groupIdx", m_groupIdx) << LOG_KV("zoneId", m_zoneId)
                              << LOG_KV("idx", m_idx);
    }


    template <typename T>
    bool collectEnoughReq(
        T const& cache, IDXTYPE const& minSize, std::string const& desc, bool const& equalCondition)
    {
        auto cacheSize = m_groupPBFTReqCache->getSizeFromCache(
            m_groupPBFTReqCache->prepareCache()->block_hash, cache);
        GPBFTENGINE_LOG(DEBUG) << LOG_DESC("collectEnoughReq") << LOG_DESC(desc)
                               << LOG_KV("cacheSize", cacheSize) << LOG_KV("minSize", minSize)
                               << LOG_KV("equalCondition", equalCondition);

        if (equalCondition && (cacheSize == (size_t)(minSize)))
        {
            printCollectEnoughReqLog(cacheSize, desc);
            return true;
        }
        else if (!equalCondition && (cacheSize >= (size_t)(minSize)))
        {
            printCollectEnoughReqLog(cacheSize, desc);
            return true;
        }
        return false;
    }

    // handle superSign message
    CheckResult isValidSuperSignReq(std::shared_ptr<SuperSignReq> superSignReq,
        ZONETYPE const& zoneId, std::ostringstream& oss) const
    {
        return isValidSuperReq(m_groupPBFTReqCache->superSignCache(), superSignReq, zoneId, oss);
    }

    virtual bool handleSuperSignReq(
        std::shared_ptr<SuperSignReq> superSignReq, PBFTMsgPacket const& pbftMsg);

    virtual bool broadcastSuperSignMsg();

    template <typename T>
    void commonLogWhenHandleMsg(std::ostringstream& oss, std::string const& desc,
        ZONETYPE const& zoneId, std::shared_ptr<T> superReq, PBFTMsgPacket const& pbftMsg)
    {
        oss << LOG_DESC(desc) << LOG_KV("number", superReq->height)
            << LOG_KV("curBlkNum", m_highestBlock.number())
            << LOG_KV("hash", superReq->block_hash.abridged()) << LOG_KV("genIdx", superReq->idx)
            << LOG_KV("genZone", zoneId) << LOG_KV("curView", m_view)
            << LOG_KV("curToView", m_toView) << LOG_KV("reqView", superReq->view)
            << LOG_KV("fromIdx", pbftMsg.node_idx) << LOG_KV("fromIp", pbftMsg.endpoint)
            << LOG_KV("zone", m_zoneId) << LOG_KV("idx", m_idx)
            << LOG_KV("nodeId", m_keyPair.pub().abridged());
    }

    virtual void checkAndBackupForSuperSignEnough();

    // handle superCommit message
    virtual bool handleSuperCommitReq(
        std::shared_ptr<SuperCommitReq> superCommitReq, PBFTMsgPacket const& pbftMsg);

    CheckResult isValidSuperCommitReq(std::shared_ptr<SuperCommitReq> superCommitReq,
        ZONETYPE const& zoneId, std::ostringstream& oss) const
    {
        return isValidSuperReq(
            m_groupPBFTReqCache->superCommitCache(), superCommitReq, zoneId, oss);
    }


    void checkSuperReqAndCommitBlock();
    virtual void printWhenCollectEnoughSuperReq(std::string const& desc, size_t superReqSize);
    virtual bool broadcastSuperCommitMsg();

    void checkAndCommit() override;
    void checkAndSave() override;

    // handle superViewChange message
    virtual bool handleSuperViewChangeReq(
        std::shared_ptr<SuperViewChangeReq> superViewChangeReq, PBFTMsgPacket const& pbftMsg);
    CheckResult isValidSuperViewChangeReq(std::shared_ptr<SuperViewChangeReq> superViewChangeReq,
        ZONETYPE const& zoneId, std::ostringstream const& oss);
    virtual bool broadcastSuperViewChangeReq(uint8_t type = 0);

    void checkAndChangeView() override;
    virtual void checkSuperViewChangeAndChangeView();

    std::shared_ptr<PBFTMsg> handleMsg(std::string& key, PBFTMsgPacket const& pbftMsg) override;

    CheckResult isValidPrepare(std::shared_ptr<PrepareReq> req, std::ostringstream& oss) override
    {
        CheckResult ret = PBFTEngine::isValidPrepare(req, oss);
        if (ret != CheckResult::INVALID)
        {
            broadcastPrepareToOtherGroups(req);
        }
        return ret;
    }

    void checkTimeout() override;

    // TODO: remove this logic after add at least (2*f + 1) signatures at the end of the generated
    // block
    bool checkBlock(dev::eth::Block const& block) override;

    bool shouldReportBlock(dev::eth::Block const& block) const override
    {
        if (m_currentBlockHash == 0 && m_blockChain->number() == 0)
        {
            return true;
        }
        return (m_highestBlock.number() < block.blockHeader().number());
    }

    void initPBFTCacheObject() override;
    virtual bool broadcastGlobalViewChangeReq();

    template <typename T>
    dev::p2p::NodeIDs NodeIdFilterHandler(T const& peers)
    {
        dev::p2p::NodeIDs nodeList;
        dev::p2p::NodeID selectedNode;
        // add the child node
        RecursiveFilterChildNode(nodeList, m_idx, peers);
        // add the parent node
        size_t parentIdx = m_idx / m_broadcastNodes;
        // the parentNode is the node-self
        if (parentIdx == m_idx)
        {
            return nodeList;
        }
        // the parentNode exists in the peer list
        if (getNodeIDByIndex(selectedNode, parentIdx) && peers.count(selectedNode))
        {
            GPBFTENGINE_LOG(DEBUG) << LOG_DESC("NodeIdFilterHandler")
                                   << LOG_KV("chosedParentNode", selectedNode.abridged())
                                   << LOG_KV("chosedIdx", parentIdx);
            nodeList.push_back(selectedNode);
        }
        // the parentNode doesn't exist in the peer list
        else
        {
            while (parentIdx != 0)
            {
                parentIdx /= m_broadcastNodes;
                if (getNodeIDByIndex(selectedNode, parentIdx) && peers.count(selectedNode))
                {
                    GPBFTENGINE_LOG(DEBUG) << LOG_DESC("NodeIdFilterHandler")
                                           << LOG_KV("chosedParentNode", selectedNode.abridged())
                                           << LOG_KV("chosedIdx", parentIdx);
                    nodeList.push_back(selectedNode);
                    break;
                }
            }
        }
        return nodeList;
    }

    template <typename T>
    void RecursiveFilterChildNode(
        dev::p2p::NodeIDs& nodeList, ssize_t const& startIndex, T const& peers)
    {
        dev::p2p::NodeID selectedNode;
        for (ssize_t i = 1; i <= m_broadcastNodes; i++)
        {
            ssize_t expectedIdx = startIndex * 3 + i;
            if (expectedIdx >= m_nodeNum)
            {
                break;
            }
            // the expectedNode existed in the peers
            if (getNodeIDByIndex(selectedNode, expectedIdx) && peers.count(selectedNode))
            {
                GPBFTENGINE_LOG(DEBUG) << LOG_DESC("NodeIdFilterHandler:RecursiveFilterChildNode")
                                       << LOG_KV("chosedNode", selectedNode.abridged())
                                       << LOG_KV("chosedIdx", expectedIdx);
                nodeList.push_back(selectedNode);
            }
            // selected the child
            else
            {
                RecursiveFilterChildNode(nodeList, expectedIdx, peers);
            }
        }
    }


private:
    template <class T, class S>
    CheckResult isValidSuperReq(S const& cache, std::shared_ptr<T> superReq, ZONETYPE const& zoneId,
        std::ostringstream& oss) const
    {
        if (m_groupPBFTReqCache->cacheExists(cache, superReq->block_hash, zoneId))
        {
            GPBFTENGINE_LOG(DEBUG)
                << LOG_DESC("Invalid superReq: already cached!") << LOG_KV("INFO", oss.str());
            return CheckResult::INVALID;
        }
        if (hasConsensused(superReq))
        {
            GPBFTENGINE_LOG(DEBUG)
                << LOG_DESC("Invalid superReq: already consensued!") << LOG_KV("INFO", oss.str());
            return CheckResult::INVALID;
        }
        return checkBasic(superReq, oss, false);
    }

    ZONETYPE getZoneByNodeIndex(IDXTYPE const& nodeIdx)
    {
        return (nodeIdx / m_configuredGroupSize);
    }


protected:
    // the zone that include this node
    std::atomic<ZONETYPE> m_zoneId = {0};
    // configured group size
    std::atomic<int64_t> m_configuredGroupSize = {0};
    /// group idx
    std::atomic<int64_t> m_groupIdx = {INT64_MAX};
    /// real zone Size
    std::atomic<int64_t> m_zoneSize = {0};
    /// group num
    std::atomic<int64_t> m_zoneNum = {0};
    /// maxmum fault-tolerance inner a given group
    std::atomic<int64_t> m_FaultTolerance = {0};
    std::atomic<int64_t> m_groupSwitchCycle = {0};
    /// maxmum fault-tolerance among groups
    std::atomic<int64_t> m_groupFaultTolerance = {0};
    std::atomic<int64_t> m_zoneSwitchBlocks = {0};

    std::atomic<VIEWTYPE> m_globalView = {0};
    std::atomic_bool m_lastTimeout = {false};


    // used to calculate leader, updated when commit succ
    u256 m_currentBlockHash = 0;


    mutable SharedMutex x_broadCastListAmongGroups;
    std::set<h512> m_broadCastListAmongGroups;

    // functions
    std::function<ssize_t(dev::network::NodeID const&)> m_groupBroadcastFilter = nullptr;

    // pointers
    std::shared_ptr<GroupPBFTReqCache> m_groupPBFTReqCache = nullptr;
    std::shared_ptr<GroupPBFTMsgFactory> m_groupPBFTMsgFactory = nullptr;
    ssize_t m_broadcastNodes = 3;
};
}  // namespace consensus
}  // namespace dev