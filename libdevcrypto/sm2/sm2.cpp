/*
    This file is part of fisco-bcos.

    fisco-bcos is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    fisco-bcos is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with fisco-bcos.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * @file: sm2.cpp
 * @author: websterchen
 *
 * @date: 2018
 */
#include "sm2.h"
#include "crypto/ec/ec_lcl.h"
#include "internal/sm3.h"
#include "libdevcore/CommonData.h"
#include <libdevcore/Guards.h>
#include <openssl/evp.h>

#define SM3_DIGEST_LENGTH 32

using namespace std;
using namespace dev;

bool SM2::sign(const char* _originalData, int _originalDataLen, const string& _privateKeyHex,
    unsigned char* _r, unsigned char* _s)
{
    bool lresult = false;
    // create EC_GROUP
    EC_GROUP* sm2Group = EC_GROUP_new_by_curve_name(NID_sm2);
    BIGNUM* privateKey = NULL;

    EC_KEY* sm2Key = NULL;

    ECDSA_SIG* sig = NULL;
    EC_POINT* point = NULL;

    const char* userId = "1234567812345678";

    int i = 0;
    int len = 0;
    if (!BN_hex2bn(&privateKey, _privateKeyHex.data()))
    {
        CRYPTO_LOG(ERROR) << "[SM2:sign] ERROR of BN_hex2bn privateKey: " << _privateKeyHex;
        goto done;
    }
    sm2Key = EC_KEY_new();
    if (sm2Key == NULL)
    {
        CRYPTO_LOG(ERROR) << "[SM2::sign] ERROR of EC_KEY_new";
        goto done;
    }
    if (!EC_KEY_set_group(sm2Key, sm2Group))
    {
        CRYPTO_LOG(ERROR) << "[SM2::sign] ERROR of EC_KEY_set_group";
        goto done;
    }
    if (!EC_KEY_set_private_key(sm2Key, privateKey))
    {
        CRYPTO_LOG(ERROR) << "[SM2::sign] ERROR of EC_KEY_set_private_key";
        goto done;
    }
    point = EC_POINT_new(sm2Group);
    if (!point)
    {
        CRYPTO_LOG(ERROR) << "[SM2::sign] ERROR of EC_POINT_new";
        goto done;
    }

    if (!EC_POINT_mul(sm2Group, point, privateKey, NULL, NULL, NULL))
    {
        CRYPTO_LOG(ERROR) << "[SM2::sign] ERROR of EC_POINT_mul";
        goto done;
    }
    if (!EC_KEY_set_public_key(sm2Key, point))
    {
        CRYPTO_LOG(ERROR) << "[SM2::sign] ERROR of EC_KEY_set_public_key";
        goto done;
    }
    sig = sm2_do_sign(sm2Key, EVP_sm3(), (const uint8_t*)userId, (size_t)strlen(userId),
        (const uint8_t*)_originalData, _originalDataLen);
    if (sig == NULL)
    {
        CRYPTO_LOG(ERROR) << "[SM2::sign] ERROR of sm2_do_sign";
        goto done;
    }
    len = BN_bn2bin(sig->r, _r);
    for (i = 31; len > 0 && len != 32; --len, --i)
    {
        _r[i] = _r[len - 1];
    }
    for (; i >= 0 && len != 32; --i)
    {
        _r[i] = 0;
    }
    len = BN_bn2bin(sig->s, _s);
    for (i = 31; len > 0 && len != 32; --len, --i)
    {
        _s[i] = _s[len - 1];
    }
    for (; i >= 0 && len != 32; --i)
    {
        _s[i] = 0;
    }
    lresult = true;
done:
    if (sig)
    {
        ECDSA_SIG_free(sig);
    }
    if (sm2Key)
    {
        EC_KEY_free(sm2Key);
    }
    if (privateKey)
    {
        BN_free(privateKey);
    }
    return lresult;
}

