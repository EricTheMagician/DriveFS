cmake_minimum_required(VERSION 3.9)
project(DriveFS)

set(CMAKE_CXX_STANDARD 17)

add_executable(DriveFS
        /usr/include/easylogging++.cc
        /usr/include/easylogging++.h
        src/main.cpp
        src/BaseFile.cpp include/BaseFile.h
        src/gdrive/File.cpp include/gdrive/File.h
        src/gdrive/Filesystem.cpp include/gdrive/Filesystem.h
        src/gdrive/Account.cpp include/gdrive/Account.h
        src/BaseAccount.cpp include/BaseAccount.h external/adaptive_time_parser/adaptive_time_parser.cpp src/gdrive/Filesystem.cpp include/gdrive/Filesystem.h)

target_compile_definitions(${PROJECT_NAME}
        PUBLIC
        _FILE_OFFSET_BITS=64)

find_package(PkgConfig REQUIRED)

#add search for libbson
pkg_search_module(LIBBSON REQUIRED libbson-1.0)

target_include_directories(${PROJECT_NAME} PUBLIC
        include
        external/adaptive_time_parser
        /usr/include/openssl-1.0
        /usr/local/include/mongocxx/v_noabi
        /usr/local/include/bsoncxx/v_noabi
        )
#set(CMAKE_MODULE_PATH /usr/local/include/mongocxx/v_noabi/mongocxx/cmake)
#find_package(mongocxx REQUIRED)

#include_directories(${LIBBSON_INCLUDE_DIRS})
#link_directories(${LIBBSON_LIBRARY_DIRS})
#link_libraries(bsoncxx)


SET(Boost_USE_MULTITHREAD ON)
find_package(Boost COMPONENTS thread system REQUIRED)
find_library(LIBCRYPTO libcrypto.so.1.0.0)
find_library(LIBSSL libssl.so.1.0.0)
target_link_libraries(${PROJECT_NAME}
        cpprest
        pthread
        ${Boost_LIBRARIES}
        ${LIBCRYPTO}
        ${LIBSSL}
        ${LIBBSON_LIBRARIES}
        mongocxx bsoncxx
        )
