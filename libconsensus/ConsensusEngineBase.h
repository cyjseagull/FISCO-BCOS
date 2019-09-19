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
 * @brief : implementation for the base of the PBFT consensus
 * @file: ConsensusEngineBase.h
 * @author: yujiechen
 * @date: 2018-09-28
 */
#pragma once
#include "Common.h"
#include "ConsensusInterface.h"
#include <libblockchain/BlockChainInterface.h>
#include <libblockverifier/BlockVerifierInterface.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/Worker.h>
#include <libethcore/BlockFactory.h>
#include <libp2p/P2PInterface.h>
#include <libp2p/P2PMessage.h>
#include <libp2p/P2PMessageFactory.h>
#include <libp2p/P2PSession.h>
#include <libsync/SyncInterface.h>
#include <libsync/SyncStatus.h>
#include <libtxpool/TxPoolInterface.h>

namespace dev
{
namespace consensus
{
class ConsensusEngineBase : public Worker, virtual public ConsensusInterface
{
public:
    using Ptr = std::shared_ptr<ConsensusEngineBase>;
    ConsensusEngineBase(std::shared_ptr<dev::p2p::P2PInterface> _service,
        std::shared_ptr<dev::txpool::TxPoolInterface> _txPool,
        std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain,
        std::shared_ptr<dev::sync::SyncInterface> _blockSync,
        std::shared_ptr<dev::blockverifier::BlockVerifierInterface> _blockVerifier,
        PROTOCOL_ID const& _protocolId, KeyPair const& _keyPair,
        dev::h512s const& _sealerList = dev::h512s())
      : Worker("ConsensusEngine", 0),
        m_service(_service),
        m_txPool(_txPool),
        m_blockChain(_blockChain),
        m_blockSync(_blockSync),
        m_blockVerifier(_blockVerifier),
        m_consensusBlockNumber(0),
        m_protocolId(_protocolId),
        m_keyPair(_keyPair),
        m_sealerList(_sealerList)
    {
        assert(m_service && m_txPool && m_blockChain && m_blockSync && m_blockVerifier);
        if (m_protocolId == 0)
            BOOST_THROW_EXCEPTION(dev::eth::InvalidProtocolID()
                                  << errinfo_comment("Protocol id must be larger than 0"));
        m_groupId = dev::eth::getGroupAndProtocol(m_protocolId).first;
        std::sort(m_sealerList.begin(), m_sealerList.end());
        m_blockSync->registerNodeIdFilterHandler(boost::bind(
            &ConsensusEngineBase::NodeIdFilterHandler<std::set<dev::p2p::NodeID> const&>, this,
            _1));
    }

    void start() override;
    void stop() override;
    virtual ~ConsensusEngineBase() { stop(); }

    /// get sealer list
    dev::h512s sealerList() const override
    {
        ReadGuard l(m_sealerListMutex);
        return m_sealerList;
    }
    /// append sealer
    void appendSealer(h512 const& _sealer) override
    {
        {
            WriteGuard l(m_sealerListMutex);
            m_sealerList.push_back(_sealer);
        }
        resetConfig();
    }

    const std::string consensusStatus() override
    {
        Json::Value status_obj;
        getBasicConsensusStatus(status_obj);
        Json::FastWriter fastWriter;
        std::string status_str = fastWriter.write(status_obj);
        return status_str;
    }
    /// get status of consensus
    void getBasicConsensusStatus(Json::Value& status_obj) const
    {
        status_obj["nodeNum"] = IDXTYPE(m_nodeNum);
        status_obj["node_index"] = IDXTYPE(m_idx);
        status_obj["max_faulty_leader"] = IDXTYPE(m_f);
        status_obj["consensusedBlockNumber"] = int64_t(m_consensusBlockNumber);
        status_obj["highestblockNumber"] = m_highestBlock.number();
        status_obj["highestblockHash"] = "0x" + toHex(m_highestBlock.hash());
        status_obj["groupId"] = m_groupId;
        status_obj["protocolId"] = m_protocolId;
        status_obj["accountType"] = NodeAccountType(m_accountType);
        status_obj["cfgErr"] = bool(m_cfgErr);
        status_obj["omitEmptyBlock"] = m_omitEmptyBlock;
        status_obj["nodeId"] = toHex(m_keyPair.pub());
        {
            int i = 0;
            ReadGuard l(m_sealerListMutex);
            for (auto sealer : m_sealerList)
            {
                status_obj["sealer." + toString(i)] = toHex(sealer);
                i++;
            }
        }
        status_obj["allowFutureBlocks"] = m_allowFutureBlocks;
    }