int SM2::verify(const unsigned char* _signData, size_t, const unsigned char* _originalData,
    size_t _originalDataLen, const char* _publicKey)
{  // _publicKey length must 64, start with 4
    EC_KEY* sm2Key = NULL;
    EC_POINT* point = NULL;
    ECDSA_SIG* signData = NULL;
    int ok = 0;
    EC_GROUP* sm2Group = EC_GROUP_new_by_curve_name(NID_sm2);
    auto pubHex = toHex(_publicKey, _publicKey + 64, "04");
    auto rHex = toHex(_signData, _signData + 32, "");
    auto sHex = toHex(_signData + 32, _signData + 64, "");

    const char* userId = "1234567812345678";
    point = EC_POINT_new(sm2Group);
    if (!point)
    {
        CRYPTO_LOG(ERROR) << "[SM2::verify] ERROR of EC_POINT_new";
        goto done;
    }
    if (!EC_POINT_hex2point(sm2Group, pubHex.c_str(), point, NULL))
    {
        CRYPTO_LOG(ERROR) << "[SM2::veify] ERROR of Verify EC_POINT_hex2point";
        goto done;
    }

    sm2Key = EC_KEY_new();
    if (sm2Key == NULL)
    {
        CRYPTO_LOG(ERROR) << "[SM2::verify] ERROR of EC_KEY_new";
        goto done;
    }

    if (!EC_KEY_set_group(sm2Key, sm2Group))
    {
        CRYPTO_LOG(ERROR) << "[SM2::verify] ERROR of EC_KEY_set_group";
        goto done;
    }
    if (!EC_KEY_set_public_key(sm2Key, point))
    {
        CRYPTO_LOG(ERROR) << "[SM2::verify] ERROR of EC_KEY_set_public_key";
        goto done;
    }
    /*Now Verify it*/
    signData = ECDSA_SIG_new();
    if (signData == NULL)
    {
        CRYPTO_LOG(ERROR) << "[SM2::veify] ERR_R_MALLOC_FAILURE";
        goto done;
    }
    signData->r = BN_bin2bn(_signData, 32, NULL);
    if (signData->r == NULL)
    {
        CRYPTO_LOG(ERROR) << "[SM2::veify] ERROR of BN_bin2bn r";
        goto done;
    }
    signData->s = BN_bin2bn(_signData + 32, 32, NULL);
    if (signData->s == NULL)
    {
        CRYPTO_LOG(ERROR) << "[SM2::veify] ERROR BN_bin2bn s";
        goto done;
    }
    ok = sm2_do_verify(sm2Key, EVP_sm3(), signData, (const uint8_t*)userId, strlen(userId),
        _originalData, _originalDataLen);
done:
    if (signData)
    {
        ECDSA_SIG_free(signData);
    }
    if (point)
    {
        EC_POINT_free(point);
    }
    if (sm2Key)
    {
        EC_KEY_free(sm2Key);
    }
    return ok;
}

string SM2::priToPub(const string& pri)
{
    EC_KEY* sm2Key = NULL;
    EC_POINT* pubPoint = NULL;
    const EC_GROUP* sm2Group = NULL;
    string pubKey = "";
    BIGNUM* privateKey = NULL;
    BN_CTX* ctx;
    char* pub = NULL;
    ctx = BN_CTX_new();
    BN_hex2bn(&privateKey, (const char*)pri.c_str());
    sm2Key = EC_KEY_new_by_curve_name(NID_sm2);
    if (!EC_KEY_set_private_key(sm2Key, privateKey))
    {
        CRYPTO_LOG(ERROR) << "[SM2::priToPub] Error PriToPub EC_KEY_set_private_key";
        goto err;
    }
    sm2Group = EC_KEY_get0_group(sm2Key);
    pubPoint = EC_POINT_new(sm2Group);

    if (!EC_POINT_mul(sm2Group, pubPoint, privateKey, NULL, NULL, NULL))
    {
        CRYPTO_LOG(ERROR) << "[SM2::priToPub] Error of PriToPub EC_POINT_mul";
        goto err;
    }

    pub = EC_POINT_point2hex(sm2Group, pubPoint, POINT_CONVERSION_UNCOMPRESSED, ctx);
    pubKey = pub;
    pubKey = pubKey.substr(2, 128);
    pubKey = strlower((char*)pubKey.c_str());
err:
    if (pub)
    {
        OPENSSL_free(pub);
    }
    if (ctx)
    {
        BN_CTX_free(ctx);
    }
    if (sm2Key)
    {
        EC_KEY_free(sm2Key);
    }
    if (pubPoint)
    {
        EC_POINT_free(pubPoint);
    }
    if (privateKey)
    {
        BN_free(privateKey);
    }
    return pubKey;
}

char* SM2::strlower(char* s)
{
    char* str;
    str = s;
    while (*str != '\0')
    {
        if (*str >= 'A' && *str <= 'Z')
        {
            *str += 'a' - 'A';
        }
        str++;
    }
    return s;
}

string SM2::ascii2hex(const char* chs, int len)
{
    char hex[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

    char* ascii = (char*)calloc(len * 3 + 1, sizeof(char));  // calloc ascii

    int i = 0;
    while (i < len)
    {
        int b = chs[i] & 0x000000ff;
        ascii[i * 2] = hex[b / 16];
        ascii[i * 2 + 1] = hex[b % 16];
        ++i;
    }
    string str = ascii;
    free(ascii);
    return str;
}

SM2& SM2::getInstance()
{
    static SM2 sm2;
    return sm2;
}