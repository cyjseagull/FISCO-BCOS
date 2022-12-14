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
 * @file CipherMatchPrecompiled.h
 * @author: yujiechen
 * @date 2022-12-14
 */
#pragma once
#include "../../vm/Precompiled.h"
#include "bcos-executor/src/precompiled/common/Common.h"
#include "wedpr-crypto/wedpr_ffi_c_equality.h"

namespace bcos::precompiled
{
#if 0
contract CipherMatchPrecompiled
{
    function bls128CipherMatch(bytes memory cipher, bytes memory trapdoor) public virtual view returns(bool);
}
#endif
class CipherMatchPrecompiled : public Precompiled
{
public:
    using Ptr = std::shared_ptr<CipherMatchPrecompiled>;
    CipherMatchPrecompiled(bcos::crypto::Hash::Ptr _hashImpl);
    ~CipherMatchPrecompiled() override{};

    std::shared_ptr<PrecompiledExecResult> call(
        std::shared_ptr<executor::TransactionExecutive> _executive,
        PrecompiledExecResult::Ptr _callParameters) override;

private:
    void callCipherMatch(CodecWrapper const& _codec, bytesConstRef _paramData,
        PrecompiledExecResult::Ptr _callResult);
};
}  // namespace bcos::precompiled