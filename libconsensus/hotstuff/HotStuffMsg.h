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
#pragma once
#include "Common.h"

namespace dev
{
namespace consensus
{
struct HotStuffPacketType
{
    // Prepare phase
    static const int NewViewPacket = 0x00;
    static const int PreparePacket = 0x01;

    // pre-commit phase
    static const int PrepareVotePacket = 0x02;
    static const int PrepareQCPacket = 0x03;

    // commit phase
    static const int PrecommitVotePacket = 0x04;
    static const int PrecommitQCPacket = 0x05;

    // decide phase
    static const int CommitVotePacket = 0x06;
    static const int CommitQCPacket = 0x07;

    static const int DecideVotePacket = 0x08;
};

// basic hotstuffMsg
class BasicHotStuffMsg
{
public:
    BasicHotStuffMsg() = default;
    virtual ~BasicHotStuffMsg() {}
    virtual void encode(bytes& encodeData) = 0;
    virtual void decode(bytesConstRef data) = 0;
};

/// basic hotstuff message
class HotStuffMsg : public BasicHotStuffMsg
{
public:
    using Ptr = std::shared_ptr<HotStuffMsg>;
    HotStuffMsg() = default;
    HotStuffMsg(KeyPair const& keyPair, int const& _type, IDXTYPE const& _idx,
        h256 const& _blockHash, dev::eth::BlockNumber const& _blockHeight, VIEWTYPE const& _view);

    virtual ~HotStuffMsg() {}

    // encode HotStuffMsg to bytes
    void encode(bytes& encodeData) override;
    // decode HotStuffMsg from the given data
    void decode(bytesConstRef data) override;

    // get type
    int const& type() const { return m_type; }
    // get idx
    int idx() { return m_idx; }
    // get blockHash
    h256 const& blockHash() const { return m_blockHash; }
    // get blockHeight
    dev::eth::BlockNumber const& blockHeight() const { return m_blockHeight; }
    // get view
    VIEWTYPE const& view() const { return m_view; }
    // get timestamp
    uint64_t const& timestamp() const { return m_timestamp; }
    // get signature
    Signature const& patialSig() const { return m_patialSig; }
    // get block signature
    Signature const& blockSig() const { return m_blockSig; }

    virtual h256 calSignatureContent();
    std::string uniqueKey() { return m_patialSig.hex() + m_blockSig.hex(); }

protected:
    virtual void convertFieldsToRLPStream(RLPStream& _s) const;
    virtual void populateFieldsFromRLP(RLP const& rlp);

protected:
    // packet type
    int m_type = -1;
    // nodeIdx of the node
    IDXTYPE m_idx = MAXIDX;
    // blockHash
    h256 m_blockHash = h256();
    // blockNumber
    dev::eth::BlockNumber m_blockHeight;
    // view
    VIEWTYPE m_view = MAXVIEW;
    // timestamp
    uint64_t m_timestamp = INT64_MAX;
    // signature to this message
    Signature m_patialSig = Signature();
    // signature to blockHash
    Signature m_blockSig = Signature();
};

// quorum certificate
class QuorumCert : public HotStuffMsg
{
public:
    using Ptr = std::shared_ptr<QuorumCert>;
    QuorumCert() = default;

    QuorumCert(KeyPair const& _keyPair, int const& _type, IDXTYPE const& _idx,
        h256 const& _blockHash, dev::eth::BlockNumber const& _blockHeight, VIEWTYPE const& _view);

    void setSigList(std::vector<std::pair<IDXTYPE, Signature>> const& _sigs)
    {
        m_sigList = _sigs;
        m_blockSigList.clear();
    }

    std::vector<std::pair<IDXTYPE, Signature>> const& sigList() { return m_sigList; }
    std::vector<std::pair<u256, Signature>> const& blockSigList()
    {
        if (m_blockSigList.size() > 0)
        {
            return m_blockSigList;
        }
        for (auto const& sig : m_sigList)
        {
            m_blockSigList.push_back(sig);
        }
        return m_blockSigList;
    }

protected:
    void convertFieldsToRLPStream(RLPStream& _s) const override;
    void populateFieldsFromRLP(RLP const& rlp) override;

protected:
    // signatures from (n-f) replias
    std::vector<std::pair<IDXTYPE, Signature>> m_sigList;
    std::vector<std::pair<u256, Signature>> m_blockSigList;
};

class HotStuffNewViewMsg : public HotStuffMsg
{
public:
    using Ptr = std::shared_ptr<HotStuffNewViewMsg>;

