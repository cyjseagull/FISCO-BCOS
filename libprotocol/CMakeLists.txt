aux_source_directory(. SRC_LIST)
file(GLOB HEADERS "*.h")

aux_source_directory(./proto SRC_LIST)
include_directories(./proto)

add_library(protocol ${SRC_LIST} ${HEADERS})
target_link_libraries(protocol PUBLIC devcrypto TBB PROTOBUF)
