include(ExternalProject)
include(GNUInstallDirs)

ExternalProject_Add(rapid-sv
    PREFIX ${CMAKE_SOURCE_DIR}/deps
    DOWNLOAD_NAME RapidSV.tar.gz
    DOWNLOAD_NO_PROGRESS 1
    URL https://github.com/cyjseagull/RapidSV/archive/ffe234ddb4fe7264ac7b6baa94fabba18683f008.tar.gz
    URL_HASH SHA256=9461b26e708918e5e77dc0107f01fdddae44a46104afa3760daa1d916e1ab146
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
