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

cmake_minimum_required(VERSION 3.28...4.4)

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
cmake_policy(SET CMP0077 NEW)
cmake_policy(SET CMP0079 NEW)

include(FetchContent)

# Google Test
if(FASTSCHEMA_BUILD_TESTS)
    if(NOT TARGET GTest::gtest_main AND NOT TARGET gtest)
        FetchContent_Declare(
            GTest
            GIT_REPOSITORY https://github.com/google/googletest
            GIT_TAG v1.18.0
            OVERRIDE_FIND_PACKAGE
        )
        set(gtest_force_shared_crt ON)
        FetchContent_MakeAvailable(GTest)
    endif()
endif()

# Google Abseil
FetchContent_Declare(abseil
    GIT_REPOSITORY https://github.com/abseil/abseil-cpp
    GIT_TAG        20260817.0)
block()
    set(ABSL_BUILD_TESTING OFF)
    set(ABSL_ENABLE_INSTALL ON)
    set(ABSL_PROPAGATE_CXX_STD ON)
    set(CMAKE_CXX_STANDARD 23)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    FetchContent_MakeAvailable(abseil)
endblock()

# Google Highway
FetchContent_Declare(hwy
    GIT_REPOSITORY https://github.com/google/highway
    GIT_TAG 1.4.0
)
block()
    set(HWY_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
    set(HWY_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(HWY_ENABLE_CONTRIB OFF CACHE BOOL "" FORCE)
    if(TARGET gtest OR TARGET GTest::gtest_main)
        set(HWY_SYSTEM_GTEST ON CACHE BOOL "" FORCE)
    endif()
    FetchContent_MakeAvailable(hwy)
    unset(HWY_SYSTEM_GTEST CACHE)
endblock()

# libdeflate
FetchContent_Declare(libdeflate
    GIT_REPOSITORY https://github.com/ebiggers/libdeflate
    GIT_TAG v1.26)
block()
    set(LIBDEFLATE_BUILD_TESTS OFF)
    set(LIBDEFLATE_BUILD_SHARED_LIB OFF)
    set(LIBDEFLATE_BUILD_STATIC_LIB ON)
    FetchContent_MakeAvailable(libdeflate)

    if(TARGET libdeflate)
        target_include_directories(libdeflate INTERFACE $<BUILD_INTERFACE:${libdeflate_SOURCE_DIR}>)
    endif()
endblock()

# Intel oneTBB
FetchContent_Declare(oneTBB
    GIT_REPOSITORY https://github.com/oneapi-src/oneTBB
    GIT_TAG        v2023.1.0)
block()
    set(TBB_TEST OFF)
    set(TBB_EXAMPLES OFF)
    set(BUILD_SHARED_LIBS ON)
    FetchContent_MakeAvailable(oneTBB)
endblock()

# Google Benchmark
if(FASTSCHEMA_BUILD_BENCHMARKS)
    FetchContent_Declare(googlebenchmark
        GIT_REPOSITORY https://github.com/google/benchmark
        GIT_TAG v1.9.5)
    block()
        set(BENCHMARK_ENABLE_TESTING OFF)
        set(BENCHMARK_ENABLE_GTEST_TESTS OFF)
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
    fschema/base/compressor.cc
    fschema/base/error.cc
    fschema/base/nbt/parse.cc
    fschema/base/nbt/primitives.cc
    fschema/base/nbt/reader.cc
    fschema/base/nbt/skip.cc
    fschema/base/nbt/writer.cc
    fschema/memory/arena.cc
    fschema/litematic/parse.cc
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
    fschema/ir/format_handler.cc
    fschema/ir/litematic_handler.cc
    fschema/ir/manipulate.cc
    fschema/ir/materialize.cc
    fschema/ir/schem_handler.cc
    fschema/ir/internal/bit_pack.cc
    fschema/ir/internal/codec_utils.cc
    fschema/ir/internal/varint_pack.cc
)

# Private headers
target_sources(fschema
  PRIVATE
    fschema/ir/internal/bit_pack.h
    fschema/ir/internal/varint_pack.h
    fschema/litematic/internal/block_states.h
    fschema/litematic/internal/entity.h
    fschema/litematic/internal/metadata.h
    fschema/litematic/internal/palette.h
    fschema/litematic/internal/pending.h
    fschema/litematic/internal/region.h
    fschema/litematic/internal/root.h
    fschema/litematic/internal/tile_entity.h
    fschema/schem/internal/block_data.h
    fschema/schem/internal/block_entities.h
    fschema/schem/internal/block_state_string.h
    fschema/schem/internal/entities.h
    fschema/schem/internal/palette.h
    fschema/schem/internal/root.h
)

# Public headers
target_sources(fschema
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
    FILES
      fschema/base/block_utils.h
      fschema/base/byte_order.h
      fschema/base/compressor.h
      fschema/base/error.h
      fschema/base/hash.h
      fschema/base/limits.h
      fschema/base/nbt/parse.h
      fschema/base/nbt/primitives.h
      fschema/base/nbt/reader-inl.h
      fschema/base/nbt/reader.h
      fschema/base/nbt/scope.h
      fschema/base/nbt/skip.h
      fschema/base/nbt/tag-inl.h
      fschema/base/nbt/tag.h
      fschema/base/nbt/tree.h
      fschema/base/nbt/property.h
      fschema/base/nbt/writer.h
      fschema/base/port.h
      fschema/editors/bounding_box_trim.h
      fschema/editors/copy.h
      fschema/editors/erase_nbt.h
      fschema/editors/extract.h
      fschema/editors/fill.h
      fschema/editors/hollow.h
      fschema/editors/mirror.h
      fschema/editors/move.h
      fschema/editors/overlay.h
      fschema/editors/palette_utils.h
      fschema/editors/replace.h
      fschema/editors/replace_palette_entry.h
      fschema/editors/rotate.h
      fschema/filters/axis_filter.h
      fschema/filters/block_name_filter.h
      fschema/filters/block_state_filter.h
      fschema/filters/box_filter.h
      fschema/filters/composite_filter.h
      fschema/filters/concepts.h
      fschema/filters/nbt_filter.h
      fschema/filters/neighbor_filter.h
      fschema/filters/pos.h
      fschema/filters/random_filter.h
      fschema/filters/sphere_filter.h
      fschema/filters/surface_filter.h
      fschema/filters/view.h
      fschema/inspectors/count.h
      fschema/inspectors/diff.h
      fschema/inspectors/floating_check.h
      fschema/inspectors/list_entity.h
      fschema/inspectors/list_material.h
      fschema/inspectors/list_tile_entity.h
      fschema/inspectors/support_check.h
      fschema/inspectors/version_check.h
      fschema/ir/format_handler.h
      fschema/ir/litematic_handler.h
      fschema/ir/manipulate.h
      fschema/ir/materialize.h
      fschema/ir/schem_handler.h
      fschema/ir/types.h
      fschema/litematic/parse.h
      fschema/litematic/types.h
      fschema/memory/arena.h
      fschema/memory/uninit_buffer.h
      fschema/schem/parse.h
      fschema/schem/types.h
      fschema/editors/internal/state_transform.h
      fschema/filters/internal/neighbors.h
      fschema/inspectors/internal/block_version_data.h
      fschema/inspectors/internal/block_versions.inc
      fschema/inspectors/internal/data_versions.inc
      fschema/ir/internal/codec_utils.h
      fschema/third_party/pdqsort/pdqsort.h
      fschema/third_party/xxhash/xxhash.h
)

target_link_libraries(fschema
  PUBLIC
    absl::base
    absl::flat_hash_map
    absl::flat_hash_set
    absl::synchronization
    absl::time
  PRIVATE
    $<BUILD_INTERFACE:hwy>
    $<BUILD_INTERFACE:libdeflate::libdeflate_static>
    $<BUILD_INTERFACE:TBB::tbb>
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

    set(FSCHEMA_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/fschema")

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
        DESTINATION ${FSCHEMA_INSTALL_CMAKEDIR}
    )

    set(FSCHEMA_CONFIG_IN "${CMAKE_CURRENT_BINARY_DIR}/fschemaConfig.cmake.in")
    file(WRITE "${FSCHEMA_CONFIG_IN}"
"@PACKAGE_INIT@

include(CMakeFindDependencyMacro)
find_dependency(absl)

include(\"\${CMAKE_CURRENT_LIST_DIR}/fschemaTargets.cmake\")
check_required_components(fschema)
"
    )

    configure_package_config_file(
        "${FSCHEMA_CONFIG_IN}"
        "${CMAKE_CURRENT_BINARY_DIR}/fschemaConfig.cmake"
        INSTALL_DESTINATION ${FSCHEMA_INSTALL_CMAKEDIR}
    )

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/fschemaConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/fschemaConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/fschemaConfigVersion.cmake"
        DESTINATION ${FSCHEMA_INSTALL_CMAKEDIR}
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
    fschema_add_bench(fschema_convert_benchmark          fschema/benchmarks/convert_benchmark.cc)
endif()