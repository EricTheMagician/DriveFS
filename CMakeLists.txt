cmake_minimum_required(VERSION 3.9)
project(DriveFS CXX)

set(CMAKE_CXX_STANDARD 17)

add_executable(DriveFS
        # semaphore
        external/semaphore/common/autoresetevent.h
        external/semaphore/common/sema.h

        # my files
        /usr/include/easylogging++.cc
        /usr/include/easylogging++.h
        src/main.cpp
        src/BaseFile.cpp 
        ./include/BaseFile.h
        src/gdrive/File.cpp 
        ./include/gdrive/File.h
        src/gdrive/Filesystem.cpp 
        ./include/gdrive/Filesystem.h
        src/gdrive/Account.cpp 
        ./include/gdrive/Account.h
        src/BaseAccount.cpp 
        ./include/BaseAccount.h 
        external/adaptive_time_parser/adaptive_time_parser.cpp 
        src/gdrive/Filesystem.cpp 
        ./include/gdrive/Filesystem.h 
        src/BaseFileSystem.cpp
        ./include/BaseFileSystem.h
        src/FolderIO.cpp
        ./include/FolderIO.h src/gdrive/FileIO.cpp
        ./include/gdrive/FileIO.h
        ./include/DownloadBuffer.h

        src/DownloadBuffer.cpp)

target_compile_definitions(${PROJECT_NAME}
        PUBLIC
        _FILE_OFFSET_BITS=64
        FUSE_USE_VERSION=31)

find_package(PkgConfig REQUIRED)

#add search for libbson
pkg_search_module(LIBBSON REQUIRED libbson-1.0)
pkg_search_module(LIBFUSE3 REQUIRED fuse3)
target_include_directories(${PROJECT_NAME} PUBLIC
        ./include
        ./external
        ./external/adaptive_time_parser
        ./external/semaphore/common
        /usr/include/openssl-1.0
        /usr/local/include/mongocxx/v_noabi
        /usr/local/include/bsoncxx/v_noabi
        ${LIBFUSE3_INCLUDE_DIRS}
        )
#set(CMAKE_MODULE_PATH /usr/local/include/mongocxx/v_noabi/mongocxx/cmake)
#find_package(mongocxx REQUIRED)



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
        mongocxx bsoncxx
        ${LIBFUSE3_LIBRARIES}
        )


# for you complte me on vim
SET( CMAKE_EXPORT_COMPILE_COMMANDS ON )
IF( EXISTS "${CMAKE_CURRENT_BINARY_DIR}/compile_commands.json" )
  EXECUTE_PROCESS( COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_CURRENT_BINARY_DIR}/compile_commands.json
    ${CMAKE_CURRENT_SOURCE_DIR}/compile_commands.json
  )
ENDIF()
