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
 * @brief : interface for transaction verify
 * @file: TransactionVerifierInterface.h
 * @author: yujiechen
 * @date: 2021-07-30
 */
#pragma once
#include "libethcore/Common.h"
#include "libethcore/Transaction.h"
namespace dev
{
namespace txpool
{
class TransactionVerifierInterface
{
public:
    using Ptr = std::shared_ptr<TransactionVerifierInterface>;
    TransactionVerifierInterface() = default;
    virtual ~TransactionVerifierInterface() {}

    virtual void batchVerifyTxs(dev::eth::TransactionsPtr _txs) = 0;

    virtual void registerChecker(std::function<bool(dev::h256 const& _txHash)> _checker)
    {
        m_checker = _checker;
    }

    virtual void registerVerifierAndSubmitHandler(
        std::function<void(dev::eth::Transaction::Ptr)> _verifierAndSubmitHandler)
    {
        m_verifierAndSubmitHandler = _verifierAndSubmitHandler;
    }

    virtual void registerReceiptNotifier(
        std::function<void(dev::eth::Transaction::Ptr, dev::eth::ImportResult const&)>
            _receiptNotifier)
    {
        m_receiptNotifier = _receiptNotifier;
    }

    bool batchVerify() const { return m_batchVerify; }
    void setBatchVerify(bool _batchVerify) { m_batchVerify = _batchVerify; }

protected:
    std::function<bool(dev::h256 const&)> m_checker;
    std::function<void(dev::eth::Transaction::Ptr)> m_verifierAndSubmitHandler;
    std::function<void(dev::eth::Transaction::Ptr, dev::eth::ImportResult const&)>
        m_receiptNotifier;

    bool m_batchVerify = false;
};
}  // namespace txpool
}  // namespace dev