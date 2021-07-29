file(GLOB_RECURSE HEADERS "*.h")
file(GLOB_RECURSE SRC_LIST "*.cpp")
add_library(devcrypto ${SRC_LIST} ${HEADERS})

eth_use(devcrypto OPTIONAL OpenSSL)
target_link_libraries(devcrypto PRIVATE Secp256k1 Cryptopp libff::ff)
target_link_libraries(devcrypto PUBLIC devcore)
target_include_directories(devcrypto PRIVATE ${TASSL_CRYPTO_INCLUDE_DIRS} ${TASSL_CRYPTO_ROOT_INCLUDE_DIRS})