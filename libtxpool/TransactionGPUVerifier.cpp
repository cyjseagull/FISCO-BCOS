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
#include "internal/sm3.h"
#include "libdevcrypto/SM2Signature.h"
#include "openssl/evp.h"
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
    memset(&(results[0]), 1, _txs->size());
    GSV_verify_exec(2, _txs->size(), &(signatureList[0]), &(results[0]));
    size_t index = 0;
    for (auto tx : *_txs)
    {
        if (results[index] != 0)
        {
            LOG(INFO) << LOG_DESC("verify tx failed") << LOG_KV("tx", tx->hash().abridged())
                      << LOG_KV("result", results[index]);
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

bool TransactionGPUVerifier::generatePublicKey(gsv_verify_t& _signature,
    std::shared_ptr<SM2Signature> _sm2Signature, h256 const& _originalData)
{
    bool success = false;
    EC_POINT* point = NULL;
    EC_KEY* sm2Key = NULL;
    SM3_CTX sm3Ctx;
    unsigned char digest[h256::size];
    size_t digestLen = sizeof(digest);
    const char* userId = "1234567812345678";

    h256 hashBytes = h256("10D51CB90C0C0522E94875A2BEA7AB72299EBE7192E64EFE0573B1C77110E5C9");
    h256 keyXBytes = h256("D5548C7825CBB56150A3506CD57464AF8A1AE0519DFAF3C58221DC810CAF28DD");
    h256 keyYBytes = h256("921073768FE3D59CE54E79A49445CF73FED23086537027264D168946D479533E");

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
    unsigned char pubX[32];
    unsigned char pubY[32];
    BN_bn2bin(point->X, &(pubX[0]));
    BN_bn2bin(point->Y, &(pubY[0]));
    binToWords(_signature.key_x._limbs, sizeof(_signature.key_x._limbs) / sizeof(uint32_t),
        (byte const*)(&pubX[0]), 32);
    binToWords(_signature.key_y._limbs, sizeof(_signature.key_y._limbs) / sizeof(uint32_t),
        (byte const*)(&pubY[0]), 32);

    LOG(INFO) << LOG_DESC("key_y: ") << toHex(&(pubY[0]), &(pubY[0]) + 32, "");
    LOG(INFO) << LOG_DESC("key_x: ") << toHex(&(pubX[0]), &(pubX[0]) + 32, "");

    sm2Key = EC_KEY_new();
    if (sm2Key == NULL)
    {
        LOG(ERROR) << "[SM2::verify] ERROR of EC_KEY_new";
        goto done;
    }

    if (!EC_KEY_set_group(sm2Key, sm2Group))
    {
        LOG(ERROR) << "[SM2::verify] ERROR of EC_KEY_set_group";
        goto done;
    }
    if (!EC_KEY_set_public_key(sm2Key, point))
    {
        LOG(ERROR) << "[SM2::verify] ERROR of EC_KEY_set_public_key";
        goto done;
    }
    memset(digest, 0x00, sizeof(digest));
    if (!sm2_compute_z_digest(digest, EVP_sm3(), (const uint8_t*)userId, strlen(userId), sm2Key))
    {
        LOG(ERROR) << LOG_DESC("Error Of Compute Z");
        goto done;
    }
    /*Now Compute Digest*/
    sm3_init(&sm3Ctx);
    sm3_update(&sm3Ctx, digest, digestLen);
    sm3_update(&sm3Ctx, _originalData.data(), h256::size);
    sm3_final(digest, &sm3Ctx);
    binToWords(_signature.e._limbs, sizeof(_signature.e._limbs) / sizeof(uint32_t), &(digest[0]),
        h256::size);

    binToWords(_signature.key_x._limbs, sizeof(_signature.key_x._limbs) / sizeof(uint32_t),
        keyXBytes.data(), h256::size);
    binToWords(_signature.key_y._limbs, sizeof(_signature.key_y._limbs) / sizeof(uint32_t),
        keyYBytes.data(), h256::size);
    binToWords(_signature.e._limbs, sizeof(_signature.e._limbs) / sizeof(uint32_t),
        hashBytes.data(), h256::size);
    success = true;
done:
    if (point)
    {
        EC_POINT_free(point);
    }
    if (sm2Key)
    {
        EC_KEY_free(sm2Key);
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
        gsv_verify_t signature;
        h256 rBytes = h256("23B20B796AAAFEAAA3F1592CB9B4A93D5A8D279843E1C57980E64E0ABC5F5B05");
        h256 sBytes = h256("E11F5909F947D5BE08C84A22CE9F7C338F7CF4A5B941B9268025495D7D433071");
        binToWords(signature.r._limbs, sizeof(signature.r._limbs) / sizeof(uint32_t), rBytes.data(),
            h256::size);
        binToWords(signature.s._limbs, sizeof(signature.s._limbs) / sizeof(uint32_t), sBytes.data(),
            h256::size);


        generatePublicKey(signature, sm2Signature, tx->hash());
        signatureList.emplace_back(signature);
    }
    return signatureList;
}

int TransactionGPUVerifier::char2int(char c)
{
    if ('0' <= c && c <= '9')
        return c - '0';
    else if ('a' <= c && c <= 'f')
        return c - 'a' + 10;
    else if ('A' <= c && c <= 'F')
        return c - 'A' + 10;
    else
    {
        printf("Invalid char: '%c'\n", c);
        exit(1);
    }
}

void TransactionGPUVerifier::hex2bn(uint32_t* _dst, size_t _dstSize, const char* _hexString)
{
    for (size_t i = 0; i < _dstSize; i++)
    {
        _dst[i] = 0;
    }
    size_t length = 0;
    while (_hexString[length] != 0)
        length++;
    for (size_t i = 0; i < length; i++)
    {
        int value = char2int(_hexString[length - i - 1]);
        _dst[i / 8] += value << i % 8 * 4;
    }
    for (size_t i = 0; i < _dstSize; i++)
    {
        LOG(INFO) << LOG_DESC("### hex2bn ") << i << LOG_DESC(": ") << _dst[i];
    }
}

void TransactionGPUVerifier::binToWords(
    uint32_t* _dst, size_t _dstSize, byte const* _data, size_t _size)
{
    memset(_dst, 0, _dstSize);
    for (size_t i = 0; i < _dstSize; i++)
    {
        _dst[i] = 0;
    }
    for (size_t i = 0; i < _size; i++)
    {
        auto value = (int)_data[_size - i - 1];
        _dst[i / 4] += value << ((i % 4) * 8);
    }
    for (size_t i = 0; i < _dstSize; i++)
    {
        LOG(INFO) << LOG_DESC("### binToWords ") << i << LOG_DESC(": ") << _dst[i];
    }
}