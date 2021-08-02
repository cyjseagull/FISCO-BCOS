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
 * @brief : implementation of transaction GPU verify
 * @file: TransactionGPUVerifier.h
 * @author: yujiechen
 * @date: 2021-08-02
 */
#pragma once
#include "RapidSV/gsv_wrapper.h"
#include "TransactionVerifier.h"
#include "libdevcrypto/SM2Signature.h"
namespace dev
{
namespace txpool
{
class TransactionGPUVerifier : public TransactionVerifier
{
public:
    TransactionGPUVerifier() { GSV_verify_init(); }
    ~TransactionGPUVerifier() { GSV_verify_close(); }

protected:
    void verifyTxs(dev::eth::TransactionsPtr _txs) override;

private:
    void binToWords(uint32_t* _dst, size_t _distSize, byte const* _data, size_t _size);
    void calculateHash(dev::eth::TransactionsPtr _txs);
    std::vector<gsv_verify_t> generateSignatureList(dev::eth::TransactionsPtr _txs);
    bool generatePublicKey(
        gsv_verify_t& _signature, std::shared_ptr<dev::crypto::SM2Signature> _sm2Signature);
};
}  // namespace txpool
}  // namespace dev
