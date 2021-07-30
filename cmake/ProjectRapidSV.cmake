include(ExternalProject)
include(GNUInstallDirs)

ExternalProject_Add(rapid-sv
    PREFIX ${CMAKE_SOURCE_DIR}/deps
    DOWNLOAD_NAME RapidSV.tar.gz
    DOWNLOAD_NO_PROGRESS 1
    URL https://github.com/cyjseagull/RapidSV/archive/e5dc2af83b8c59657928ac6d28b397e6c0f77964.tar.gz
    URL_HASH SHA256=a7ea121bbcbd67d283d469edc091e83233763d333ad2f6c5e7d106b6c924191e
    INSTALL_COMMAND ""
    LOG_CONFIGURE 1
    LOG_BUILD 1
    LOG_INSTALL 1
    BUILD_IN_SOURCE 1
)
ExternalProject_Get_Property(rapid-sv SOURCE_DIR)
add_library(RapidSV STATIC IMPORTED)
set(RapidSV_LIBRARY ${SOURCE_DIR}/RapidSV/libRapidSV.a)
set(RapidSV_INCLUDE_DIR ${SOURCE_DIR}/RapidSV)
set_property(TARGET RapidSV PROPERTY IMPORTED_LOCATION ${RapidSV_LIBRARY})
set_property(TARGET RapidSV PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${RapidSV_INCLUDE_DIR})
unset(SOURCE_DIR)
