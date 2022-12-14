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
 *
 * @file CipherMatchPrecompiled.cpp
 * @author: yujiechen
 * @date 2022-12-14
 */
#include "CipherMatchPrecompiled.h"
#include "bcos-codec/wrapper/CodecWrapper.h"
#include "bcos-executor/src/precompiled/common/Utilities.h"

using namespace bcos::precompiled;
using namespace bcos::executor;

// wedpr_pairing_bls128_cipher_match(cipher, trapdoor);
const char* const BLS128_CIPHER_MATCH = "bls128CipherMatch(bytes,bytes)";

CipherMatchPrecompiled::CipherMatchPrecompiled(bcos::crypto::Hash::Ptr _hashImpl)
  : Precompiled(_hashImpl)
{
    name2Selector[BLS128_CIPHER_MATCH] = getFuncSelector(BLS128_CIPHER_MATCH, _hashImpl);
}

std::shared_ptr<PrecompiledExecResult> CipherMatchPrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive,
    PrecompiledExecResult::Ptr _callParameters)
{
    auto funcSelector = getParamFunc(_callParameters->input());
    auto paramData = _callParameters->params();
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();
    gasPricer->setMemUsed(paramData.size());
    if (name2Selector[BLS128_CIPHER_MATCH] == funcSelector)
    {
        callCipherMatch(codec, paramData, _callParameters);
    }
    else
    {
        // no defined function
        PRECOMPILED_LOG(INFO) << LOG_DESC("CipherMatchPrecompiled: undefined method")
                              << LOG_KV("funcSelector", std::to_string(funcSelector));
        BOOST_THROW_EXCEPTION(
            bcos::protocol::PrecompiledError("CipherMatchPrecompiled call undefined function!"));
    }
    gasPricer->updateMemUsed(_callParameters->m_execResult.size());
    _callParameters->setGasLeft(_callParameters->m_gasLeft - gasPricer->calTotalGas());
    return _callParameters;
}

void CipherMatchPrecompiled::callCipherMatch(
    CodecWrapper const& _codec, bytesConstRef _paramData, PrecompiledExecResult::Ptr _callResult)
{
    bool result = false;
    try
    {
        bytes cipher;
        bytes trapdoor;
        _codec.decode(_paramData, cipher, trapdoor);
        CInputBuffer cipherInput{(const char*)cipher.data(), cipher.size()};
        CInputBuffer trapdoorInput{(const char*)trapdoor.data(), trapdoor.size()};
        auto ret = wedpr_pairing_bls128_peks_test(&cipherInput, &trapdoorInput);
        if (ret == 0)
        {
            result = true;
        }
    }
    catch (std::exception const& e)
    {
        PRECOMPILED_LOG(WARNING) << LOG_DESC("callCipherMatch exception")
                                 << LOG_KV("error", boost::diagnostic_information(e));
    }
    _callResult->setExecResult(_codec.encode(result));
    PRECOMPILED_LOG(DEBUG) << LOG_DESC("callCipherMatch: ") << result;
}