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
 * @file block_bench.cpp
 */
#include <libprotocol/Block.h>
#include <libutilities/Common.h>
#include <memory>
using namespace std;
using namespace bcos::protocol;
using namespace bcos;
using namespace bcos::crypto;

shared_ptr<Transactions> fakeTransactions(size_t _num, int64_t _currentBlockNumber)
{
    shared_ptr<Transactions> txs = make_shared<Transactions>();
    for (size_t i = 0; i < _num; ++i)
    {
        /// Transaction tx(ref(c_txBytes), CheckTransaction::Everything);
        u256 value = u256(100);
        u256 gas = u256(100000000);
        u256 gasPrice = u256(0);
        Address dst = toAddress(KeyPair::create().pub());
        std::string str = "test transaction";
        for (int j = 0; j < 100; j++)
        {
            str += "test transaction";
        }
        bytes data(str.begin(), str.end());
        Transaction::Ptr tx = std::make_shared<Transaction>(value, gasPrice, gas, dst, data);
        KeyPair sigKeyPair = KeyPair::create();
        tx->setNonce(tx->nonce() + utcTime());
        tx->setBlockLimit(u256(_currentBlockNumber) + 500);
        tx->setRpcTx(true);
        std::shared_ptr<crypto::Signature> sig =
            bcos::crypto::Sign(sigKeyPair.secret(), tx->hash(WithoutSignature));
        /// update the signature of transaction
        tx->updateSignature(sig);
        // std::pair<h256, Address> ret = txPool->submit(tx);
        txs->emplace_back(tx);
    }
    return txs;
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

    std::vector<Block::Ptr> blockVector;
    auto startTime = utcTimeUs();
    for (int i = 0; i < perfRound; i++)
    {
        Block::Ptr block = std::make_shared<Block>();
        block->setTransactions(fakeTransactions(txSize, i));
        blockVector.push_back(block);
    }
    auto endTime = utcTimeUs();
    std::cout << "Fake transaction success, time cost: " << (endTime - startTime) << std::endl;
    // test encode
    startTime = utcTimeUs();
    std::vector<bytes> encodedDataVec;
    for (int i = 0; i < perfRound; i++)
    {
        bytes encodedData;
        blockVector[i]->encode(encodedData);
        encodedDataVec.push_back(encodedData);
    }
    endTime = utcTimeUs();
    blockVector.clear();
    std::cout << "Block encode time: " << (endTime - startTime) << " us" << std::endl;
    std::cout << "Block encode time averageTime: " << (endTime - startTime) / perfRound << " us"
              << std::endl;
    // test decode
    startTime = utcTimeUs();
    for (int i = 0; i < perfRound; i++)
    {
        std::shared_ptr<Block> block = std::make_shared<Block>();
        bytesConstRef blockData = bytesConstRef(encodedDataVec[i].data(), encodedDataVec[i].size());
        std::cout << "#### blockData size:" << blockData.size() << std::endl;
        block->decode(blockData, CheckTransaction::None, true, false);
    }
    endTime = utcTimeUs();
    std::cout << "Block decode time: " << (endTime - startTime) << " us" << std::endl;
    std::cout << "Block decode time averageTime: " << (endTime - startTime) / perfRound << " us"
              << std::endl;
}