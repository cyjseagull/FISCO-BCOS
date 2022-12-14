/**
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */
#include "bcos-executor/src/precompiled/extension/CipherMatchPrecompiled.h"
#include "../mock/MockLedger.h"
#include "bcos-codec/abi/ContractABICodec.h"
#include "bcos-executor/src/executive/BlockContext.h"
#include "bcos-executor/src/executive/TransactionExecutive.h"
#include "bcos-executor/src/precompiled/common/Common.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-utilities/Exceptions.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <json/json.h>


using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;

namespace bcos::test
{
struct CipherMatchPrecompiledFixture
{
    CipherMatchPrecompiledFixture()
    {
        m_hashImpl = std::make_shared<bcos::crypto::Keccak256>();
        cipherMatchPrecompiled = std::make_shared<CipherMatchPrecompiled>(m_hashImpl);
        m_ledgerCache = std::make_shared<LedgerCache>(std::make_shared<MockLedger>());
        m_blockContext = std::make_shared<BlockContext>(nullptr, m_ledgerCache, m_hashImpl, 0,
            h256(), utcTime(), 0, FiscoBcosScheduleV4, false, false);
        std::shared_ptr<wasm::GasInjector> gasInjector = nullptr;
        m_executive = std::make_shared<TransactionExecutive>(
            std::weak_ptr<BlockContext>(m_blockContext), "", 100, 0, gasInjector);
    }

    ~CipherMatchPrecompiledFixture() {}

    LedgerCache::Ptr m_ledgerCache;
    bcos::crypto::Hash::Ptr m_hashImpl;
    BlockContext::Ptr m_blockContext;
    TransactionExecutive::Ptr m_executive;
    CipherMatchPrecompiled::Ptr cipherMatchPrecompiled;
};
BOOST_FIXTURE_TEST_SUITE(test_CipherMatchPrecompiled, CipherMatchPrecompiledFixture)

BOOST_AUTO_TEST_CASE(TestCipherMatch)
{
    CipherMatchPrecompiledFixture fixture;
    auto hashImpl = fixture.m_hashImpl;
    // bls128CipherMatch
    std::string input = "zhangsan";
    bcos::bytes trapdoor = *fromHexString(
        "903b2ae12c3de650f05d6a6adaa7546d7ea0fefe6772c620ea8b6cd45a7e4dae9b0a3cca1a5b3dd8414df0f9a7"
        "980d80");
    bcos::bytes cipher = *fromHexString(
        "896dbc5689abbb2201ed970e2a6c2d211d098c0826a27eb1dedbef5fad47a1a6a2150434bbe28b90d1d9798e90"
        "e2de990a6bd55a9fbbcbb1fe7e71886bc0680929573d6633b31e695db7a113e2279dac8aa768d7430d74b3f009"
        "50f638157cf8157d9638b2d1ff7ec7b0c67de9d9c41367ca00d8edb329460952557d2db6357e");
    bcos::codec::abi::ContractABICodec abi(hashImpl);
    bytes in = abi.abiIn("bls128CipherMatch(bytes,bytes)", cipher, trapdoor);

    auto cipherMatchPrecompiled = fixture.cipherMatchPrecompiled;
    auto parameters = std::make_shared<PrecompiledExecResult>();
    parameters->m_input = bytesConstRef(in.data(), in.size());
    auto execResult = cipherMatchPrecompiled->call(fixture.m_executive, parameters);
    bytes out = execResult->execResult();
    bool result;
    abi.abiOut(bytesConstRef(&out), result);
    BOOST_TEST(true == result);

    // case2: mismatch
    cipher = *fromHexString(
        "896dbc5689abbb2201ed970e2a6c2d211d098c0826a27eb1dedbef5fad47a1a6a2150434bbe28b90d1d9798e90"
        "e2de990a6bd55a9fbbcbb1fe7e71886bc0680929573d6633b31e695db7a113e2279dac8aa768d7430d74b3f009"
        "50f638157cf8157d9638b2d1ff7ec7b0c67de9d9c41367ca00d8edb329460952557d2db63");
    in = abi.abiIn("bls128CipherMatch(bytes,bytes)", cipher, trapdoor);
    parameters = std::make_shared<PrecompiledExecResult>();
    parameters->m_input = bytesConstRef(in.data(), in.size());
    execResult = cipherMatchPrecompiled->call(fixture.m_executive, parameters);
    out = execResult->execResult();
    abi.abiOut(bytesConstRef(&out), result);
    BOOST_TEST(false == result);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test