    HotStuffNewViewMsg() { m_justifyQC = std::make_shared<QuorumCert>(); }
    HotStuffNewViewMsg(KeyPair const& _keyPair, int const& _type, IDXTYPE const& _idx,
        h256 const& _blockHash, dev::eth::BlockNumber const& _blockHeight, VIEWTYPE const& _view,
        QuorumCert::Ptr _justifyQC)
    {
        m_type = _type;
        m_idx = _idx;
        m_blockHash = _blockHash;
        m_blockHeight = _blockHeight;
        m_view = _view;
        m_justifyQC = _justifyQC;
        // encode the prepareQC
        m_justifyQC->encode(m_justifyQCData);
        m_timestamp = utcTime();
        m_patialSig = dev::sign(_keyPair.secret(), calSignatureContent());
        m_blockSig = dev::sign(_keyPair.secret(), _blockHash);
    }

    virtual ~HotStuffNewViewMsg() {}
    QuorumCert::Ptr justifyQC() { return m_justifyQC; }
    VIEWTYPE const& justifyView() const { return m_justifyQC->view(); }

protected:
    void convertFieldsToRLPStream(RLPStream& _s) const override
    {
        HotStuffMsg::convertFieldsToRLPStream(_s);
        _s << m_justifyQCData;
    }
    void populateFieldsFromRLP(RLP const& rlp) override
    {
        HotStuffMsg::populateFieldsFromRLP(rlp);
        m_justifyQCData = rlp[8].toBytes();
        // decode into justifyQC
        m_justifyQC->decode(ref(m_justifyQCData));
    }

    h256 calSignatureContent() override
    {
        RLPStream stream;
        auto justifyDataHash = m_justifyQC->calSignatureContent();
        stream << m_type << m_view << m_idx << m_blockHash << m_blockHeight << m_timestamp
               << justifyDataHash;
        return dev::sha3(stream.out());
    }

protected:
    bytes m_justifyQCData;
    QuorumCert::Ptr m_justifyQC;
};

/// hotstuff prepare message
class HotStuffPrepareMsg : public HotStuffNewViewMsg
{
public:
    using Ptr = std::shared_ptr<HotStuffPrepareMsg>;

    HotStuffPrepareMsg() = default;

    HotStuffPrepareMsg(KeyPair const& _keyPair, int const& _type, IDXTYPE const& _idx,
        h256 const& _blockHash, dev::eth::BlockNumber const& _blockHeight, VIEWTYPE const& _view,
        QuorumCert::Ptr _justifyQC);

    HotStuffPrepareMsg(
        KeyPair const& _keyPair, Sealing::Ptr sealing, HotStuffPrepareMsg::Ptr prepareMsg);

    void setBlock(std::shared_ptr<dev::eth::Block> block)
    {
        m_pBlock = block;
        m_pBlock->encode(m_blockData);
    }

    void decode(bytesConstRef data) override { HotStuffMsg::decode(data); }

    std::shared_ptr<dev::eth::Block> getBlock() { return m_pBlock; }
    dev::blockverifier::ExecutiveContext::Ptr getExecContext() { return m_pExecContext; }

    bytes const& blockData() const { return m_blockData; }

protected:
    void convertFieldsToRLPStream(RLPStream& _s) const override;
    void populateFieldsFromRLP(RLP const& rlp) override;

protected:
    bytes m_blockData;

    std::shared_ptr<dev::eth::Block> m_pBlock = nullptr;
    /// execution result of block(save the execution result temporarily)
    /// no need to send or receive accross the network
    dev::blockverifier::ExecutiveContext::Ptr m_pExecContext = nullptr;
};
}  // namespace consensus
}  // namespace dev