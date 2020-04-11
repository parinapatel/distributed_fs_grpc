cmake_minimum_required(VERSION 3.12)
project(part1)
# set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_STANDARD 14)

set(CMAKE_CXX_FLAGS -g3)

include_directories(.)
include_directories(proto-src)
include_directories(src)
include_directories(/usr/local/include)

add_custom_command(OUTPUT proto-src/dfs-service.pb.h proto-src/dfs-service.pb.cc
        COMMAND protoc -I ./ --cpp_out=./proto-src dfs-service.proto
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/dfs-service.proto
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "regular protos")

add_custom_command(OUTPUT proto-src/dfs-service.grpc.pb.h proto-src/dfs-service.grpc.pb.cc
        COMMAND protoc -I ./ --grpc_out=./proto-src --plugin=protoc-gen-grpc=/usr/local/bin/grpc_cpp_plugin dfs-service.proto
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/dfs-service.proto
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "grpc protos")

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/part1/proto-src/dfs-service.grpc.pb.cc ${CMAKE_CURRENT_BINARY_DIR}/proto-src/dfs-service.grpc.pb.cc)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/part1/proto-src/dfs-service.grpc.pb.h ${CMAKE_CURRENT_BINARY_DIR}/proto-src/dfs-service.grpc.pb.h)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/part1/proto-src/dfs-service.pb.cc ${CMAKE_CURRENT_BINARY_DIR}/proto-src/dfs-service.pb.cc)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/part1/proto-src/dfs-service.pb.h ${CMAKE_CURRENT_BINARY_DIR}/proto-src/dfs-service.pb.h)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/part2/proto-src/dfs-service.grpc.pb.cc ${CMAKE_CURRENT_BINARY_DIR}/proto-src/dfs-service.grpc.pb.cc)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/part2/proto-src/dfs-service.grpc.pb.h ${CMAKE_CURRENT_BINARY_DIR}/proto-src/dfs-service.grpc.pb.h)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/part2/proto-src/dfs-service.pb.cc ${CMAKE_CURRENT_BINARY_DIR}/proto-src/dfs-service.pb.cc)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/part2/proto-src/dfs-service.pb.h ${CMAKE_CURRENT_BINARY_DIR}/proto-src/dfs-service.pb.h)
add_definitions(-DDFS_MAIN)

add_executable(dfs-client-p1
        part1/proto-src/dfs-service.grpc.pb.cc
        part1/proto-src/dfs-service.grpc.pb.h
        part1/proto-src/dfs-service.pb.cc
        part1/proto-src/dfs-service.pb.h
        part1/src/dfslibx-clientnode-p1.h
        part1/src/dfslibx-clientnode-p1.cpp
        part1/dfslib-clientnode-p1.cpp
        part1/dfslib-clientnode-p1.h
        part1/dfslib-shared-p1.cpp
        part1/dfslib-shared-p1.h
        part1/dfslib-servernode-p1.h
        part1/dfslib-servernode-p1.cpp
        part1/src/dfs-utils.h
        part1/src/dfs-client-p1.h
        part1/src/dfs-client-p1.cpp
        )

target_link_libraries(dfs-client-p1
        -fPIC
        -fno-omit-frame-pointer
        -fsanitize=address
        -L/usr/local/lib
        protobuf
        grpc
        grpc++
        pthread
        grpc++_reflection
        )

target_link_libraries(dfs-client-p1 -static-libasan)

add_executable(dfs-server-p1
        part1/proto-src/dfs-service.grpc.pb.cc
        part1/proto-src/dfs-service.grpc.pb.h
        part1/proto-src/dfs-service.pb.cc
        part1/proto-src/dfs-service.pb.h
        part1/src/dfslibx-clientnode-p1.h
        part1/src/dfslibx-clientnode-p1.cpp
        part1/dfslib-clientnode-p1.cpp
        part1/dfslib-clientnode-p1.h
        part1/dfslib-shared-p1.cpp
        part1/dfslib-shared-p1.h
        part1/dfslib-servernode-p1.h
        part1/dfslib-servernode-p1.cpp
        part1/src/dfs-server-p1.cpp
        )

target_link_libraries(dfs-server-p1
        -fPIC
        -fno-omit-frame-pointer
        -fsanitize=address
        -L/usr/local/lib
        protobuf
        grpc
        grpc++
        pthread
        grpc++_reflection
        )

target_link_libraries(dfs-server-p1 -static-libasan)


add_executable(dfs-client-p2
        part2/proto-src/dfs-service.grpc.pb.cc
        part2/proto-src/dfs-service.grpc.pb.h
        part2/proto-src/dfs-service.pb.cc
        part2/proto-src/dfs-service.pb.h
        part2/src/dfs-client-p2.cpp
        part2/src/dfs-client-p2.h
        part2/src/dfs-utils.h
        part2/src/CRC.h
        part2/src/dfslibx-call-data.h
        part2/src/dfslibx-clientnode-p2.cpp
        part2/src/dfslibx-clientnode-p2.h
        part2/src/dfslibx-service-runner.h
        part2/dfslib-clientnode-p2.cpp
        part2/dfslib-clientnode-p2.h
        part2/dfslib-shared-p2.cpp
        part2/dfslib-shared-p2.h
        )

target_link_libraries(dfs-client-p2
        -fPIC
        -fno-omit-frame-pointer
        -fsanitize=address
        -L/usr/local/lib
        protobuf
        grpc
        grpc++
        pthread
        grpc++_reflection
        )
target_link_libraries(dfs-client-p2 -static-libasan)


add_executable(dfs-server-p2
        part2/proto-src/dfs-service.grpc.pb.cc
        part2/proto-src/dfs-service.grpc.pb.h
        part2/proto-src/dfs-service.pb.cc
        part2/proto-src/dfs-service.pb.h
        part2/src/dfs-server-p2.cpp
        part2/src/dfs-utils.h
        part2/src/dfslibx-call-data.h
        part2/src/CRC.h
        part2/src/dfslibx-service-runner.h
        part2/dfslib-servernode-p2.cpp
        part2/dfslib-servernode-p2.h
        part2/dfslib-shared-p2.cpp
        part2/dfslib-shared-p2.h
        )

target_link_libraries(dfs-server-p2
        -fPIC
        -fno-omit-frame-pointer
        -fsanitize=address
        -L/usr/local/lib
        protobuf
        grpc
        grpc++
        pthread
        grpc++_reflection
        )
target_link_libraries(dfs-server-p2 -static-libasan)
