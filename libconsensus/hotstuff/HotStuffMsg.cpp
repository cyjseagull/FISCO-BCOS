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
 * @file: HotStuffMsg.cpp
 * @author: yujiechen
 * @date: 2019-8-17
 */

#include "HotStuffMsg.h"
#include <libconsensus/Common.h>
using namespace dev;
using namespace dev::consensus;

HotStuffMsg::HotStuffMsg(KeyPair const& keyPair, int const& _type, IDXTYPE const& _idx,
    h256 const& _blockHash, dev::eth::BlockNumber const& _blockHeight, VIEWTYPE const& _view)
  : m_type(_type), m_idx(_idx), m_blockHash(_blockHash), m_blockHeight(_blockHeight), m_view(_view)
{
    m_timestamp = utcTime();
    m_patialSig = dev::sign(keyPair.secret(), calSignatureContent());
}

// encode HotStuffMsg to bytes
void HotStuffMsg::encode(bytes& encodeData)
{
    RLPStream hotStuffStream;
    convertFieldsToRLPStream(hotStuffStream);
    hotStuffStream.swapOut(encodeData);
}

// decode HotStuffMsg from the given data
void HotStuffMsg::decode(bytesConstRef data)
{
    RLP hotStuffRLP(data);
    populateFieldsFromRLP(hotStuffRLP);
}

// get the content of signature
h256 HotStuffMsg::calSignatureContent()
{
    RLPStream stream;
    stream << m_type << m_view << m_idx << m_blockHash << m_blockHeight << m_timestamp;
    return dev::sha3(stream.out());
}

// covert members of HotStuffMsg to RLPStream
void HotStuffMsg::convertFieldsToRLPStream(RLPStream& _s) const
{
    _s << m_type << m_idx << m_view << m_timestamp << m_blockHash << m_patialSig.asBytes();
}

// populate members from the given RLP
void HotStuffMsg::populateFieldsFromRLP(RLP const& rlp)
{
    try
    {
        int field = 0;
        m_type = rlp[field = 0].toInt();
        m_idx = rlp[field = 1].toInt<IDXTYPE>();
        m_view = rlp[field = 2].toInt<VIEWTYPE>();
        m_timestamp = rlp[field = 3].toInt<uint64_t>();
        m_blockHash = rlp[field = 4].toHash<h256>(RLP::VeryStrict);
        m_patialSig = Signature(rlp[field = 5].toBytesConstRef());
    }
    catch (Exception const& e)
    {
        HOTSTUFF_LOG(ERROR) << LOG_DESC("populate fields from RLP failed")
                            << LOG_KV("errorInfo", boost::diagnostic_information(e));
        throw;
    }
}

HotStuffPrepareMsg::HotStuffPrepareMsg(KeyPair const& _keyPair, int const& _type,
    IDXTYPE const& _idx, h256 const& _blockHash, dev::eth::BlockNumber const& _blockHeight,
    VIEWTYPE const& _view, VIEWTYPE const& justifyView)
  : HotStuffNewViewMsg(_keyPair, _type, _idx, _blockHash, _blockHeight, _view, justifyView)
{}

HotStuffPrepareMsg::HotStuffPrepareMsg(KeyPair const& _keyPair, HotStuffPrepareMsg::Ptr prepareMsg)
  : HotStuffNewViewMsg(_keyPair, prepareMsg->type(), prepareMsg->idx(), prepareMsg->blockHash(),
        prepareMsg->blockHeight(), prepareMsg->view(), prepareMsg->justifyView())
{
    m_pBlock = std::make_shared<dev::eth::Block>(*prepareMsg->getBlock());
}

void HotStuffPrepareMsg::convertFieldsToRLPStream(RLPStream& _s) const
{
    HotStuffNewViewMsg::convertFieldsToRLPStream(_s);
    _s << m_blockData;
}

void HotStuffPrepareMsg::populateFieldsFromRLP(RLP const& rlp)
{
    HotStuffNewViewMsg::populateFieldsFromRLP(rlp);
    m_blockData = rlp[7].toBytes();
}

QuorumCert::QuorumCert(KeyPair const& _keyPair, int const& _type, IDXTYPE const& _idx,
    h256 const& _blockHash, dev::eth::BlockNumber const& _blockHeight, VIEWTYPE const& _view)
  : HotStuffMsg(_keyPair, _type, _idx, _blockHash, _blockHeight, _view)
{}

void QuorumCert::convertFieldsToRLPStream(RLPStream& _s) const
{
    HotStuffMsg::convertFieldsToRLPStream(_s);
    _s.appendVector(m_sigList);
}

void QuorumCert::populateFieldsFromRLP(RLP const& rlp)
{
    HotStuffMsg::populateFieldsFromRLP(rlp);
    m_sigList = rlp[6].toVector<std::pair<IDXTYPE, Signature>>();
}
