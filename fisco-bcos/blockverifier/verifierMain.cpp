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
 * @brief : verifierMain
 * @author: mingzhenliu
 * @date: 2018-09-21
 */
#include <leveldb/db.h>
#include <libblockchain/BlockChainImp.h>
#include <libblockverifier/BlockVerifier.h>
#include <libblockverifier/ExecutiveContextFactory.h>
#include <libdevcore/easylog.h>
#include <libdevcrypto/Common.h>
#include <libethcore/Block.h>
#include <libethcore/TransactionReceipt.h>
#include <libmptstate/MPTStateFactory.h>
#include <libstorage/LevelDBStorage.h>
#include <libstorage/MemoryTableFactory.h>
#include <libstorage/Storage.h>

using namespace dev;
INITIALIZE_EASYLOGGINGPP
int main(int argc, char* argv[])
{
    auto storagePath = std::string("test_storage/");
    boost::filesystem::create_directories(storagePath);
    leveldb::Options option;
    option.create_if_missing = true;
    option.max_open_files = 100;
    leveldb::DB* dbPtr = NULL;
    leveldb::Status s = leveldb::DB::Open(option, storagePath, &dbPtr);
    if (!s.ok())
    {
        LOG(ERROR) << "Open storage leveldb error: " << s.ToString();
    }

    auto storageDB = std::shared_ptr<leveldb::DB>(dbPtr);
    auto storage = std::make_shared<dev::storage::LevelDBStorage>();
    storage->setDB(storageDB);

    auto blockChain = std::make_shared<dev::blockchain::BlockChainImp>();
    blockChain->setStateStorage(storage);


    auto stateFactory = std::make_shared<dev::mptstate::MPTStateFactory>(
        dev::u256(0), "test_state", dev::h256(0), dev::WithExisting::Trust);

    auto executiveContextFactory = std::make_shared<dev::blockverifier::ExecutiveContextFactory>();
    executiveContextFactory->setStateFactory(stateFactory);
    executiveContextFactory->setStateStorage(storage);


    auto blockVerifier = std::make_shared<dev::blockverifier::BlockVerifier>();
    blockVerifier->setExecutiveContextFactory(executiveContextFactory);
    blockVerifier->setNumberHash(
        [blockChain](int64_t num) { return blockChain->getBlockByNumber(num)->headerHash(); });

    if (argc > 1 && std::string("insert") == argv[1])
    {
        for (int i = 0; i < 3; ++i)
        {
            auto max = blockChain->number();
            auto parentBlock = blockChain->getBlockByNumber(max);
            dev::eth::BlockHeader header;
            header.setNumber(max + 1);
            header.setParentHash(parentBlock->headerHash());
            header.setGasLimit(dev::u256(1024 * 1024 * 1024));
            header.setRoots(parentBlock->header().transactionsRoot(),
                parentBlock->header().receiptsRoot(), parentBlock->header().stateRoot());
            dev::eth::Block block;
            block.setBlockHeader(header);
            LOG(INFO) << "max " << max << " parentHeader " << parentBlock->header() << " header "
                      << header;

            dev::bytes rlpBytes = dev::fromHex(
                "0xf92cc4a002b6cb747cb0285a58d4a949cf3ccb9cb6c0c2a43a3568f95fcd3c902a0d822b85174876"
                "e7ff8609184e729fff8202048080b92c4b6060604052341561000c57fe5b604051612bab380380612b"
                "ab833981016040528080518201919050505b6000600090505b81518110156100c05760008054806001"
                "01828161004d91906100c8565b916000526020600020900160005b848481518110151561006957fe5b"
                "90602001906020020151909190916101000a81548173ffffffffffffffffffffffffffffffffffffff"
                "ff021916908373ffffffffffffffffffffffffffffffffffffffff160217905550505b806001019050"
                "610030565b5b5050610119565b8154818355818115116100ef57818360005260206000209182019101"
                "6100ee91906100f4565b5b505050565b61011691905b80821115610112576000816000905550600101"
                "6100fa565b5090565b90565b612a83806101286000396000f30060606040526000357c010000000000"
                "0000000000000000000000000000000000000000000000900463ffffffff1680633ffefe4e14620000"
                "6c57806363a9c3d714620000cf57806394cf795e1462000120578063a4286264146200019b578063fa"
                "69efbd14620002e4575bfe5b34156200007557fe5b6200008d60048080359060200190919050506200"
                "030d565b604051808273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffff"
                "ffffffffffffffffffffff16815260200191505060405180910390f35b3415620000d857fe5b620001"
                "06600480803573ffffffffffffffffffffffffffffffffffffffff1690602001909190505062000378"
                "565b604051808215151515815260200191505060405180910390f35b34156200012957fe5b62000133"
                "62000422565b6040518080602001828103825283818151815260200191508051906020019060200280"
                "83836000831462000188575b8051825260208311156200018857602082019150602081019050602083"
                "03925062000162565b5050509050019250505060405180910390f35b3415620001a457fe5b620002a2"
                "600480803590602001908201803590602001908080601f016020809104026020016040519081016040"
                "5280939291908181526020018383808284378201915050505050509190803590602001908201803590"
                "602001908080601f016020809104026020016040519081016040528093929190818152602001838380"
                "8284378201915050505050509190803590602001908201803590602001908080601f01602080910402"
                "6020016040519081016040528093929190818152602001838380828437820191505050505050919080"
                "3560ff1690602001909190803560001916906020019091908035600019169060200190919050506200"
                "04bb565b604051808273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffff"
                "ffffffffffffffffffffff16815260200191505060405180910390f35b3415620002ed57fe5b620002"
                "f762000767565b6040518082815260200191505060405180910390f35b600060006000805490509050"
                "8083101562000367576000838154811015156200033257fe5b906000526020600020900160005b9054"
                "906101000a900473ffffffffffffffffffffffffffffffffffffffff16915062000372565b60009150"
                "62000372565b5b50919050565b60006000600090505b60008054905081101562000417576000818154"
                "811015156200039f57fe5b906000526020600020900160005b9054906101000a900473ffffffffffff"
                "ffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168373ffff"
                "ffffffffffffffffffffffffffffffffffff1614156200040a57600191506200041c565b5b80600101"
                "905062000381565b600091505b50919050565b6200042c62000775565b600080548060200260200160"
                "4051908101604052809291908181526020018280548015620004b05760200282019190600052602060"
                "0020905b8160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffff"
                "ffffffffffffffffffffffffffffffffffff168152602001906001019080831162000465575b505050"
                "505090505b90565b600060008787878787873033620004d162000789565b8080602001806020018060"
                "20018960ff1660ff168152602001886000191660001916815260200187600019166000191681526020"
                "018673ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffff"
                "ffffffff1681526020018573ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffff"
                "ffffffffffffffffffffffffff16815260200184810384528c81815181526020019150805190602001"
                "9080838360008314620005b4575b805182526020831115620005b45760208201915060208101905060"
                "20830392506200058e565b505050905090810190601f168015620005e1578082038051600183602003"
                "6101000a031916815260200191505b5084810383528b81815181526020019150805190602001908083"
                "83600083146200062c575b8051825260208311156200062c5760208201915060208101905060208303"
                "925062000606565b505050905090810190601f16801562000659578082038051600183602003610100"
                "0a031916815260200191505b5084810382528a81815181526020019150805190602001908083836000"
                "8314620006a4575b805182526020831115620006a45760208201915060208101905060208303925062"
                "00067e565b505050905090810190601f168015620006d15780820380516001836020036101000a0319"
                "16815260200191505b509b505050505050505050505050604051809103906000f0801515620006f357"
                "fe5b90507f8b94c7f6b3fadc764673ea85b4bfef3e17ce928d13e51b818ddfa891ad0f1fcc81604051"
                "808273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffff"
                "ffffffff16815260200191505060405180910390a18091505b509695505050505050565b6000600080"
                "54905090505b90565b602060405190810160405280600081525090565b6040516122bd806200079b83"
                "3901905600606060405234156200000d57fe5b604051620022bd380380620022bd8339810160405280"
                "8051820191906020018051820191906020018051820191906020018051906020019091908051906020"
                "0190919080519060200190919080519060200190919080519060200190919050505b81600760006101"
                "000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffff"
                "ffffffffffffffffffffff160217905550620000ce816200066264010000000002620017b717640100"
                "0000009004565b1562000444578760009080519060200190620000ec92919062000748565b50866001"
                "90805190602001906200010592919062000748565b5085600290805190602001906200011e92919062"
                "000748565b5060038054806001018281620001359190620007cf565b91600052602060002090602091"
                "828204019190065b87909190916101000a81548160ff021916908360ff160217905550506004805480"
                "60010182816200017c91906200080c565b916000526020600020900160005b86909190915090600019"
                "1690555060058054806001018281620001ae91906200080c565b916000526020600020900160005b85"
                "9091909150906000191690555060068054806001018281620001e091906200083b565b916000526020"
                "600020900160005b83909190916101000a81548173ffffffffffffffffffffffffffffffffffffffff"
                "021916908373ffffffffffffffffffffffffffffffffffffffff160217905550507f6001b9d5afd61e"
                "6053e8a73e30790ef8240d919a754055049131521927fbe21188888888888888604051808060200180"
                "602001806020018860ff1660ff16815260200187600019166000191681526020018660001916600019"
                "1681526020018573ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffff"
                "ffffffffffffffffff16815260200184810384528b8181518152602001915080519060200190808383"
                "600083146200030c575b8051825260208311156200030c576020820191506020810190506020830392"
                "50620002e6565b505050905090810190601f168015620003395780820380516001836020036101000a"
                "031916815260200191505b5084810383528a8181518152602001915080519060200190808383600083"
                "1462000384575b80518252602083111562000384576020820191506020810190506020830392506200"
                "035e565b505050905090810190601f168015620003b15780820380516001836020036101000a031916"
                "815260200191505b508481038252898181518152602001915080519060200190808383600083146200"
                "03fc575b805182526020831115620003fc57602082019150602081019050602083039250620003d656"
                "5b505050905090810190601f168015620004295780820380516001836020036101000a031916815260"
                "200191505b509a505050505050505050505060405180910390a162000653565b7f45cb885dcdccd3be"
                "ce3cb78b963aec501f1cf9756fd93584f0643c7a953343108888888888888860405180806020018060"
                "2001806020018860ff1660ff1681526020018760001916600019168152602001866000191660001916"
                "81526020018573ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffff"
                "ffffffffffffffff16815260200184810384528b818151815260200191508051906020019080838360"
                "00831462000520575b8051825260208311156200052057602082019150602081019050602083039250"
                "620004fa565b505050905090810190601f1680156200054d5780820380516001836020036101000a03"
                "1916815260200191505b5084810383528a818151815260200191508051906020019080838360008314"
                "62000598575b8051825260208311156200059857602082019150602081019050602083039250620005"
                "72565b505050905090810190601f168015620005c55780820380516001836020036101000a03191681"
                "5260200191505b50848103825289818151815260200191508051906020019080838360008314620006"
                "10575b8051825260208311156200061057602082019150602081019050602083039250620005ea565b"
                "505050905090810190601f1680156200063d5780820380516001836020036101000a03191681526020"
                "0191505b509a505050505050505050505060405180910390a15b5b5050505050505050620008ba565b"
                "6000600760009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffff"
                "ffffffffffffffffffffffffffffffffff166363a9c3d7836000604051602001526040518263ffffff"
                "ff167c0100000000000000000000000000000000000000000000000000000000028152600401808273"
                "ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffff"
                "ff168152602001915050602060405180830381600087803b15156200072757fe5b6102c65a03f11515"
                "6200073657fe5b5050506040518051905090505b919050565b82805460018160011615610100020316"
                "6002900490600052602060002090601f016020900481019282601f106200078b57805160ff19168380"
                "01178555620007bc565b82800160010185558215620007bc579182015b82811115620007bb57825182"
                "55916020019190600101906200079e565b5b509050620007cb91906200086a565b5090565b8"
                "154818355818115116200080757601f016020900481601f01602090048360005260206000209182019"
                "1016200080691906200086a565b5b505050565b8154818355818115116200083657818360005260206"
                "00020918201910162000835919062000892565b5b505050565b8154818355818115116200086557818"
                "3600052602060002091820191016200086491906200086a565b5b505050565b6200088f91905b80821"
                "1156200088b57600081600090555060010162000871565b5090565b90565b620008b791905b8082111"
                "5620008b357600081600090555060010162000899565b5090565b90565b6119f380620008ca6000396"
                "000f30060606040523615610076576000357c010000000000000000000000000000000000000000000"
                "0000000000000900463ffffffff1680633b52ebd01461007857806348f85bce146100ca578063596f2"
                "1f81461011f57806394cf795e14610404578063c7eaf9b414610479578063dc58ab1114610512575bf"
                "e5b341561008057fe5b610088610560565b604051808273fffffffffffffffffffffffffffffffffff"
                "fffff1673ffffffffffffffffffffffffffffffffffffffff16815260200191505060405180910390f"
                "35b34156100d257fe5b610105600480803560ff1690602001909190803560001916906020019091908"
                "03560001916906020019091905050610586565b6040518082151515158152602001915050604051809"
                "10390f35b341561012757fe5b61012f610fd2565b60405180806020018060200180602001806020018"
                "0602001806020018060200188810388528f81815181526020019150805190602001908083836000831"
                "4610196575b80518252602083111561019657602082019150602081019050602083039250610172565"
                "b505050905090810190601f1680156101c25780820380516001836020036101000a031916815260200"
                "191505b5088810387528e81815181526020019150805190602001908083836000831461020a575b805"
                "18252602083111561020a576020820191506020810190506020830392506101e6565b5050509050908"
                "10190601f1680156102365780820380516001836020036101000a031916815260200191505b5088810"
                "386528d81815181526020019150805190602001908083836000831461027e575b80518252602083111"
                "561027e5760208201915060208101905060208303925061025a565b505050905090810190601f16801"
                "56102aa5780820380516001836020036101000a031916815260200191505b5088810385528c8181518"
                "152602001915080519060200190602002808383600083146102f5575b8051825260208311156102f55"
                "76020820191506020810190506020830392506102d1565b50505090500188810384528b81815181526"
                "0200191508051906020019060200280838360008314610345575b80518252602083111561034557602"
                "082019150602081019050602083039250610321565b50505090500188810383528a818151815260200"
                "191508051906020019060200280838360008314610395575b805182526020831115610395576020820"
                "19150602081019050602083039250610371565b5050509050018881038252898181518152602001915"
                "080519060200190602002808383600083146103e5575b8051825260208311156103e55760208201915"
                "06020810190506020830392506103c1565b5050509050019e505050505050505050505050505050604"
                "05180910390f35b341561040c57fe5b610414611513565b60405180806020018281038252838181518"
                "15260200191508051906020019060200280838360008314610466575b8051825260208311156104665"
                "7602082019150602081019050602083039250610442565b5050509050019250505060405180910390f"
                "35b341561048157fe5b61048961170e565b60405180806020018281038252838181518152602001915"
                "080519060200190808383600083146104d8575b8051825260208311156104d85760208201915060208"
                "10190506020830392506104b4565b505050905090810190601f1680156105045780820380516001836"
                "020036101000a031916815260200191505b509250505060405180910390f35b341561051a57fe5b610"
                "546600480803573ffffffffffffffffffffffffffffffffffffffff169060200190919050506117b75"
                "65b604051808215151515815260200191505060405180910390f35b600760009054906101000a90047"
                "3ffffffffffffffffffffffffffffffffffffffff1681565b60006000600090505b600680549050811"
                "015610a70576006818154811015156105ab57fe5b906000526020600020900160005b9054906101000"
                "a900473ffffffffffffffffffffffffffffffffffffffff1673fffffffffffffffffffffffffffffff"
                "fffffffff163373ffffffffffffffffffffffffffffffffffffffff161415610a62578460ff1660038"
                "281548110151561061e57fe5b90600052602060002090602091828204019190065b9054906101000a9"
                "00460ff1660ff161480156106745750836000191660048281548110151561065e57fe5b90600052602"
                "0600020900160005b505460001916145b80156106a55750826000191660058281548110151561068f5"
                "7fe5b906000526020600020900160005b505460001916145b156108b1577fcb265a1c827beb2fd9947"
                "f2a8d4725c8918f266faf695a26a53ac662e42a2f3f600060016002888888604051808060200180602"
                "001806020018760ff1660ff16815260200186600019166000191681526020018560001916600019168"
                "15260200184810384528a8181546001816001161561010002031660029004815260200191508054600"
                "1816001161561010002031660029004801561078d5780601f106107625761010080835404028352916"
                "020019161078d565b820191906000526020600020905b8154815290600101906020018083116107705"
                "7829003601f168201915b5050848103835289818154600181600116156101000203166002900481526"
                "0200191508054600181600116156101000203166002900480156108105780601f106107e5576101008"
                "08354040283529160200191610810565b820191906000526020600020905b815481529060010190602"
                "0018083116107f357829003601f168201915b505084810382528881815460018160011615610100020"
                "31660029004815260200191508054600181600116156101000203166002900480156108935780601f1"
                "061086857610100808354040283529160200191610893565b820191906000526020600020905b81548"
                "152906001019060200180831161087657829003601f168201915b50509950505050505050505050604"
                "05180910390a160019150610fca565b7f05e85d72e83e8d2c8c877a19dd3a15c60415f315dc6d176b2"
                "1537216537d275760006002878787336040518080602001806020018760ff1660ff168152602001866"
                "000191660001916815260200185600019166000191681526020018473fffffffffffffffffffffffff"
                "fffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200183810383528"
                "9818154600181600116156101000203166002900481526020019150805460018160011615610100020"
                "3166002900480156109c15780601f10610996576101008083540402835291602001916109c1565b820"
                "191906000526020600020905b8154815290600101906020018083116109a457829003601f168201915"
                "b505083810382528881815460018160011615610100020316600290048152602001915080546001816"
                "0011615610100020316600290048015610a445780601f10610a1957610100808354040283529160200"
                "191610a44565b820191906000526020600020905b815481529060010190602001808311610a2757829"
                "003601f168201915b50509850505050505050505060405180910390a160009150610fca565b5b5b808"
                "060010191505061058f565b610a79336117b7565b15610d8e5760038054806001018281610a9291906"
                "1189b565b91600052602060002090602091828204019190065b87909190916101000a81548160ff021"
                "916908360ff1602179055505060048054806001018281610ad791906118d5565b91600052602060002"
                "0900160005b869091909150906000191690555060058054806001018281610b0791906118d5565b916"
                "000526020600020900160005b859091909150906000191690555060068054806001018281610b37919"
                "0611901565b916000526020600020900160005b33909190916101000a81548173fffffffffffffffff"
                "fffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff1602179"
                "05550507fbf474e795141390215f4f179557402a28c562b860f7b74dce4a3c0e0604cd97e600060016"
                "002888888604051808060200180602001806020018760ff1660ff16815260200186600019166000191"
                "68152602001856000191660001916815260200184810384528a8181546001816001161561010002031"
                "66002900481526020019150805460018160011615610100020316600290048015610c6a5780601f106"
                "10c3f57610100808354040283529160200191610c6a565b"
                "820191906000526020600020905b815481529060010190602001808311610c4d57829003601f168201"
                "915b505084810383528981815460018160011615610100020316600290048152602001915080546001"
                "8160011615610100020316600290048015610ced5780601f10610cc257610100808354040283529160"
                "200191610ced565b820191906000526020600020905b815481529060010190602001808311610cd057"
                "829003601f168201915b50508481038252888181546001816001161561010002031660029004815260"
                "20019150805460018160011615610100020316600290048015610d705780601f10610d455761010080"
                "8354040283529160200191610d70565b820191906000526020600020905b8154815290600101906020"
                "01808311610d5357829003601f168201915b5050995050505050505050505060405180910390a16001"
                "9150610fca565b7fc585b66a303b685f03874907af9278712998ea1acfed37bcb4277da02cddb8b460"
                "006001600288888833604051808060200180602001806020018860ff1660ff16815260200187600019"
                "1660001916815260200186600019166000191681526020018573ffffffffffffffffffffffffffffff"
                "ffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200184810384528b8181"
                "5460018160011615610100020316600290048152602001915080546001816001161561010002031660"
                "0290048015610ea45780601f10610e7957610100808354040283529160200191610ea4565b82019190"
                "6000526020600020905b815481529060010190602001808311610e8757829003601f168201915b5050"
                "84810383528a8181546001816001161561010002031660029004815260200191508054600181600116"
                "15610100020316600290048015610f275780601f10610efc5761010080835404028352916020019161"
                "0f27565b820191906000526020600020905b815481529060010190602001808311610f0a5782900360"
                "1f168201915b5050848103825289818154600181600116156101000203166002900481526020019150"
                "805460018160011615610100020316600290048015610faa5780601f10610f7f576101008083540402"
                "83529160200191610faa565b820191906000526020600020905b815481529060010190602001808311"
                "610f8d57829003601f168201915b50509a505050505050505050505060405180910390a16000915061"
                "0fca565b5b509392505050565b610fda61192d565b610fe261192d565b610fea61192d565b610ff261"
                "1941565b610ffa611955565b611002611955565b61100a611969565b6000611014611969565b600060"
                "0760009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffff"
                "ffffffffffffffffffffffffffff1663fa69efbd6000604051602001526040518163ffffffff167c01"
                "0000000000000000000000000000000000000000000000000000000002815260040180905060206040"
                "5180830381600087803b15156110a457fe5b6102c65a03f115156110b257fe5b505050604051805190"
                "509250826040518059106110cc5750595b908082528060200260200182016040525b50915060009050"
                "5b828110156111f357600760009054906101000a900473ffffffffffffffffffffffffffffffffffff"
                "ffff1673ffffffffffffffffffffffffffffffffffffffff16633ffefe4e8260006040516020015260"
                "40518263ffffffff167c01000000000000000000000000000000000000000000000000000000000281"
                "5260040180828152602001915050602060405180830381600087803b151561118357fe5b6102c65a03"
                "f1151561119157fe5b5050506040518051905082828151811015156111a957fe5b9060200190602002"
                "019073ffffffffffffffffffffffffffffffffffffffff16908173ffffffffffffffffffffffffffff"
                "ffffffffffff16815250505b80806001019150506110e5565b60006001600260036004600587868054"
                "600181600116156101000203166002900480601f016020809104026020016040519081016040528092"
                "9190818152602001828054600181600116156101000203166002900480156112955780601f1061126a"
                "57610100808354040283529160200191611295565b820191906000526020600020905b815481529060"
                "01019060200180831161127857829003601f168201915b505050505096508580546001816001161561"
                "01000203166002900480601f0160208091040260200160405190810160405280929190818152602001"
                "828054600181600116156101000203166002900480156113315780601f106113065761010080835404"
                "0283529160200191611331565b820191906000526020600020905b8154815290600101906020018083"
                "1161131457829003601f168201915b5050505050955084805460018160011615610100020316600290"
                "0480601f01602080910402602001604051908101604052809291908181526020018280546001816001"
                "16156101000203166002900480156113cd5780601f106113a257610100808354040283529160200191"
                "6113cd565b820191906000526020600020905b8154815290600101906020018083116113b057829003"
                "601f168201915b50505050509450838054806020026020016040519081016040528092919081815260"
                "2001828054801561144557602002820191906000526020600020906000905b82829054906101000a90"
                "0460ff1660ff168152602001906001019060208260000104928301926001038202915080841161140e"
                "5790505b50505050509350828054806020026020016040519081016040528092919081815260200182"
                "8054801561149b57602002820191906000526020600020905b81546000191681526020019060010190"
                "808311611483575b505050505092508180548060200260200160405190810160405280929190818152"
                "60200182805480156114f157602002820191906000526020600020905b815460001916815260200190"
                "600101908083116114d9575b5050505050915099509950995099509950995099505b50505090919293"
                "949596565b61151b611969565b6000611525611969565b6000600760009054906101000a900473ffff"
                "ffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16"
                "63fa69efbd6000604051602001526040518163ffffffff167c01000000000000000000000000000000"
                "00000000000000000000000000028152600401809050602060405180830381600087803b15156115b5"
                "57fe5b6102c65a03f115156115c357fe5b505050604051805190509250826040518059106115dd5750"
                "595b908082528060200260200182016040525b509150600090505b8281101561170457600760009054"
                "906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffff"
                "ffffffffffffffffff16633ffefe4e826000604051602001526040518263ffffffff167c0100000000"
                "0000000000000000000000000000000000000000000000000281526004018082815260200191505060"
                "2060405180830381600087803b151561169457fe5b6102c65a03f115156116a257fe5b505050604051"
                "8051905082828151811015156116ba57fe5b9060200190602002019073ffffffffffffffffffffffff"
                "ffffffffffffffff16908173ffffffffffffffffffffffffffffffffffffffff16815250505b808060"
                "01019150506115f6565b8193505b50505090565b61171661192d565b60018054600181600116156101"
                "000203166002900480601f016020809104026020016040519081016040528092919081815260200182"
                "8054600181600116156101000203166002900480156117ac5780601f10611781576101008083540402"
                "835291602001916117ac565b820191906000526020600020905b815481529060010190602001808311"
                "61178f57829003601f168201915b505050505090505b90565b6000600760009054906101000a900473"
                "ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffff"
                "ff166363a9c3d7836000604051602001526040518263ffffffff167c01000000000000000000000000"
                "00000000000000000000000000000000028152600401808273ffffffffffffffffffffffffffffffff"
                "ffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001915050602060405180"
                "830381600087803b151561187b57fe5b6102c65a03f1151561188957fe5b5050506040518051905090"
                "505b919050565b8154818355818115116118d057601f016020900481601f0160209004836000526020"
                "60002091820191016118cf919061197d565b5b505050565b8154818355818115116118fc5781836000"
                "52602060002091820191016118fb91906119a2565b5b505050565b8154818355818115116119285781"
                "8360005260206000209182019101611927919061197d565b5b505050565b6020604051908101604052"
                "80600081525090565b602060405190810160405280600081525090565b602060405190810160405280"
                "600081525090565b602060405190810160405280600081525090565b61199f91905b8082111561199b"
                "576000816000905550600101611983565b5090565b90565b6119c491905b808211156119c057600081"
                "60009055506001016119a8565b5090565b905600a165627a7a7230582046f5f583ae0b03fcb18e88a7"
                "acc2aab1cafc4497dadf93eddaf47ebdf2abfa9b0029a165627a7a72305820b189b4b2b76318081522"
                "14e97b9d87f80444f01baeb4fedf251de35c7998da8000290000000000000000000000000000000000"
                "0000000000000000000000000000200000000000000000000000000000000000000000000000000000"
                "00000000000300000000000000000000000033674063c4618f4773fac75dc2f07e55f6f391ce000000"
                "0000000000000000006bc952a2e4db9c0c86a368d83e9df0c6ab481102000000000000000000000000"
                "5a6c7ccf9efa702f4e8888ff7e8a3310abcf8c511ca06fc1c64606152aec58be38aafd15de2b0bacea"
                "5de6405f8d620c4c08ab6584aea01504e9a6d468b30a6886bee617364464f7d42f5bad3f29ff0e473e"
                "7772792359");
            dev::eth::Transaction tx(ref(rlpBytes), dev::eth::CheckTransaction::Everything);
            dev::KeyPair key_pair(dev::Secret::random());
            dev::Secret sec = key_pair.secret();
            u256 maxBlockLimit = u256(1000);
            tx.setNonce(tx.nonce() + u256(1));
            tx.setBlockLimit(u256(blockChain->number()) + maxBlockLimit);
            dev::Signature sig = sign(sec, tx.sha3(dev::eth::WithoutSignature));
            tx.updateSignature(SignatureStruct(sig));
            LOG(INFO) << "Tx" << tx.sha3();
            block.appendTransaction(tx);
            auto context = blockVerifier->executeBlock(block, parentBlock->header().stateRoot());
            blockChain->commitBlock(block, context);
            dev::eth::TransactionReceipt receipt =
                blockChain->getTransactionReceiptByHash(tx.sha3());
            LOG(INFO) << "receipt " << receipt;
        }
    }
    else if (argc > 1 && std::string("verify") == argv[1])
    {
    }
}