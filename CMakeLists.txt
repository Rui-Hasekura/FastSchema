# Copyright (C) 2026 Rui-Hasekura <ruihasekura@gmail.com>
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.26...4.4)

project(
    FastSchema
    VERSION 1.0.0
    DESCRIPTION "A high-performance modern C++ schematics tool library powered by SIMD."
    LANGUAGES CXX
)

include(GNUInstallDirs)

# Options
option(FASTSCHEMA_BUILD_BENCHMARKS "Build FastSchema benchmarks"
       ${PROJECT_IS_TOP_LEVEL})
option(FASTSCHEMA_BUILD_TESTS "Build FastSchema tests"
       ${PROJECT_IS_TOP_LEVEL})
option(FASTSCHEMA_INSTALL "Generate install target"
       ${PROJECT_IS_TOP_LEVEL})

# Dependencies
include(FetchContent)

# Highway (static)
FetchContent_Declare(hwy
    GIT_REPOSITORY https://github.com/google/highway.git
    GIT_TAG 1.4.0)
block()
    set(HWY_ENABLE_TESTS OFF)
    set(HWY_ENABLE_EXAMPLES OFF)
    FetchContent_MakeAvailable(hwy)
endblock()

# libdeflate (static)
FetchContent_Declare(libdeflate
    GIT_REPOSITORY https://github.com/ebiggers/libdeflate.git
    GIT_TAG v1.22)
block()
    set(LIBDEFLATE_BUILD_TESTS OFF)
    set(LIBDEFLATE_BUILD_SHARED_LIB OFF)
    set(LIBDEFLATE_BUILD_STATIC_LIB ON)
    FetchContent_MakeAvailable(libdeflate)
endblock()

# Intel oneTBB (shared)
FetchContent_Declare(oneTBB
    GIT_REPOSITORY https://github.com/oneapi-src/oneTBB.git
    GIT_TAG        v2021.13.0)
block()
    set(TBB_TEST OFF)
    set(TBB_EXAMPLES OFF)
    set(BUILD_SHARED_LIBS ON)
    FetchContent_MakeAvailable(oneTBB)
endblock()

if(FASTSCHEMA_BUILD_TESTS)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.18.0)
    block()
        set(gtest_force_shared_crt ON)
        FetchContent_MakeAvailable(googletest)
    endblock()
endif()

if(FASTSCHEMA_BUILD_BENCHMARKS)
    FetchContent_Declare(googlebenchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG v1.9.5)
    block()
        set(BENCHMARK_ENABLE_TESTING OFF)
        FetchContent_MakeAvailable(googlebenchmark)
    endblock()
endif()

add_library(fschema STATIC)
add_library(fschema::fschema ALIAS fschema)

target_compile_features(fschema PUBLIC cxx_std_23)
set_target_properties(fschema PROPERTIES CXX_EXTENSIONS OFF)

# Implementation sources
target_sources(fschema
  PRIVATE
    fschema/base/byte_order.cc
    fschema/base/decompression.cc
    fschema/base/error.cc
    fschema/base/nbt_parse.cc
    fschema/base/nbt_reader.cc
    fschema/base/nbt_skip.cc
    fschema/memory/arena.cc
    fschema/litematic/parse.cc
    fschema/litematic/internal/block_states.cc
    fschema/litematic/internal/entity.cc
    fschema/litematic/internal/metadata.cc
    fschema/litematic/internal/palette.cc
    fschema/litematic/internal/pending.cc
    fschema/litematic/internal/region.cc
    fschema/litematic/internal/root.cc
    fschema/litematic/internal/simd_block_states.cc
    fschema/litematic/internal/tile_entity.cc
    fschema/schem/parse.cc
    fschema/schem/internal/block_data.cc
    fschema/schem/internal/block_entities.cc
    fschema/schem/internal/block_state_string.cc
    fschema/schem/internal/entities.cc
    fschema/schem/internal/palette.cc
    fschema/schem/internal/root.cc
    fschema/schem/internal/simd_block_data.cc
)

