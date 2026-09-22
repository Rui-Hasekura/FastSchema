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

cmake_minimum_required(VERSION 4.4)

project(
    FastSchema
    VERSION 1.0.0
    DESCRIPTION "A high-performance modern C++ schematics tool library powered by SIMD."
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Options
option(FASTSCHEMA_BUILD_BENCHMARKS "Build FastSchema benchmark executable" ${PROJECT_IS_TOP_LEVEL})
option(FASTSCHEMA_BUILD_TESTS "Build FastSchema tests" ${PROJECT_IS_TOP_LEVEL})

# Dependencies
include(FetchContent)

# 1. Google Highway (Core)
FetchContent_Declare(
    hwy
    GIT_REPOSITORY https://github.com/google/highway.git
    GIT_TAG        1.4.0
)
set(HWY_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(HWY_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)

# 2. libdeflate (Core)
FetchContent_Declare(
    libdeflate
    GIT_REPOSITORY https://github.com/ebiggers/libdeflate.git
    GIT_TAG        v1.22
)
set(LIBDEFLATE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LIBDEFLATE_BUILD_GZIP OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# 3. Intel oneAPI TBB (Core)
FetchContent_Declare(
    oneTBB
    GIT_REPOSITORY https://github.com/oneapi-src/oneTBB.git
    GIT_TAG        v2021.13.0
)
set(TBB_TEST OFF CACHE BOOL "" FORCE)
set(TBB_EXAMPLES OFF CACHE BOOL "" FORCE)

# Download and make available core dependencies
FetchContent_MakeAvailable(hwy libdeflate oneTBB)

# 4. Google Test (Test only)
if(FASTSCHEMA_BUILD_TESTS)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.18.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

# 5. Google Benchmark (Benchmark only)
if(FASTSCHEMA_BUILD_BENCHMARKS)
    FetchContent_Declare(
        googlebenchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG        v1.9.5
    )
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googlebenchmark)
endif()

# Library Target
add_library(FastSchema STATIC
    # Sources
    src/parser/error.cc
    src/parser/unpacker.cc

    # NBT Sources
    src/parser/nbt/parse.cc
    src/parser/nbt/reader.cc
    src/parser/nbt/skip.cc
    src/parser/nbt/simd_bswap.cc

    # Litematic Sources
    src/parser/litematic/detail/block_states.cc
    src/parser/litematic/detail/entity.cc
    src/parser/litematic/detail/metadata.cc
    src/parser/litematic/detail/palette.cc
    src/parser/litematic/detail/pending.cc
    src/parser/litematic/detail/region.cc
    src/parser/litematic/detail/root.cc
    src/parser/litematic/detail/simd_block_states.cc
    src/parser/litematic/detail/tile_entity.cc
    src/parser/litematic/parse.cc

    # Schem Sources
    src/parser/schem/detail/block_data.cc
    src/parser/schem/detail/block_entities.cc
    src/parser/schem/detail/entities.cc
    src/parser/schem/detail/palette.cc
    src/parser/schem/detail/root.cc
    src/parser/schem/detail/simd_block_data.cc
    src/parser/schem/parse.cc

    # Common Headers
    include/parser/error.h
    include/parser/limits.h
    include/parser/nbt_tree.h
    include/parser/unpacker.h

    # NBT Parser Headers
    include/parser/nbt/noinit_allocator.h
    include/parser/nbt/noinit_allocator-inl.h
    include/parser/nbt/parse.h
    include/parser/nbt/reader.h
    include/parser/nbt/reader-inl.h
    include/parser/nbt/skip.h
    include/parser/nbt/tag.h
    include/parser/nbt/tag-inl.h

    # Litematic Parser Headers
    include/parser/litematic/parse.h
    include/parser/litematic/properties.h
    include/parser/litematic/types.h
    include/parser/litematic/detail/block_states.h
    include/parser/litematic/detail/entity.h
    include/parser/litematic/detail/metadata.h
    include/parser/litematic/detail/palette.h
    include/parser/litematic/detail/pending.h
    include/parser/litematic/detail/region.h
    include/parser/litematic/detail/root.h
    include/parser/litematic/detail/tile_entity.h

    # Schem Parser Headers
    include/parser/schem/parse.h
    include/parser/schem/block_state_string.h
    include/parser/schem/types.h
    include/parser/schem/detail/block_data.h
    include/parser/schem/detail/block_entities.h
    include/parser/schem/detail/entities.h
    include/parser/schem/detail/palette.h
    include/parser/schem/detail/root.h
)

# Compiler Options
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    if(MSVC)
        # Clang-CL on Windows
        target_compile_options(FastSchema PRIVATE
            /O2
            /Z7
            /W4
            /utf-8
        )
    else()
        # GCC / Clang
        target_compile_options(FastSchema PRIVATE
            -O2
            -g3
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()
elseif(MSVC)
    # MSVC
    target_compile_options(FastSchema PRIVATE
        /O2
        /Z7
        /W4
        /WX-
        /utf-8
    )
endif()

# Include Directories
target_include_directories(FastSchema
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link Libraries
target_link_libraries(FastSchema
    PUBLIC
        hwy
        libdeflate::libdeflate_static
    PRIVATE
        TBB::tbb
)

# Test Executable
if(FASTSCHEMA_BUILD_TESTS)
    include(CTest)
    include(GoogleTest)

    add_executable(fastschema_test
        test/litematic_parse.cc
    )

    target_compile_definitions(fastschema_test PRIVATE
        FASTSCHEMA_SAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/samples"
    )

    if(MSVC)
        target_link_options(fastschema_test PRIVATE /DEBUG:FULL)
    endif()

    target_link_libraries(fastschema_test
        PRIVATE
            FastSchema
            GTest::gtest_main
    )

    gtest_discover_tests(fastschema_test)
endif()

# Benchmark Executable
if(FASTSCHEMA_BUILD_BENCHMARKS)
    add_executable(fastschema_bench
        bench/schem_parse.cc
    )

    target_compile_definitions(fastschema_bench PRIVATE
        FASTSCHEMA_SAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/samples"
    )

    if(MSVC)
        target_link_options(fastschema_bench PRIVATE
            /DEBUG:FULL
        )
    endif()

    target_link_libraries(fastschema_bench
        PRIVATE
            FastSchema
            benchmark
            wsock32
            TBB::tbb
    )
endif()