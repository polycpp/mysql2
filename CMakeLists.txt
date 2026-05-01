cmake_minimum_required(VERSION 3.20)
project(polycpp_mysql2 VERSION 0.1.0 LANGUAGES CXX)

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

    add_executable(test_smoke tests/test_smoke.cpp)
    target_link_libraries(test_smoke PRIVATE polycpp_mysql2 GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(test_smoke)
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