    /// protocol id used when register handler to p2p module
    PROTOCOL_ID const& protocolId() const override { return m_protocolId; }
    GROUP_ID groupId() const override { return m_groupId; }
    /// get account type
    ///@return NodeAccountType::SealerAccount: the node can generate and execute block
    ///@return NodeAccountType::ObserveAccout: the node can only sync block from other nodes
    NodeAccountType accountType() override { return m_accountType; }
    /// set the node account type
    void setNodeAccountType(dev::consensus::NodeAccountType const& _accountType) override
    {
        m_accountType = _accountType;
    }
    /// get the node index if the node is a sealer
    IDXTYPE nodeIdx() const override { return m_idx; }

    bool const& allowFutureBlocks() const { return m_allowFutureBlocks; }
    void setAllowFutureBlocks(bool isAllowFutureBlocks)
    {
        m_allowFutureBlocks = isAllowFutureBlocks;
    }

    virtual IDXTYPE minValidNodes() const { return m_nodeNum - m_f; }
    /// update the context of PBFT after commit a block into the block-chain
    virtual void reportBlock(dev::eth::Block const&) override {}

    /// obtain maxBlockTransactions
    uint64_t maxBlockTransactions() override { return m_maxBlockTransactions; }

    void setBlockFactory(std::shared_ptr<dev::eth::BlockFactory> blockFactory) override
    {
        m_blockFactory = blockFactory;
    }

    /// get the node id of specified sealer according to its index
    /// @param index: the index of the node
    /// @return h512(): the node is not in the sealer list
    /// @return node id: the node id of the node
    dev::network::NodeID getSealerByIndex(size_t const& index) const
    {
        ReadGuard l(m_sealerListMutex);
        if (index < m_sealerList.size())
            return m_sealerList[index];
        return dev::network::NodeID();
    }

    bool getNodeIDByIndex(h512& nodeID, const IDXTYPE& idx) const;

    /// get the index of specified sealer according to its node id
    /// @param nodeId: the node id of the sealer
    /// @return : 1. >0: the index of the sealer
    ///           2. equal to -1: the node is not a sealer(not exists in sealer list)
    virtual ssize_t getIndexBySealer(dev::network::NodeID const& nodeId)
    {
        ReadGuard l(m_sealerListMutex);
        ssize_t index = -1;
        for (size_t i = 0; i < m_sealerList.size(); ++i)
        {
            if (m_sealerList[i] == nodeId)
            {
                index = i;
                break;
            }
        }
        return index;
    }

