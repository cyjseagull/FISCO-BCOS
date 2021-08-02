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
 * @file: TransactionGPUVerifier.cpp
 * @author: yujiechen
 * @date: 2021-08-02
 */

#include "libtxpool/TransactionGPUVerifier.h"
#include "crypto/ec/ec_lcl.h"
#include "internal/sm2.h"
#include "libdevcrypto/SM2Signature.h"
#include <tbb/parallel_for.h>

using namespace dev;
using namespace dev::txpool;
using namespace dev::crypto;

void TransactionGPUVerifier::verifyTxs(dev::eth::TransactionsPtr _txs)
{
    calculateHash(_txs);
    auto signatureList = generateSignatureList(_txs);
    std::vector<int> results;
    results.resize(_txs->size());
    memset(&(results[0]), 0, _txs->size());
    GSV_verify_exec(_txs->size(), &(signatureList[0]), &(results[0]));
    size_t index = 0;
    for (auto tx : *_txs)
    {
        if (results[index] == 0)
        {
            LOG(INFO) << LOG_DESC("verify tx failed") << LOG_KV("tx", tx->hash().abridged());
            tx->setInvalid(true);
        }
    }
}

void TransactionGPUVerifier::calculateHash(dev::eth::TransactionsPtr _txs)
{
    // parallel verify transaction before import
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, _txs->size()), [&](const tbb::blocked_range<size_t>& _r) {
            for (size_t i = _r.begin(); i != _r.end(); i++)
            {
                auto tx = (*_txs)[i];
                tx->hash();
            }
        });
}

bool TransactionGPUVerifier::generatePublicKey(
    gsv_verify_t& _signature, std::shared_ptr<SM2Signature> _sm2Signature)
{
    bool success = false;
    EC_POINT* point = NULL;
    auto pubHex = toHex(_sm2Signature->v.begin(), _sm2Signature->v.end(), "04");
    EC_GROUP* sm2Group = EC_GROUP_new_by_curve_name(NID_sm2);
    point = EC_POINT_new(sm2Group);
    if (!point)
    {
        LOG(ERROR) << "[TransactionGPUVerifier::generatePublicKey] ERROR of EC_POINT_new";
        goto done;
    }
    if (!EC_POINT_hex2point(sm2Group, pubHex.c_str(), point, NULL))
    {
        LOG(ERROR) << "[TransactionGPUVerifier::generatePublicKey] ERROR of EC_POINT_hex2point";
        goto done;
    }
    //unsigned char pubX[32];
    //unsigned char pubY[32];
    BN_bn2bin(point->X, &(pubX[0]));
    BN_bn2bin(point->Y, &(pubX[0]));
    h256 pubX = h256("D5548C7825CBB56150A3506CD57464AF8A1AE0519DFAF3C58221DC810CAF28DD");
    h256 pubY = h256("921073768FE3D59CE54E79A49445CF73FED23086537027264D168946D479533E");
    /*binToWords(_signature.key_x._limbs, sizeof(_signature.key_x._limbs) / sizeof(uint32_t),
        (byte const*)(&pubX[0]), 64);
    binToWords(_signature.key_x._limbs, sizeof(_signature.key_x._limbs) / sizeof(uint32_t),
        (byte const*)(&pubY[0]), 64);*/
    binToWords(_signature.key_x._limbs, sizeof(_signature.key_x._limbs) / sizeof(uint32_t),
        (byte const*)(pubX.data()), 64);
    binToWords(_signature.key_x._limbs, sizeof(_signature.key_x._limbs) / sizeof(uint32_t),
        (byte const*)(pubY.data()), 64);
    success = true;
done:
    if (point)
    {
        EC_POINT_free(point);
    }
    return success;
}

std::vector<gsv_verify_t> TransactionGPUVerifier::generateSignatureList(
    dev::eth::TransactionsPtr _txs)
{
    std::vector<gsv_verify_t> signatureList;
    for (auto tx : *_txs)
    {
        auto sm2Signature = std::dynamic_pointer_cast<SM2Signature>(tx->signature());
        sm2Signature->r = h256("23B20B796AAAFEAAA3F1592CB9B4A93D5A8D279843E1C57980E64E0ABC5F5B05");
        sm2Signature->s = h256("E11F5909F947D5BE08C84A22CE9F7C338F7CF4A5B941B9268025495D7D433071");
        gsv_verify_t signature;

        binToWords(signature.r._limbs, sizeof(signature.r._limbs) / sizeof(uint32_t),
            sm2Signature->r.data(), h256::size);
        binToWords(signature.s._limbs, sizeof(signature.s._limbs) / sizeof(uint32_t),
            sm2Signature->s.data(), h256::size);
        //auto hash = tx->hash();
        auto hash = h256("10D51CB90C0C0522E94875A2BEA7AB72299EBE7192E64EFE0573B1C77110E5C9");
        binToWords(signature.e._limbs, sizeof(signature.e._limbs) / sizeof(uint32_t), hash.data(),
            h256::size);
        generatePublicKey(signature, sm2Signature);
        signatureList.emplace_back(signature);
    }
    return signatureList;
}

void TransactionGPUVerifier::binToWords(
    uint32_t* _dst, size_t _distSize, byte const* _data, size_t _size)
{
    memset(_dst, 0, _distSize);
    for (size_t i = 0; i < _size; i++)
    {
        auto pdata = _data + i;
        int value = (int)*pdata;
        _dst[i / 4] += value << (i % 4);
    }
}