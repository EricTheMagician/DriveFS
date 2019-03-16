cmake_minimum_required(VERSION 3.9)
project(DriveFS CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS -std=c++17)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
endif()

set(common_src_files 
        # semaphore
        external/semaphore/common/autoresetevent.h
        external/semaphore/common/sema.h

        #easylogging
        external/easyloggingpp/src/easylogging++.cc
        external/easyloggingpp/src/easylogging++.h

        # time parser
        external/adaptive_time_parser/adaptive_time_parser.cpp

        # common base files
        src/BaseFile.cpp
        ./include/BaseFile.h
        src/BaseFileSystem.cpp
        ./include/BaseFileSystem.h
        src/FolderIO.cpp
        ./include/FolderIO.h 
        
)

# add_executable(${PROJECT_NAME}_encrypted
#         ${common_src_files}


#         # my files
#         src/main.cpp
#         src/BaseFile.cpp
#         ./include/BaseFile.h
#         src/gdrive/File.cpp
#         ./include/gdrive/File.h
#         src/gdrive/Filesystem.cpp
#         ./include/gdrive/Filesystem.h
#         src/gdrive/Account.cpp
#         ./include/gdrive/Account.h
#         src/BaseAccount.cpp
#         ./include/BaseAccount.h
#         src/gdrive/Filesystem.cpp
#         ./include/gdrive/Filesystem.h
#         src/BaseFileSystem.cpp
#         ./include/BaseFileSystem.h
#         ./include/gdrive/FileIO.h
#         ./include/DownloadBuffer.h
#         ./src/DownloadBuffer.cpp 

#         ./src/crypt/rclone.cpp 
#         ./include/crypt/rclone.h 

#         ./src/gdrive_encrypted/Filesystem.cpp
#         ./include/gdrive_encrypted/Filesystem.h
#         ./src/gdrive_encrypted/Account.cpp
# )

add_executable(${PROJECT_NAME}
        ${common_src_files}
        # my files
        src/main.cpp
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
        src/gdrive/FileIO.cpp
        ./include/gdrive/FileIO.h
        ./include/DownloadBuffer.h
        ./src/DownloadBuffer.cpp 


        )
target_compile_definitions(${PROJECT_NAME}
        PUBLIC
        _FILE_OFFSET_BITS=64
        )

#lib fuse3
find_package(PkgConfig REQUIRED)

if(USE_FUSE3)
    MESSAGE("Using fuse3")
    pkg_search_module(LIBFUSE REQUIRED fuse3)
    target_compile_definitions(${PROJECT_NAME} PUBLIC USE_FUSE3
            FUSE_USE_VERSION=31)

else()
    MESSAGE("Using fuse2")
    pkg_search_module(LIBFUSE REQUIRED fuse)
    target_compile_definitions(${PROJECT_NAME} PUBLIC
            FUSE_USE_VERSION=29)
endif()

#add search for libbson
#pkg_search_module(LIBBSON REQUIRED libbson-1.0)
target_include_directories(${PROJECT_NAME} PUBLIC
#        ./external/include/mongocxx/v_noabi/
#        ./external/include/bsoncxx/v_noabi/
        ./include
        ./external
        ./external/include
        ./external/adaptive_time_parser
        ./external/semaphore/common
        ./external/easyloggingpp/src
        ${LIBFUSE_INCLUDE_DIRS}
#        ${CMAKE_BINARY_DIR}/include
        )


include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup(TARGETS)
#set(CMAKE_MODULE_PATH /usr/local/include/mongocxx/v_noabi/mongocxx/cmake)
#find_package(mongocxx REQUIRED)


#SET(Boost_USE_MULTITHREAD ON)
#SET(BOOST_ROOT ${CONAN_BOOST_ROOT})
#find_package(Boost 1.66.0 COMPONENTS thread system program_options filesystem REQUIRED)
#set(OPENSSL_ROOT_DIR ${CONAN_OPENSSL_ROOT})
#set(OPENSSL_USE_STATIC_LIBS FALSE)
#set(OPENSSL_CRYPTO_LIBRARY ${CONAN_OPENSSL_ROOT}/lib)
#find_package(OpenSSL 1.0 EXACT REQUIRED )

MESSAGE("fuse libraries are: ${LIBFUSE_LIBRARIES}")

target_link_libraries(${PROJECT_NAME}
        CONAN_PKG::jemalloc
        pthread_nonshared.a
        ${LIBFUSE_LIBRARIES}
        CONAN_PKG::boost
        CONAN_PKG::OpenSSL
        CONAN_PKG::cpprestsdk
        CONAN_PKG::mongo-cxx-driver

)

target_compile_definitions(${PROJECT_NAME}
      PUBLIC
        ELPP_THREAD_SAFE
        ELPP_FEATURE_CRASH_LOG
        )

