cmake_minimum_required(VERSION 3.20)
project(polycpp_mysql2 VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

set(POLYCPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(POLYCPP_SOURCE_DIR "" CACHE PATH "Path to a local polycpp checkout")
set(POLYCPP_ICONV_LITE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(POLYCPP_ICONV_LITE_SOURCE_DIR "" CACHE PATH "Path to a local polycpp iconv-lite checkout")

if(NOT POLYCPP_ICONV_LITE_SOURCE_DIR AND EXISTS "/data/work/lib/iconv-lite/CMakeLists.txt")
    set(POLYCPP_ICONV_LITE_SOURCE_DIR "/data/work/lib/iconv-lite" CACHE PATH "Path to a local polycpp iconv-lite checkout" FORCE)
endif()

if(NOT POLYCPP_SOURCE_DIR AND EXISTS "/data/repo/polycpp/CMakeLists.txt")
    set(POLYCPP_SOURCE_DIR "/data/repo/polycpp" CACHE PATH "Path to a local polycpp checkout" FORCE)
endif()

if(POLYCPP_SOURCE_DIR AND EXISTS "${POLYCPP_SOURCE_DIR}/CMakeLists.txt")
    FetchContent_Declare(polycpp SOURCE_DIR "${POLYCPP_SOURCE_DIR}")
else()
    FetchContent_Declare(
        polycpp
        GIT_REPOSITORY git@github.com:enricohuang/polycpp.git
        GIT_TAG        master
        GIT_SHALLOW    TRUE
    )
endif()
FetchContent_MakeAvailable(polycpp)

if(POLYCPP_ICONV_LITE_SOURCE_DIR AND EXISTS "${POLYCPP_ICONV_LITE_SOURCE_DIR}/CMakeLists.txt")
    FetchContent_Declare(polycpp_iconv_lite SOURCE_DIR "${POLYCPP_ICONV_LITE_SOURCE_DIR}")
else()
    FetchContent_Declare(
        polycpp_iconv_lite
        GIT_REPOSITORY git@github.com:polycpp/iconv-lite.git
        GIT_TAG        main
        GIT_SHALLOW    TRUE
    )
endif()
FetchContent_MakeAvailable(polycpp_iconv_lite)

add_library(polycpp_mysql2
    src/aws_rds_ca.cpp
    src/mysql2.cpp
)
target_link_libraries(polycpp_mysql2 PUBLIC polycpp polycpp_iconv_lite)
target_compile_features(polycpp_mysql2 PUBLIC cxx_std_20)
target_include_directories(polycpp_mysql2 PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

add_library(polycpp::mysql2 ALIAS polycpp_mysql2)

set(POLYCPP_MYSQL2_BUILD_TESTS_DEFAULT OFF)
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(POLYCPP_MYSQL2_BUILD_TESTS_DEFAULT ON)
endif()
option(POLYCPP_MYSQL2_BUILD_TESTS "Build tests" ${POLYCPP_MYSQL2_BUILD_TESTS_DEFAULT})
if(POLYCPP_MYSQL2_BUILD_TESTS)
    enable_testing()
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2
    )
    FetchContent_MakeAvailable(googletest)

    include(GoogleTest)

    function(polycpp_mysql2_add_test target)
        add_executable(${target} ${ARGN})
        target_link_libraries(${target} PRIVATE polycpp_mysql2 GTest::gtest_main)
        target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        gtest_discover_tests(${target})
    endfunction()

    polycpp_mysql2_add_test(test_smoke tests/test_smoke.cpp)
    polycpp_mysql2_add_test(test_protocol_hardening tests/test_protocol_hardening.cpp)
    polycpp_mysql2_add_test(test_e2e_server_protocol tests/e2e/test_server_protocol.cpp)
    polycpp_mysql2_add_test(test_e2e_real_database tests/e2e/test_real_database.cpp)
    polycpp_mysql2_add_test(test_e2e_real_database_operations tests/e2e/test_real_database_operations.cpp)
    polycpp_mysql2_add_test(test_e2e_replication tests/e2e/test_replication.cpp)
endif()

option(POLYCPP_MYSQL2_BUILD_BENCHMARKS "Build benchmark tools" OFF)
option(POLYCPP_MYSQL2_BENCHMARK_NATIVE_C_API "Build optional native MySQL C API comparison benchmark" OFF)
if(POLYCPP_MYSQL2_BUILD_BENCHMARKS)
    add_executable(bench_mysql2 benchmarks/mysql2_benchmark.cpp)
    target_link_libraries(bench_mysql2 PRIVATE polycpp_mysql2)

    if(POLYCPP_MYSQL2_BENCHMARK_NATIVE_C_API)
        find_path(MYSQLCLIENT_INCLUDE_DIR NAMES mysql.h PATH_SUFFIXES mysql mariadb)
        find_library(MYSQLCLIENT_LIBRARY NAMES mysqlclient mariadb mariadbclient)
        if(NOT MYSQLCLIENT_INCLUDE_DIR OR NOT MYSQLCLIENT_LIBRARY)
            message(FATAL_ERROR "POLYCPP_MYSQL2_BENCHMARK_NATIVE_C_API=ON requires mysqlclient or mariadb Connector/C headers and library")
        endif()
        target_include_directories(bench_mysql2 PRIVATE "${MYSQLCLIENT_INCLUDE_DIR}")
        target_link_libraries(bench_mysql2 PRIVATE "${MYSQLCLIENT_LIBRARY}")
        target_compile_definitions(bench_mysql2 PRIVATE POLYCPP_MYSQL2_HAS_NATIVE_C_API=1)
    endif()
endif()

option(POLYCPP_MYSQL2_BUILD_EXAMPLES "Build examples" OFF)
if(POLYCPP_MYSQL2_BUILD_EXAMPLES)
    file(GLOB POLYCPP_MYSQL2_EXAMPLES CONFIGURE_DEPENDS examples/*.cpp)
    foreach(example_source IN LISTS POLYCPP_MYSQL2_EXAMPLES)
        get_filename_component(example_name "${example_source}" NAME_WE)
        add_executable("${example_name}" "${example_source}")
        target_link_libraries("${example_name}" PRIVATE polycpp_mysql2)
        set_target_properties("${example_name}" PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples"
        )
    endforeach()
endif()
