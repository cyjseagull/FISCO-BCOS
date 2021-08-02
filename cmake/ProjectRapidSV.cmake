include(ExternalProject)
include(GNUInstallDirs)

ExternalProject_Add(rapid-sv
    PREFIX ${CMAKE_SOURCE_DIR}/deps
    DOWNLOAD_NAME RapidSV.tar.gz
    DOWNLOAD_NO_PROGRESS 1
    URL https://github.com/cyjseagull/RapidSV/archive/55fc816bdcd31925b93adc2fe252204c5896d96d.tar.gz
    URL_HASH SHA256=73b3d0bb63760cd46b55e8141b3ed56449c299b09a593b06a03833442856d4eb
    INSTALL_COMMAND ""
    LOG_CONFIGURE 1
    LOG_BUILD 1
    LOG_INSTALL 1
    BUILD_IN_SOURCE 1
)
ExternalProject_Get_Property(rapid-sv SOURCE_DIR)
find_package(CUDA)
find_library(gmp gmp)
find_package(OpenMP)

add_library(RapidSV STATIC IMPORTED)
set(RapidSV_LIBRARY ${SOURCE_DIR}/RapidSV/libRapidSV.a)
set(RapidSV_INCLUDE_DIR ${SOURCE_DIR})
set_property(TARGET RapidSV PROPERTY INTERFACE_LINK_LIBRARIES ${CUDA_LIBRARIES} gmp OpenMP::OpenMP_CXX)
set_property(TARGET RapidSV PROPERTY IMPORTED_LOCATION ${RapidSV_LIBRARY})
set_property(TARGET RapidSV PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${RapidSV_INCLUDE_DIR})
unset(SOURCE_DIR)
