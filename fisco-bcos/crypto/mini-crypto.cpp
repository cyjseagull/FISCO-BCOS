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
 * @file: mini-crypto.cpp
 * @author: yujiechen
 *
 * @date: 2021
 */

#include <libdevcrypto/CryptoInterface.h>
#include <libdevcrypto/Common.h>
#include <libdevcrypto/Hash.h>
#include <libdevcrypto/SM3Hash.h>
using namespace dev::crypto;
using namespace dev;
int main(int, const char* argv[])
{
    size_t loopRound = atoi(argv[1]);
    initSMCrypto();
    g_BCOSConfig.setUseSMCrypto(true);
    KeyPair keyPair = KeyPair::create();
    getchar();
    std::cout << "###begin test" << std::endl;
    // calculate hash
    std::cout << "#### test sm3" << std::endl;
    std::string input = "test_sm3";
    for(size_t i = 0; i < loopRound; i++)
    {
        sm3(input);
    }
    std::cout << "### test sign" << std::endl;
    auto hash =  sm3(input);
    for(size_t i = 0; i < loopRound; i++)
    {
        Sign(keyPair, hash);
    }
    std::cout << "### test verify" << std::endl;
    auto signatureResult = Sign(keyPair, hash);
    for(size_t i = 0; i < loopRound; i++)
    {
        crypto::Verify(keyPair.pub(), signatureResult, hash);
    }
    std::cout << "#### test end"<<std::endl;
    getchar();
}