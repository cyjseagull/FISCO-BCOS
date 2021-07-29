include(ExternalProject)
include(GNUInstallDirs)

if (CMAKE_SYSTEM_NAME MATCHES "Darwin")
	set(TASSL_CONFIG_COMMAND perl ./Configure darwin64-x86_64-cc)
else()
	set(TASSL_CONFIG_COMMAND bash config -Wl,--rpath=./ shared)
endif () 

set(TASSL_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps/src/tassl-1.1.1b)
set(TASSL_BUILD_COMMAND make)
ExternalProject_Add(tassl-1.1.1b
        PREFIX ${CMAKE_SOURCE_DIR}/deps
        DOWNLOAD_NAME tassl_1.1.1b-crypto-acc.tar.gz
        DOWNLOAD_NO_PROGRESS 1
        #URL https://github.com/WeBankBlockchain/Crypto-Accelerator-HKUST/archive/0ce9202410439d0a7d486f73f639a41339967616.tar.gz
        #URL_HASH SHA256=34e81bcd599c44d90bc629e2625bdaaef3946c801262a82bccb8413e91561429
        GIT_REPOSITORY https://github.com/cyjseagull/Crypto-Accelerator-HKUST.git
        GIT_TAG d7a832588b15a6ba0271d904ae2b8fef395aee57
        # GIT_SHALLOW true
        # BUILD_IN_SOURCE 1
        BINARY_DIR ${TASSL_SOURCE_DIR}
        CONFIGURE_COMMAND ${TASSL_CONFIG_COMMAND}
        LOG_CONFIGURE 1
        LOG_BUILD 1
        LOG_INSTALL 1
        BUILD_COMMAND ${TASSL_BUILD_COMMAND}
        INSTALL_COMMAND ""
)

ExternalProject_Get_Property(tassl-1.1.1b SOURCE_DIR)
add_library(TASSL STATIC IMPORTED)
set(TASSL_SUFFIX .a)
set(TASSL_INCLUDE_DIRS ${TASSL_SOURCE_DIR}/include)
set(TASSL_LIBRARY ${TASSL_SOURCE_DIR}/libssl${TASSL_SUFFIX})
set(TASSL_CRYPTO_LIBRARIE ${TASSL_SOURCE_DIR}/libcrypto${TASSL_SUFFIX})
set(TASSL_LIBRARIES ${TASSL_LIBRARY} ${TASSL_CRYPTO_LIBRARIE} dl)

add_library(tassl::ssl STATIC IMPORTED GLOBAL)
set_property(TARGET tassl::ssl PROPERTY IMPORTED_LOCATION ${TASSL_LIBRARY})
set_property(TARGET tassl::ssl PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${TASSL_INCLUDE_DIRS})
add_dependencies(tassl::ssl tassl-1.1.1b2)

set(OPENSSL_INCLUDE_DIRS ${TASSL_INCLUDE_DIRS})
set(OPENSSL_LIBRARIES ${TASSL_LIBRARIES})
set(TASSL_CRYPTO_INCLUDE_DIRS ${TASSL_SOURCE_DIR}/crypto/include)
set(TASSL_CRYPTO_ROOT_INCLUDE_DIRS ${TASSL_SOURCE_DIR}/)
message(STATUS "libssl include  : ${TASSL_INCLUDE_DIRS}")
message(STATUS "libssl libraries: ${TASSL_LIBRARIES}")
