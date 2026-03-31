cmake_minimum_required(VERSION 3.20)
project(copilot_tools LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(COPILOT_BUILD_TESTS "Build tests" ON)
option(COPILOT_BUILD_EXAMPLES "Build examples" ON)
option(COPILOT_BUILD_BENCHMARKS "Build benchmark examples" ON)
option(HTTPX_ENABLE_OPENSSL "Enable OpenSSL backend for httpx TLS" OFF)
option(HTTPX_ENABLE_MBEDTLS "Enable mbedTLS backend for httpx TLS" OFF)

if(HTTPX_ENABLE_OPENSSL AND HTTPX_ENABLE_MBEDTLS)
    message(FATAL_ERROR "Select only one TLS backend: HTTPX_ENABLE_OPENSSL or HTTPX_ENABLE_MBEDTLS.")
endif()

if(MSVC)
    add_compile_options(/utf-8 /Zc:preprocessor)
endif()

if(WIN32)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

add_library(utils src/utils.cpp)
target_include_directories(utils PUBLIC include)

add_library(sysx src/sysx.cpp)
target_include_directories(sysx PUBLIC include)
if(WIN32)
    target_link_libraries(sysx PUBLIC ws2_32)
endif()

add_library(argtool src/argtool.cpp)
target_include_directories(argtool PUBLIC include)

add_library(asyncx src/asyncx.cpp)
target_include_directories(asyncx PUBLIC include)
target_link_libraries(asyncx PUBLIC sysx)

add_library(cfgx src/cfgx.cpp)
target_include_directories(cfgx PUBLIC include)

add_library(fsx src/fsx.cpp)
target_include_directories(fsx PUBLIC include)
target_link_libraries(fsx PUBLIC utils)

add_library(hashx src/hashx.cpp)
target_include_directories(hashx PUBLIC include)
target_link_libraries(hashx PUBLIC utils)

add_library(httpx src/httpx.cpp)
target_include_directories(httpx PUBLIC include)
target_link_libraries(httpx PUBLIC utils)
if(WIN32)
    target_link_libraries(httpx PUBLIC ws2_32)
endif()

if(HTTPX_ENABLE_OPENSSL)
    find_package(OpenSSL REQUIRED)
    target_compile_definitions(httpx PUBLIC HTTPX_ENABLE_OPENSSL)
    target_link_libraries(httpx PUBLIC OpenSSL::SSL OpenSSL::Crypto)
elseif(HTTPX_ENABLE_MBEDTLS)
    set(MBEDTLS_ROOT "" CACHE PATH "Root folder of mbedTLS installation")
    find_path(MBEDTLS_INCLUDE_DIR mbedtls/ssl.h
        HINTS
            ${MBEDTLS_ROOT}
            ${MBEDTLS_ROOT}/include
    )
    find_library(MBEDTLS_LIBRARY mbedtls
        HINTS
            ${MBEDTLS_ROOT}
            ${MBEDTLS_ROOT}/lib
            ${MBEDTLS_ROOT}/library
    )
    find_library(MBEDX509_LIBRARY mbedx509
        HINTS
            ${MBEDTLS_ROOT}
            ${MBEDTLS_ROOT}/lib
            ${MBEDTLS_ROOT}/library
    )
    find_library(MBEDCRYPTO_LIBRARY mbedcrypto
        HINTS
            ${MBEDTLS_ROOT}
            ${MBEDTLS_ROOT}/lib
            ${MBEDTLS_ROOT}/library
    )

    if(NOT MBEDTLS_INCLUDE_DIR OR NOT MBEDTLS_LIBRARY OR NOT MBEDX509_LIBRARY OR NOT MBEDCRYPTO_LIBRARY)
        message(FATAL_ERROR "mbedTLS requested but required headers/libs were not found. Set MBEDTLS_ROOT.")
    endif()

    target_compile_definitions(httpx PUBLIC HTTPX_ENABLE_MBEDTLS)
    target_include_directories(httpx PUBLIC ${MBEDTLS_INCLUDE_DIR})
    target_link_libraries(httpx PUBLIC ${MBEDTLS_LIBRARY} ${MBEDX509_LIBRARY} ${MBEDCRYPTO_LIBRARY})
endif()

add_library(logsys src/logsys.cpp)
target_include_directories(logsys PUBLIC include)
if(WIN32)
    target_link_libraries(logsys PUBLIC ws2_32)
endif()

add_library(textcodec src/textcodec.cpp)
target_include_directories(textcodec PUBLIC include)
target_link_libraries(textcodec PUBLIC utils)

add_library(tuix src/tuix.cpp)
target_include_directories(tuix PUBLIC include)

if(COPILOT_BUILD_EXAMPLES)
    add_executable(argtool_example examples/argtool_example.cpp)
    target_link_libraries(argtool_example PRIVATE argtool)

    add_executable(asyncx_example examples/asyncx_example.cpp)
    target_link_libraries(asyncx_example PRIVATE asyncx)

    add_executable(asyncx_fsx_bridge_example examples/asyncx_fsx_bridge_example.cpp)
    target_link_libraries(asyncx_fsx_bridge_example PRIVATE asyncx fsx)

    add_executable(asyncx_httpx_bridge_example examples/asyncx_httpx_bridge_example.cpp)
    target_link_libraries(asyncx_httpx_bridge_example PRIVATE asyncx httpx)

    add_executable(asyncx_logsys_bridge_example examples/asyncx_logsys_bridge_example.cpp)
    target_link_libraries(asyncx_logsys_bridge_example PRIVATE asyncx logsys)

    add_executable(cfgx_example examples/cfgx_example.cpp)
    target_link_libraries(cfgx_example PRIVATE cfgx)

    add_executable(cfgtool examples/cfgtool_example.cpp)
    target_link_libraries(cfgtool PRIVATE argtool cfgx)

    add_executable(fsx_example examples/fsx_example.cpp)
    target_link_libraries(fsx_example PRIVATE fsx)

    add_executable(hashx_example examples/hashx_example.cpp)
    target_link_libraries(hashx_example PRIVATE hashx)

    add_executable(httpx_example examples/httpx_example.cpp)
    target_link_libraries(httpx_example PRIVATE httpx)

    add_executable(logsys_example examples/logsys_example.cpp)
    target_link_libraries(logsys_example PRIVATE logsys)

    add_executable(sysx_example examples/sysx_example.cpp)
    target_link_libraries(sysx_example PRIVATE sysx)

    add_executable(textcodec_example examples/textcodec_example.cpp)
    target_link_libraries(textcodec_example PRIVATE textcodec)

    add_executable(tuix_example examples/tuix_example.cpp)
    target_link_libraries(tuix_example PRIVATE tuix)

    add_executable(tuix_showcase examples/tuix_showcase.cpp)
    target_link_libraries(tuix_showcase PRIVATE tuix)

    add_executable(utils_example examples/utils_example.cpp)
    target_link_libraries(utils_example PRIVATE utils)

    if(COPILOT_BUILD_BENCHMARKS)
        add_executable(asyncx_benchmark examples/asyncx_benchmark.cpp)
        target_link_libraries(asyncx_benchmark PRIVATE asyncx)

        add_executable(httpx_benchmark examples/httpx_benchmark.cpp)
        target_link_libraries(httpx_benchmark PRIVATE httpx)
    endif()
endif()

if(COPILOT_BUILD_TESTS)
    include(FetchContent)

    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )

    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()

    add_executable(argtool_tests tests/argtool_tests.cpp)
    target_link_libraries(argtool_tests PRIVATE argtool GTest::gtest_main)
    add_test(NAME argtool_tests COMMAND argtool_tests)

    add_executable(asyncx_tests tests/asyncx_tests.cpp)
    target_link_libraries(asyncx_tests PRIVATE asyncx GTest::gtest_main)
    add_test(NAME asyncx_tests COMMAND asyncx_tests)

    add_executable(cfgx_tests tests/cfgx_tests.cpp)
    target_link_libraries(cfgx_tests PRIVATE cfgx GTest::gtest_main)
    add_test(NAME cfgx_tests COMMAND cfgx_tests)

    add_executable(fsx_tests tests/fsx_tests.cpp)
    target_link_libraries(fsx_tests PRIVATE fsx GTest::gtest_main)
    add_test(NAME fsx_tests COMMAND fsx_tests)

    add_executable(hashx_tests tests/hashx_tests.cpp)
    target_link_libraries(hashx_tests PRIVATE hashx GTest::gtest_main)
    add_test(NAME hashx_tests COMMAND hashx_tests)

    add_executable(httpx_tests tests/httpx_tests.cpp)
    target_link_libraries(httpx_tests PRIVATE httpx GTest::gtest_main)
    add_test(NAME httpx_tests COMMAND httpx_tests)

    add_executable(logsys_tests tests/logsys_tests.cpp)
    target_link_libraries(logsys_tests PRIVATE logsys GTest::gtest_main)
    add_test(NAME logsys_tests COMMAND logsys_tests)

    add_executable(sysx_tests tests/sysx_tests.cpp)
    target_link_libraries(sysx_tests PRIVATE sysx GTest::gtest_main)
    add_test(NAME sysx_tests COMMAND sysx_tests)

    add_executable(textcodec_tests tests/textcodec_tests.cpp)
    target_link_libraries(textcodec_tests PRIVATE textcodec GTest::gtest_main)
    add_test(NAME textcodec_tests COMMAND textcodec_tests)

    add_executable(tuix_tests tests/tuix_tests.cpp)
    target_link_libraries(tuix_tests PRIVATE tuix GTest::gtest_main)
    add_test(NAME tuix_tests COMMAND tuix_tests)

    add_executable(utils_tests tests/utils_tests.cpp)
    target_link_libraries(utils_tests PRIVATE utils GTest::gtest_main)
    add_test(NAME utils_tests COMMAND utils_tests)
endif()
