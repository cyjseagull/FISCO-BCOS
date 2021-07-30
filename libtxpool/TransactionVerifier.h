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
 * @brief : implementation of transaction verify
 * @file: TransactionVerifier.h
 * @author: yujiechen
 * @date: 2021-07-30
 */

#pragma once
#include "libtxpool/TransactionVerifierInterface.h"

namespace dev
{
namespace txpool
{
class TransactionVerifier : public TransactionVerifierInterface,
                            public std::enable_shared_from_this<TransactionVerifier>
{
public:
    using Ptr = std::shared_ptr<TransactionVerifier>;
    TransactionVerifier();
    ~TransactionVerifier() override {}

    // the common batchVerifyTxs(without GPU)
    void batchVerifyTxs(dev::eth::TransactionsPtr _txs) override;

protected:
    virtual void verifyTxs(dev::eth::TransactionsPtr _txs);
};
}  // namespace txpool
}  // namespace dev