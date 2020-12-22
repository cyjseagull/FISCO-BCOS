/**
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
 *
 * @file block_pb_benchmark.cpp
 */

#include <libdevcrypto/Common.h>
#include <libdevcrypto/CryptoInterface.h>
#include <libdevcrypto/Hash.h>
#include <libprotocol/proto/Block.pb.h>
#include <libutilities/Common.h>
#include <libutilities/FixedBytes.h>
#include <memory>
using namespace std;
using namespace bcos::protocol;
using namespace bcos;
using namespace bcos::crypto;

void fakePBTransactions(size_t _num, std::shared_ptr<BlockPB> block)
{
    for (size_t i = 0; i < _num; ++i)
    {
        TransactionPB* tx = block->add_transactions();
        Address dst = toAddress(KeyPair::create().pub());
        std::string str = "test transaction";
        for (int j = 0; j < 100; j++)
        {
            str += "test transaction";
        }
        bytes data(str.begin(), str.end());
        tx->set_version(0);
        tx->set_chainid(1);
        tx->set_groupid(1);
        tx->set_nonce(boost::lexical_cast<std::string>(utcTime()));
        tx->set_blocklimit(500);
        tx->set_gasprice(boost::lexical_cast<std::string>(0));
        tx->set_gas(boost::lexical_cast<std::string>(100000000));
        tx->set_inputdata(string(data.begin(), data.end()));
        tx->set_receiveaddress(string(dst.begin(), dst.end()));
        tx->set_importtime(boost::lexical_cast<std::string>(utcTime()));
        KeyPair sigKeyPair = KeyPair::create();
        std::shared_ptr<Signature> sig = bcos::crypto::Sign(sigKeyPair.secret(), keccak256("test"));
        /// update the signature of transaction
        SignaturePB* signatureData = tx->mutable_signaturedata();
        signatureData->set_r(string(sig->r.begin(), sig->r.end()));
        signatureData->set_s(string(sig->s.begin(), sig->s.end()));
    }
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cout << "Usage: ./ [txSize] [perfRound]" << std::endl;
        return 0;
    }
    int txSize = atoi(argv[1]);
    int perfRound = atoi(argv[2]);
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    auto startTime = utcTimeUs();
    std::vector<std::shared_ptr<BlockPB>> blockPBVec;
    for (int i = 0; i < perfRound; i++)
    {
        std::shared_ptr<BlockPB> blockPB = std::make_shared<BlockPB>();
        fakePBTransactions(txSize, blockPB);
        blockPBVec.push_back(blockPB);
    }
    auto endTime = utcTimeUs();
    std::cout << "Fake transaction success, time cost: " << (endTime - startTime) << std::endl;
    // test encode
    std::vector<std::string> encodedDataVec;
    encodedDataVec.resize(perfRound);
    startTime = utcTimeUs();
    for (int i = 0; i < perfRound; i++)
    {
        (blockPBVec[i])->SerializeToString(&encodedDataVec[i]);
    }
    endTime = utcTimeUs();
    std::cout << "Block encode time: " << (endTime - startTime) << " us" << std::endl;
    std::cout << "Block encode time averageTime: " << (endTime - startTime) / perfRound << " us"
              << std::endl;
    blockPBVec.clear();
    // test decode
    std::vector<BlockPB> decodedBlockPBVec;
    decodedBlockPBVec.resize(perfRound);
    startTime = utcTimeUs();
    for (int i = 0; i < perfRound; i++)
    {
        (decodedBlockPBVec[i]).ParseFromString(encodedDataVec[i]);
        std::cout << "#### blockData size:" << encodedDataVec[i].size() << std::endl;
    }
    endTime = utcTimeUs();
    std::cout << "Block decode time: " << (endTime - startTime) << " us" << std::endl;
    std::cout << "Block decode time averageTime: " << (endTime - startTime) / perfRound << " us"
              << std::endl;
    std::cout << "### transactionSize:" << decodedBlockPBVec[0].transactions_size() << std::endl;
    std::cout << "### block limit:" << decodedBlockPBVec[0].transactions(0).blocklimit()
              << std::endl;
    std::cout << "### r:" << *toHexString(decodedBlockPBVec[0].transactions(0).signaturedata().r())
              << std::endl;
}