# Public headers FILE_SET
file(GLOB_RECURSE FSCHEMA_ALL_HEADERS
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/fschema/*.h"
)

# Public
set(FSCHEMA_PUBLIC_HEADERS ${FSCHEMA_ALL_HEADERS})
list(FILTER FSCHEMA_PUBLIC_HEADERS EXCLUDE REGEX "/(internal|memory)/")

# Private
set(FSCHEMA_PRIVATE_HEADERS ${FSCHEMA_ALL_HEADERS})
list(FILTER FSCHEMA_PRIVATE_HEADERS INCLUDE REGEX "/(internal|memory)/")

target_sources(fschema
  PRIVATE
    ${FSCHEMA_PRIVATE_HEADERS}
)

target_sources(fschema
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
    FILES ${FSCHEMA_PUBLIC_HEADERS}
)

target_link_libraries(fschema
  PRIVATE
    hwy
    libdeflate::libdeflate_static
    TBB::tbb
)

# Compiler flags
if(MSVC)
    target_compile_options(fschema PRIVATE
        /W4
        /WX-
        /utf-8
        /Zc:__cplusplus
        /permissive-
    )
else()
    target_compile_options(fschema PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

#  Install / Export
if(FASTSCHEMA_INSTALL)
    include(CMakePackageConfigHelpers)

    install(TARGETS fschema
        EXPORT fschemaTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        FILE_SET HEADERS
    )

    install(EXPORT fschemaTargets
        FILE fschemaTargets.cmake
        NAMESPACE fschema::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fschema
    )

    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/fschemaConfig.cmake.in
        "@PACKAGE_INIT@
        include(CMakeFindDependencyMacro)
        find_dependency(hwy)
        find_dependency(TBB)
        find_dependency(libdeflate)
        include(\"\${CMAKE_CURRENT_LIST_DIR}/fschemaTargets.cmake\")
        check_required_components(fschema)"
    )

    configure_package_config_file(
        ${CMAKE_CURRENT_BINARY_DIR}/fschemaConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/fschemaConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fschema
    )

    write_basic_package_version_file(
        fschemaConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/fschemaConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/fschemaConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fschema
    )
endif()

# Tests (opt-in)
if(FASTSCHEMA_BUILD_TESTS)
    include(CTest)
    include(GoogleTest)

    function(fschema_add_test name src)
        add_executable(${name}
            ${src}
            fschema/tests/testdata_util.h
        )
        target_compile_definitions(${name} PRIVATE
            FASTSCHEMA_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fschema/testdata")
        target_link_libraries(${name} PRIVATE fschema hwy GTest::gtest_main)
        gtest_discover_tests(${name})
        # Copy TBB DLL next to the executable (Windows, shared TBB)
        if(WIN32 AND TARGET TBB::tbb)
            add_custom_command(TARGET ${name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:TBB::tbb>
                    $<TARGET_FILE_DIR:${name}>
            )
        endif()
    endfunction()

    fschema_add_test(fschema_litematic_parse_test fschema/tests/litematic_parse_test.cc)
    fschema_add_test(fschema_nbt_parse_test    fschema/tests/nbt_parse_test.cc)
    fschema_add_test(fschema_schem_parse_test  fschema/tests/schem_parse_test.cc)
endif()

# Benchmarks (opt-in)
if(FASTSCHEMA_BUILD_BENCHMARKS)
    function(fschema_add_bench name src)
        add_executable(${name}
            ${src}
            fschema/tests/testdata_util.h
        )
        target_compile_definitions(${name} PRIVATE
            FASTSCHEMA_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fschema/testdata")
        target_link_libraries(${name} PRIVATE fschema hwy benchmark TBB::tbb)
        # Copy TBB DLL next to the executable (Windows, shared TBB)
        if(WIN32 AND TARGET TBB::tbb)
            add_custom_command(TARGET ${name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:TBB::tbb>
                    $<TARGET_FILE_DIR:${name}>
            )
        endif()
    endfunction()

    fschema_add_bench(fschema_schem_parse_benchmark      fschema/benchmarks/schem_parse_benchmark.cc)
    fschema_add_bench(fschema_litematic_parse_benchmark  fschema/benchmarks/litematic_parse_benchmark.cc)
    fschema_add_bench(fschema_nbt_parse_benchmark        fschema/benchmarks/nbt_parse_benchmark.cc)
endif()