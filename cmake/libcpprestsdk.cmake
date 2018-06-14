include(ExternalProject)
find_library(LIBCRYPTO libcrypto.so.1.0)
find_library(LIBSSL libssl.so.1.0)

set(CPPREST_LIB ${CMAKE_CURRENT_SOURCE_DIR}/external/lib/libcpprest.a)
set(CPPREST_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/external/include)
ExternalProject_Add(cpprest
        PREFIX cpprest
        GIT_REPOSITORY https://github.com/Microsoft/cpprestsdk.git
        GIT_TAG v2.10.2
        EXCLUDE_FROM_ALL 0
        SOURCE_SUBDIR Release
        EXCLUDE_FROM_ALL 0
        CMAKE_CACHE_ARGS "-DBUILD_SAMPLES:BOOL=OFF"
        CMAKE_CACHE_ARGS "-DBUILD_TESTS:BOOL=OFF"
        CMAKE_CACHE_ARGS "-DOPENSSL_CRYPTO_LIBRARY:FILEPATH=${LIBCRYPTO}"
        CMAKE_CACHE_ARGS "-DOPENSSL_INCLUDE_DIR:PATH=/usr/include/openssl-1.0"
        CMAKE_CACHE_ARGS "-DOPENSSL_SSL_LIBRARY:FILEPATH=${LIBSSL}"
        CMAKE_CACHE_ARGS "-DBUILD_SHARED_LIBS:BOOL=OFF"
        CMAKE_CACHE_ARGS "-DCMAKE_CXX_FLAGS:STRING=-fPIC -Wno-error=format-truncation="
        CMAKE_CACHE_ARGS "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
        CMAKE_CACHE_ARGS "-DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_SOURCE_DIR}/external"
        BUILD_BYPRODUCTS ${CPPREST_LIB}

        )