    template <typename T>
    dev::p2p::NodeIDs NodeIdFilterHandler(T const& peers)
    {
        dev::p2p::NodeIDs nodeList;
        dev::p2p::NodeID selectedNode;
        // add the child node
        RecursiveFilterChildNode(nodeList, m_idx, peers);
        // add the parent node
        size_t parentIdx = (m_idx - 1) / m_broadcastNodes;
        // the parentNode is the node-self
        if (parentIdx == m_idx)
        {
            return nodeList;
        }
        // the parentNode exists in the peer list
        if (getNodeIDByIndex(selectedNode, parentIdx) && peers.count(selectedNode))
        {
#if 0
            ENGINE_LOG(DEBUG) << LOG_DESC("NodeIdFilterHandler")
                                  << LOG_KV("chosedParentNode", selectedNode.abridged())
                                  << LOG_KV("chosedIdx", parentIdx);
#endif
            nodeList.push_back(selectedNode);
        }
        // the parentNode doesn't exist in the peer list
        else
        {
            while (parentIdx != 0)
            {
                parentIdx = (parentIdx - 1) / m_broadcastNodes;
                if (getNodeIDByIndex(selectedNode, parentIdx) && peers.count(selectedNode))
                {
#if 0
                    ENGINE_LOG(DEBUG) << LOG_DESC("NodeIdFilterHandler")
                                          << LOG_KV("chosedParentNode", selectedNode.abridged())
                                          << LOG_KV("chosedIdx", parentIdx);
#endif
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
#if 0
                ENGINE_LOG(DEBUG) << LOG_DESC("NodeIdFilterHandler:RecursiveFilterChildNode")
                                      << LOG_KV("chosedNode", selectedNode.abridged())
                                      << LOG_KV("chosedIdx", expectedIdx);
#endif
                nodeList.push_back(selectedNode);
            }
            // selected the child
            else
            {
                RecursiveFilterChildNode(nodeList, expectedIdx, peers);
            }
        }
    }

    virtual bool isSyncingHigherBlock(dev::eth::BlockNumber blkNum) const
    {
        if (m_blockSync->isSyncing() && blkNum <= m_blockSync->status().knownHighestNumber)
        {
            return true;
        }
        return false;
    }

protected:
    virtual void resetConfig();
    void dropHandledTransactions(dev::eth::Block const& block) { m_txPool->dropBlockTrans(block); }
    /**
     * @brief : the message received from the network is valid or not?
     *      invalid cases: 1. received data is empty
     *                     2. the message is not sended by sealers
     *                     3. the message is not receivied by sealers
     *                     4. the message is sended by the node-self
     * @param message : message constructed from data received from the network
     * @param session : the session related to the network data(can get informations about the
     * sender)
     * @return true : the network-received message is valid
     * @return false: the network-received message is invalid
     */
    virtual bool isValidReq(dev::p2p::P2PMessage::Ptr message,
        std::shared_ptr<dev::p2p::P2PSession> session, ssize_t& peerIndex)
    {
        /// check message size
        if (message->buffer()->size() <= 0)
            return false;
        /// check whether in the sealer list
        peerIndex = getIndexBySealer(session->nodeID());
        if (peerIndex < 0)
        {
            ENGINE_LOG(TRACE) << LOG_DESC(
                "isValidReq: Recv PBFT msg from unkown peer:" + session->nodeID().abridged());
            return false;
        }
        /// check whether this node is in the sealer list
        dev::network::NodeID node_id;
        bool is_sealer = getNodeIDByIndex(node_id, nodeIdx());
        if (!is_sealer || session->nodeID() == node_id)
            return false;
        return true;
    }

    /**
     * @brief: decode the network-received message to corresponding message(PBFTMsgPacket)
     *
     * @tparam T: the type of corresponding message
     * @param req : constructed object from network-received message
     * @param message : network-received(received from service of p2p module)
     * @param session : session related with the received-message(can obtain information of
     * sender-peer)
     * @return true : decode succeed
     * @return false : decode failed
     */
    template <class T>
    inline bool decodeToRequests(std::shared_ptr<T> req,
        std::shared_ptr<dev::p2p::P2PMessage> message,
        std::shared_ptr<dev::p2p::P2PSession> session)
    {
        ssize_t peer_index = 0;
        bool valid = isValidReq(message, session, peer_index);
        if (valid)
        {
            valid = decodeToRequests(req, ref(*(message->buffer())));
            if (valid)
                req->setOtherField(
                    peer_index, session->nodeID(), session->session()->nodeIPEndpoint().name());
        }
        return valid;
    }

    /**
     * @brief : decode the given data to object
     * @tparam T : type of the object obtained from the given data
     * @param req : the object obtained from the given data
     * @param data : data need to be decoded into object
     * @return true : decode succeed
     * @return false : decode failed
     */
    template <class T>
    inline bool decodeToRequests(std::shared_ptr<T> req, bytesConstRef data)
    {
        try
        {
            assert(req);
            req->decode(data);
            return true;
        }
        catch (std::exception& e)
        {
            ENGINE_LOG(DEBUG) << "[decodeToRequests] Invalid network-received packet"
                              << LOG_KV("EINFO", e.what());
            return false;
        }
    }

    dev::blockverifier::ExecutiveContext::Ptr executeBlock(std::shared_ptr<dev::eth::Block> block);
    virtual void checkBlockValid(dev::eth::Block const& block);

    virtual void updateConsensusNodeList();
    virtual void updateNodeListInP2P(dev::h512s const& nodeList);

    /// set the max number of transactions in a block
    virtual void updateMaxBlockTransactions()
    {
        /// update m_maxBlockTransactions stored in sealer when reporting a new block
        std::string ret = m_blockChain->getSystemConfigByKey("tx_count_limit");
        {
            m_maxBlockTransactions = boost::lexical_cast<uint64_t>(ret);
        }
        ENGINE_LOG(DEBUG) << LOG_DESC("resetConfig: updateMaxBlockTransactions")
                          << LOG_KV("txCountLimit", m_maxBlockTransactions);
    }

    // encode the given message into P2PMessage
    template <typename T>
    dev::p2p::P2PMessage::Ptr encodeToP2PMessage(std::shared_ptr<T> message)
    {
        dev::p2p::P2PMessage::Ptr p2pMessage = std::dynamic_pointer_cast<dev::p2p::P2PMessage>(
            m_service->p2pMessageFactory()->buildMessage());
        std::shared_ptr<bytes> p_encodeData = std::make_shared<dev::bytes>();
        message->encode(*p_encodeData);
        p2pMessage->setBuffer(p_encodeData);
        p2pMessage->setProtocolID(m_protocolId);
        // set packetType
        p2pMessage->setPacketType(message->type());
        return p2pMessage;
    }

    bool checkSigList(
        std::vector<std::pair<IDXTYPE, Signature>> const& sigList, h256 const& blockHash);

    void printNetworkInfo();

    void updateInNetworkInfo(uint8_t const& packetType, uint64_t const& length);
    void updateOutNetworkInfo(
        uint8_t const& packetType, uint64_t sessionSize, uint64_t const& length);

    virtual dev::h512s consensusList() const { return sealerList(); }

private:
    bool blockExists(h256 const& blockHash)
    {
        if (m_blockChain->getBlockByHash(blockHash) == nullptr)
            return false;
        return true;
    }

protected:
    std::atomic<uint64_t> m_maxBlockTransactions = {1000};
    /// p2p service handler
    std::shared_ptr<dev::p2p::P2PInterface> m_service;
    /// transaction pool handler
    std::shared_ptr<dev::txpool::TxPoolInterface> m_txPool;
    /// handler of the block chain module
    std::shared_ptr<dev::blockchain::BlockChainInterface> m_blockChain;
    /// handler of the block-sync module
    std::shared_ptr<dev::sync::SyncInterface> m_blockSync;
    /// handler of the block-verifier module
    std::shared_ptr<dev::blockverifier::BlockVerifierInterface> m_blockVerifier;

    // the block which is waiting consensus
    std::atomic<int64_t> m_consensusBlockNumber = {0};
    /// the latest block header
    dev::eth::BlockHeader m_highestBlock;
    /// total number of nodes
    std::atomic<IDXTYPE> m_nodeNum = {0};
    /// at-least number of valid nodes
    std::atomic<IDXTYPE> m_f = {0};

    PROTOCOL_ID m_protocolId;
    GROUP_ID m_groupId;
    /// type of this node (SealerAccount or ObserverAccount)
    std::atomic<NodeAccountType> m_accountType = {NodeAccountType::ObserverAccount};
    /// index of this node
    std::atomic<IDXTYPE> m_idx = {0};
    KeyPair m_keyPair;

    /// sealer list
    mutable SharedMutex m_sealerListMutex;
    dev::h512s m_sealerList;

    /// allow future blocks or not
    bool m_allowFutureBlocks = true;
    bool m_startConsensusEngine = false;

    /// node list record when P2P last update
    std::string m_lastNodeList;
    std::atomic<IDXTYPE> m_connectedNode = {0};

    /// whether to omit empty block
    bool m_omitEmptyBlock = true;
    std::atomic_bool m_cfgErr = {false};

    std::shared_ptr<dev::eth::BlockFactory> m_blockFactory = nullptr;
    ssize_t m_broadcastNodes = 3;

    // for network statistic
    // map between packet id and the statisticInfo(packetCount, packetSize)
    mutable SharedMutex x_inInfo;
    std::map<uint8_t, std::pair<uint64_t, uint64_t>> m_inInfo;
    mutable SharedMutex x_outInfo;
    std::map<uint8_t, std::pair<uint64_t, uint64_t>> m_outInfo;
};
}  // namespace consensus
}  // namespace dev
