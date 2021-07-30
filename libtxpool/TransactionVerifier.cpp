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
 * @file: TransactionVerifier.cpp
 * @author: yujiechen
 * @date: 2021-07-30
 */

#include "TransactionVerifier.h"
#include <tbb/parallel_for.h>

using namespace dev;
using namespace dev::eth;
using namespace dev::txpool;
TransactionVerifier::TransactionVerifier() {}
void TransactionVerifier::batchVerifyTxs(TransactionsPtr _txs)
{
    try
    {
        verifyTxs(_txs);
        for (size_t i = 0; i < _txs->size(); i++)
        {
            auto tx = (*_txs)[i];
            // invalid transaction for invalid signature
            if (tx->invalid() && m_receiptNotifier)
            {
                LOG(INFO) << LOG_DESC("verify transaction exception:") << i
                          << ", tx:" << tx->hash().abridged();
                m_receiptNotifier(tx, ImportResult::Malformed);
                continue;
            }
            if (m_verifierAndSubmitHandler)
            {
                m_verifierAndSubmitHandler(tx);
            }
        }
    }
    catch (std::exception const& e)
    {
        LOG(WARNING) << LOG_DESC("batchVerifyTxs exception")
                     << LOG_KV("reason", boost::diagnostic_information(e));
    }
}

void TransactionVerifier::verifyTxs(TransactionsPtr _txs)
{
    LOG(INFO) << LOG_DESC("batchVerifyTxs") << LOG_KV("txsSize", _txs->size());
    // parallel verify transaction before import
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, _txs->size()), [&](const tbb::blocked_range<size_t>& _r) {
            for (size_t i = _r.begin(); i != _r.end(); i++)
            {
                auto tx = (*_txs)[i];
                try
                {
                    if (m_checker && !m_checker(tx->hash()))
                    {
                        // verify the signature
                        tx->sender();
                    }
                }
                catch (std::exception const& _e)
                {
                    tx->setInvalid(true);
                    LOG(WARNING) << LOG_DESC("verify sender for tx failed")
                                 << LOG_KV("reason", boost::diagnostic_information(_e))
                                 << LOG_KV("hash", tx->hash().abridged());
                }
            }
        });
}
