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
#include <libprotocol/proto/ParallelBlock.pb.h>
#include <libutilities/Common.h>
#include <libutilities/FixedBytes.h>
#include <tbb/parallel_for.h>
#include <memory>
using namespace std;
using namespace bcos::protocol;
using namespace bcos;
using namespace bcos::crypto;

void fakePBTransactions(size_t _num, std::shared_ptr<ParallelBlockPB> block)
{
    for (size_t i = 0; i < _num; ++i)
    {
        std::string* txData = block->add_transactions();
        std::shared_ptr<TransactionPB> tx = std::make_shared<TransactionPB>();
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
        tx->SerializeToString(txData);
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
    std::vector<std::shared_ptr<ParallelBlockPB>> blockPBVec;
    for (int i = 0; i < perfRound; i++)
    {
        std::shared_ptr<ParallelBlockPB> blockPB = std::make_shared<ParallelBlockPB>();
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
    std::vector<ParallelBlockPB> decodedBlockPBVec;
    decodedBlockPBVec.resize(perfRound);
    startTime = utcTimeUs();
    for (int i = 0; i < perfRound; i++)
    {
        (decodedBlockPBVec[i]).ParseFromString(encodedDataVec[i]);
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, txSize), [&](const tbb::blocked_range<size_t>& _r) {
                for (size_t j = _r.begin(); j != _r.end(); ++j)
                {
                    std::shared_ptr<TransactionPB> tx = std::make_shared<TransactionPB>();
                    tx->ParseFromString(decodedBlockPBVec[i].transactions(j));
                }
            });
    }
    endTime = utcTimeUs();
    std::cout << "Block decode time: " << (endTime - startTime) << " us" << std::endl;
    std::cout << "Block decode time averageTime: " << (endTime - startTime) / perfRound << " us"
              << std::endl;
    std::cout << "### transactionSize:" << decodedBlockPBVec[0].transactions_size() << std::endl;
}
