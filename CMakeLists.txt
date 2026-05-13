cmake_minimum_required(VERSION 3.20)

project(ToolX VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

function(toolx_bool_option new_name old_name description default_value)
    set(new_was_defined FALSE)
    set(old_was_defined FALSE)
    if(DEFINED ${new_name})
        set(new_was_defined TRUE)
    endif()
    if(DEFINED ${old_name})
        set(old_was_defined TRUE)
    endif()

    if(DEFINED ${new_name})
        set(option_default "${${new_name}}")
    elseif(DEFINED ${old_name})
        set(option_default "${${old_name}}")
    else()
        set(option_default "${default_value}")
    endif()

    option(${new_name} "${description}" "${option_default}")

    if(old_was_defined AND NOT new_was_defined)
        message(DEPRECATION "${old_name} is deprecated; use ${new_name} instead.")
    elseif(old_was_defined AND new_was_defined AND NOT "${${old_name}}" STREQUAL "${${new_name}}")
        message(DEPRECATION "${old_name} is deprecated and differs from ${new_name}; ${new_name} takes precedence.")
    endif()
    set(${old_name} "${${new_name}}" CACHE BOOL "Deprecated alias for ${new_name}." FORCE)
    mark_as_advanced(${old_name})
endfunction()

toolx_bool_option(TOOLX_BUILD_TESTS COPILOT_BUILD_TESTS "Build tests" ON)
toolx_bool_option(TOOLX_BUILD_EXAMPLES COPILOT_BUILD_EXAMPLES "Build examples" ON)
toolx_bool_option(TOOLX_BUILD_BENCHMARKS COPILOT_BUILD_BENCHMARKS "Build benchmark examples" ON)
toolx_bool_option(TOOLX_ENABLE_CLANG_TIDY COPILOT_ENABLE_CLANG_TIDY "Enable clang-tidy during compilation" OFF)
toolx_bool_option(TOOLX_ENABLE_COVERAGE COPILOT_ENABLE_COVERAGE "Enable compiler coverage instrumentation" OFF)
option(TOOLX_BUILD_TOOLS "Build installable ToolX command-line tools" ON)
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

if(TOOLX_ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-O0 -g --coverage)
        add_link_options(--coverage)
    else()
        message(WARNING "Coverage instrumentation is only configured for GCC/Clang-like compilers.")
    endif()
endif()

find_program(CLANG_FORMAT_BIN NAMES clang-format)
find_program(CLANG_TIDY_BIN NAMES clang-tidy)
find_program(GCOVR_BIN NAMES gcovr)

set(TOOLX_FORMAT_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests"
    "${CMAKE_CURRENT_SOURCE_DIR}/examples"
)

set(TOOLX_FORMAT_FILES)
foreach(dir IN LISTS TOOLX_FORMAT_DIRS)
    if(EXISTS "${dir}")
        file(GLOB_RECURSE dir_files CONFIGURE_DEPENDS
            "${dir}/*.h"
            "${dir}/*.hpp"
            "${dir}/*.c"
            "${dir}/*.cc"
            "${dir}/*.cpp"
            "${dir}/*.cxx")
        list(APPEND TOOLX_FORMAT_FILES ${dir_files})
    endif()
endforeach()
list(REMOVE_DUPLICATES TOOLX_FORMAT_FILES)

set(TOOLX_LINT_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/utils.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/sysx.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/argtool.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/asyncx.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/cfgx.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/fsx.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/hashx.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/httpx.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/logsys.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/textcodec.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/tuix.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/cfgtool_example.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/toolx_sync.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/resultx_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/toolx_integration_tests.cpp"
)

if(CLANG_FORMAT_BIN)
    add_custom_target(format
        COMMAND "${CLANG_FORMAT_BIN}" -i ${TOOLX_FORMAT_FILES}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Formatting ToolX source files with clang-format"
        VERBATIM)

    add_custom_target(format-check
        COMMAND "${CLANG_FORMAT_BIN}" --dry-run --Werror ${TOOLX_FORMAT_FILES}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Checking ToolX source formatting with clang-format"
        VERBATIM)
endif()

if(TOOLX_ENABLE_CLANG_TIDY)
    if(NOT CLANG_TIDY_BIN)
        message(FATAL_ERROR "TOOLX_ENABLE_CLANG_TIDY=ON but clang-tidy was not found.")
    endif()
    set(CMAKE_CXX_CLANG_TIDY
        "${CLANG_TIDY_BIN};--config-file=${CMAKE_CURRENT_SOURCE_DIR}/.clang-tidy")
endif()

if(CLANG_TIDY_BIN)
    add_custom_target(lint-check
        COMMAND "${CLANG_TIDY_BIN}"
            -p "${CMAKE_BINARY_DIR}"
            --config-file="${CMAKE_CURRENT_SOURCE_DIR}/.clang-tidy"
            ${TOOLX_LINT_FILES}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Running clang-tidy on ToolX module sources"
        VERBATIM)
endif()

function(toolx_add_module name)
    add_library(${name} src/${name}.cpp)
    add_library(toolx::${name} ALIAS ${name})
    target_include_directories(${name}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    set_target_properties(${name} PROPERTIES
        EXPORT_NAME ${name}
        POSITION_INDEPENDENT_CODE ON)
endfunction()

toolx_add_module(utils)
toolx_add_module(sysx)
if(WIN32)
    target_link_libraries(sysx PUBLIC ws2_32)
endif()

add_library(resultx INTERFACE)
add_library(toolx::resultx ALIAS resultx)
target_include_directories(resultx
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(resultx INTERFACE sysx)
set_target_properties(resultx PROPERTIES
    EXPORT_NAME resultx)

toolx_add_module(argtool)

toolx_add_module(asyncx)
target_link_libraries(asyncx PUBLIC sysx)

toolx_add_module(cfgx)

toolx_add_module(fsx)
target_link_libraries(fsx PUBLIC utils)

toolx_add_module(hashx)
target_link_libraries(hashx PUBLIC utils)

toolx_add_module(httpx)
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

toolx_add_module(logsys)
if(WIN32)
    target_link_libraries(logsys PUBLIC ws2_32)
endif()

toolx_add_module(textcodec)
target_link_libraries(textcodec PUBLIC utils)

toolx_add_module(tuix)

set(TOOLX_EXPORT_TARGETS
    utils
    sysx
    resultx
    argtool
    asyncx
    cfgx
    fsx
    hashx
    httpx
    logsys
    textcodec
    tuix
)

if(TOOLX_BUILD_TOOLS)
    add_executable(cfgtool examples/cfgtool_example.cpp)
    target_link_libraries(cfgtool PRIVATE argtool cfgx)

    add_executable(toolx_sync tools/toolx_sync.cpp)
    set_target_properties(toolx_sync PROPERTIES OUTPUT_NAME "toolx-sync")
    target_link_libraries(toolx_sync PRIVATE argtool asyncx cfgx fsx httpx logsys)
endif()

if(TOOLX_BUILD_EXAMPLES)
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

    add_executable(cfgx_httpx_remote_example examples/cfgx_httpx_remote_example.cpp)
    target_link_libraries(cfgx_httpx_remote_example PRIVATE cfgx httpx)

    add_executable(fsx_example examples/fsx_example.cpp)
    target_link_libraries(fsx_example PRIVATE fsx)

    add_executable(hashx_example examples/hashx_example.cpp)
    target_link_libraries(hashx_example PRIVATE hashx)

    add_executable(httpx_example examples/httpx_example.cpp)
    target_link_libraries(httpx_example PRIVATE httpx)

    add_executable(logsys_example examples/logsys_example.cpp)
    target_link_libraries(logsys_example PRIVATE logsys)

    add_executable(logsys_cfgx_configured_example examples/logsys_cfgx_configured_example.cpp)
    target_link_libraries(logsys_cfgx_configured_example PRIVATE cfgx logsys)

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

    add_executable(toolx_cli_showcase examples/toolx_cli_showcase.cpp)
    target_link_libraries(toolx_cli_showcase PRIVATE argtool asyncx cfgx fsx logsys textcodec)

    if(TOOLX_BUILD_BENCHMARKS)
        add_executable(asyncx_benchmark examples/asyncx_benchmark.cpp)
        target_link_libraries(asyncx_benchmark PRIVATE asyncx)

        add_executable(httpx_benchmark examples/httpx_benchmark.cpp)
        target_link_libraries(httpx_benchmark PRIVATE httpx)
    endif()
endif()

if(TOOLX_BUILD_TESTS)
    include(FetchContent)

    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
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

    add_executable(resultx_tests tests/resultx_tests.cpp)
    target_link_libraries(resultx_tests PRIVATE resultx GTest::gtest_main)
    add_test(NAME resultx_tests COMMAND resultx_tests)

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

    add_executable(toolx_integration_tests tests/toolx_integration_tests.cpp)
    target_link_libraries(toolx_integration_tests PRIVATE argtool asyncx cfgx fsx httpx logsys resultx GTest::gtest_main)
    add_test(NAME toolx_integration_tests COMMAND toolx_integration_tests)

    if(TARGET cfgtool)
        add_test(
            NAME cfgtool_cli_contracts
            COMMAND "${CMAKE_COMMAND}"
                -DCFGTOOL_EXE=$<TARGET_FILE:cfgtool>
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cfgtool_cli_contracts.cmake")
    endif()

    if(TARGET toolx_sync)
        add_test(
            NAME toolx_sync_cli_scenario
            COMMAND "${CMAKE_COMMAND}"
                -DTOOLX_SYNC_EXE=$<TARGET_FILE:toolx_sync>
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/toolx_sync_cli_scenario.cmake")
    endif()

    if(TOOLX_ENABLE_COVERAGE AND GCOVR_BIN)
        add_custom_target(coverage
            COMMAND "${CMAKE_CTEST_COMMAND}" --output-on-failure
            COMMAND "${GCOVR_BIN}"
                --root "${CMAKE_CURRENT_SOURCE_DIR}"
                --filter "${CMAKE_CURRENT_SOURCE_DIR}/src"
                --filter "${CMAKE_CURRENT_SOURCE_DIR}/include"
                --exclude "${CMAKE_CURRENT_SOURCE_DIR}/tests"
                --exclude "${CMAKE_CURRENT_SOURCE_DIR}/examples"
                --xml-pretty
                --output "${CMAKE_BINARY_DIR}/coverage.xml"
                --html-details
                --html-title "ToolX Coverage"
                --output "${CMAKE_BINARY_DIR}/coverage.html"
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            COMMENT "Running tests and generating gcovr coverage reports"
            VERBATIM)
    endif()
endif()

install(
    TARGETS ${TOOLX_EXPORT_TARGETS}
    EXPORT ToolXTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

if(TOOLX_BUILD_TOOLS)
    install(
        TARGETS cfgtool toolx_sync
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/ToolXConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ToolXConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/ToolXConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ToolX"
)

install(
    EXPORT ToolXTargets
    FILE ToolXTargets.cmake
    NAMESPACE toolx::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ToolX"
)

install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/ToolXConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/ToolXConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ToolX"
)
