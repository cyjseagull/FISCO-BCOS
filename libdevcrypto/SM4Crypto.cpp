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
/** @file SM4Crypto.cpp
 * @author Asherli
 * @date 2018
 */

#include "SM4Crypto.h"
#include "internal/sm4.h"
#include "libdevcrypto/Exceptions.h"
#include "openssl/modes.h"
#include <stdlib.h>
#include <string.h>

using namespace dev;
using namespace dev::crypto;
using namespace std;

string dev::crypto::sm4Encrypt(const unsigned char* _plainData, size_t _plainDataSize,
    const unsigned char* _key, size_t, const unsigned char* _ivData)
{
    int padding = _plainDataSize % 16;
    int nSize = 16 - padding;
    int inDataVLen = _plainDataSize + nSize;
    bytes inDataV(inDataVLen);
    memcpy(inDataV.data(), _plainData, _plainDataSize);
    memset(inDataV.data() + _plainDataSize, nSize, nSize);

    string enData;
    enData.resize(inDataVLen);
    SM4_KEY sm4Key;
    unsigned char* iv = (unsigned char*)malloc(16);
    std::memcpy(iv, _ivData, 16);
    SM4_set_key((const byte*)_key, &sm4Key);
    CRYPTO_cbc128_encrypt((const byte*)inDataV.data(), (byte*)enData.data(), inDataVLen,
        (const void*)&sm4Key, iv, (block128_f)SM4_encrypt);
    free(iv);
    return enData;
}

string dev::crypto::sm4Decrypt(const unsigned char* _cypherData, size_t _cypherDataSize,
    const unsigned char* _key, size_t, const unsigned char* _ivData)
{
    string deData;
    deData.resize(_cypherDataSize);
    SM4_KEY sm4Key;
    SM4_set_key(_key, &sm4Key);
    unsigned char* iv = (unsigned char*)malloc(16);
    std::memcpy(iv, _ivData, 16);
    CRYPTO_cbc128_decrypt(_cypherData, (unsigned char*)deData.data(), _cypherDataSize,
        (const void*)&sm4Key, iv, (block128_f)SM4_decrypt);
    int padding = deData.at(_cypherDataSize - 1);
    int deLen = _cypherDataSize - padding;
    free(iv);
    return deData.substr(0, deLen);
}
