# Copyright (C) 2026 Rui-Hasekura <ruihasekura@gmail.com>
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 4.0)

project(
    FastSchema
    VERSION 0.5.0
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
option(FASTSCHEMA_BUILD_GEOMETRY "Build the geometry/tessellation subsystem"
       ${PROJECT_IS_TOP_LEVEL})

# Dependencies
include(FetchContent)

# Google Test
if(FASTSCHEMA_BUILD_TESTS)
    if(NOT TARGET GTest::gtest_main AND NOT TARGET gtest)
        FetchContent_Declare(
            GTest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG v1.18.0
            GIT_SHALLOW TRUE
            SYSTEM
            OVERRIDE_FIND_PACKAGE
        )
        block()
            set(gtest_force_shared_crt ON)
            FetchContent_MakeAvailable(GTest)
        endblock()
    endif()
endif()

# Google Abseil
if(NOT TARGET absl::base)
    FetchContent_Declare(abseil
        GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
        GIT_TAG        20260817.0
        SYSTEM)
    block()
        set(ABSL_BUILD_TESTING OFF)
        set(ABSL_ENABLE_INSTALL ON)
        set(ABSL_PROPAGATE_CXX_STD ON)
        set(CMAKE_CXX_STANDARD 23)
        set(CMAKE_CXX_STANDARD_REQUIRED ON)
        set(CMAKE_CXX_EXTENSIONS OFF)
        FetchContent_MakeAvailable(abseil)
    endblock()
endif()

# Google Highway
if(NOT TARGET hwy AND NOT TARGET hwy::hwy)
    FetchContent_Declare(hwy
        GIT_REPOSITORY https://github.com/google/highway.git
        GIT_TAG 1.4.0
        SYSTEM)
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
endif()
if(TARGET hwy AND NOT TARGET hwy::hwy)
    add_library(hwy::hwy ALIAS hwy)
endif()

# fast_float
if(NOT TARGET fast_float)
    FetchContent_Declare(fast_float
        GIT_REPOSITORY https://github.com/fastfloat/fast_float.git
        GIT_TAG v8.3.2
        GIT_SHALLOW TRUE
        SYSTEM)
    block()
        set(FASTFLOAT_INSTALL OFF)
        FetchContent_MakeAvailable(fast_float)
    endblock()
endif()

# libdeflate
if(NOT TARGET libdeflate::libdeflate_static)
    FetchContent_Declare(libdeflate
        GIT_REPOSITORY https://github.com/ebiggers/libdeflate.git
        GIT_TAG v1.26
        SYSTEM)
    block()
        set(LIBDEFLATE_BUILD_TESTS OFF)
        set(LIBDEFLATE_BUILD_SHARED_LIB OFF)
        set(LIBDEFLATE_BUILD_STATIC_LIB ON)
        set(LIBDEFLATE_INSTALL ${FASTSCHEMA_INSTALL} CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(libdeflate)
    endblock()
endif()

# Intel oneTBB
if(NOT TARGET TBB::tbb)
    FetchContent_Declare(oneTBB
        GIT_REPOSITORY https://github.com/oneapi-src/oneTBB.git
        GIT_TAG        v2023.2.0
        SYSTEM)
    block()
        set(TBB_TEST OFF)
        set(TBB_EXAMPLES OFF)
        set(BUILD_SHARED_LIBS ON)
        FetchContent_MakeAvailable(oneTBB)
    endblock()
endif()

# Google Benchmark
if(FASTSCHEMA_BUILD_BENCHMARKS)
    if(NOT TARGET benchmark::benchmark)
        FetchContent_Declare(googlebenchmark
            GIT_REPOSITORY https://github.com/google/benchmark.git
            GIT_TAG v1.10.0
            SYSTEM)
        block()
            set(BENCHMARK_ENABLE_TESTING OFF)
            set(BENCHMARK_ENABLE_GTEST_TESTS OFF)
            FetchContent_MakeAvailable(googlebenchmark)
        endblock()
    endif()
endif()

# Component libraries
#
#   fschema::nbt         base/ and base/nbt/ plus the memory primitives they use
#   fschema::litematica  Litematica parser and its dedicated structures
#   fschema::sponge      Sponge parser and its dedicated structures
#   fschema::ir          format-independent IR core and materialization
#   fschema::conversion  IR + remap + format handlers (Convert)
#   fschema::tools       filters, views, editors, inspectors
#   fschema::geometry    tessellation and export
#   fschema::fschema     umbrella target linking everything available

function(fschema_configure_library target)
    target_compile_features(${target} PUBLIC cxx_std_23)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)

    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /WX-
            /utf-8
            /Zc:__cplusplus
            /permissive-
        )
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()
endfunction()

# fschema::nbt
add_library(fschema_nbt STATIC)
add_library(fschema::nbt ALIAS fschema_nbt)
set_target_properties(fschema_nbt PROPERTIES EXPORT_NAME nbt)
fschema_configure_library(fschema_nbt)

target_sources(fschema_nbt
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

    fschema/base/nbt/primitives.h
    fschema/base/nbt/scope.h
)

target_sources(fschema_nbt
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
      fschema/base/port.h
      fschema/base/random.h
      fschema/base/nbt/parse.h
      fschema/base/nbt/property.h
      fschema/base/nbt/reader-inl.h
      fschema/base/nbt/reader.h
      fschema/base/nbt/skip.h
      fschema/base/nbt/tag-inl.h
      fschema/base/nbt/tag.h
      fschema/base/nbt/tree.h
      fschema/base/nbt/writer.h
      fschema/memory/arena.h
      fschema/memory/uninit_buffer.h
      fschema/third_party/xxhash/xxhash.h
)

target_link_libraries(fschema_nbt
  PRIVATE
    absl::base
    hwy::hwy
    libdeflate::libdeflate_static
)

# fschema::litematica
add_library(fschema_litematica STATIC)
add_library(fschema::litematica ALIAS fschema_litematica)
set_target_properties(fschema_litematica PROPERTIES EXPORT_NAME litematica)
fschema_configure_library(fschema_litematica)

target_sources(fschema_litematica
  PRIVATE
    fschema/litematica/parse.cc
    fschema/litematica/internal/entity.cc
    fschema/litematica/internal/metadata.cc
    fschema/litematica/internal/palette.cc
    fschema/litematica/internal/pending.cc
    fschema/litematica/internal/region.cc
    fschema/litematica/internal/root.cc
    fschema/litematica/internal/simd_block_states.cc
    fschema/litematica/internal/tile_entity.cc

    fschema/litematica/internal/block_states.h
    fschema/litematica/internal/entity.h
    fschema/litematica/internal/metadata.h
    fschema/litematica/internal/palette.h
    fschema/litematica/internal/pending.h
    fschema/litematica/internal/region.h
    fschema/litematica/internal/root.h
    fschema/litematica/internal/tile_entity.h
)

target_sources(fschema_litematica
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
    FILES
      fschema/litematica/parse.h
      fschema/litematica/types.h
)

target_link_libraries(fschema_litematica
  PUBLIC
    fschema::nbt
  PRIVATE
    hwy::hwy
    TBB::tbb
)

# fschema::sponge
add_library(fschema_sponge STATIC)
add_library(fschema::sponge ALIAS fschema_sponge)
set_target_properties(fschema_sponge PROPERTIES EXPORT_NAME sponge)
fschema_configure_library(fschema_sponge)

target_sources(fschema_sponge
  PRIVATE
    fschema/sponge/parse.cc
    fschema/sponge/internal/block_data.cc
    fschema/sponge/internal/block_entities.cc
    fschema/sponge/internal/block_state_string.cc
    fschema/sponge/internal/entities.cc
    fschema/sponge/internal/palette.cc
    fschema/sponge/internal/root.cc
    fschema/sponge/internal/simd_block_data.cc

    fschema/sponge/internal/block_data.h
    fschema/sponge/internal/block_entities.h
    fschema/sponge/internal/block_state_string.h
    fschema/sponge/internal/entities.h
    fschema/sponge/internal/palette.h
    fschema/sponge/internal/root.h
)

target_sources(fschema_sponge
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
    FILES
      fschema/sponge/parse.h
      fschema/sponge/types.h
)

target_link_libraries(fschema_sponge
  PUBLIC
    fschema::nbt
  PRIVATE
    hwy::hwy
    TBB::tbb
)

# fschema::ir
add_library(fschema_ir STATIC)
add_library(fschema::ir ALIAS fschema_ir)
set_target_properties(fschema_ir PROPERTIES EXPORT_NAME ir)
fschema_configure_library(fschema_ir)

target_sources(fschema_ir
  PRIVATE
    fschema/ir/manipulate.cc
    fschema/ir/materialize.cc
    fschema/ir/internal/bit_pack.cc
    fschema/ir/internal/codec_utils.cc
    fschema/ir/internal/varint_pack.cc

    fschema/ir/internal/bit_pack.h
    fschema/ir/internal/varint_pack.h
)

target_sources(fschema_ir
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
    FILES
      fschema/ir/internal/codec_utils.h
      fschema/ir/manipulate.h
      fschema/ir/materialize.h
      fschema/ir/types.h
)

target_link_libraries(fschema_ir
  PUBLIC
    fschema::litematica
    fschema::sponge
  PRIVATE
    absl::flat_hash_map
    TBB::tbb
)

# fschema::conversion
add_library(fschema_conversion STATIC)
add_library(fschema::conversion ALIAS fschema_conversion)
set_target_properties(fschema_conversion PROPERTIES EXPORT_NAME conversion)
fschema_configure_library(fschema_conversion)

target_sources(fschema_conversion
  PRIVATE
    fschema/ir/format_handler.cc
    fschema/ir/litematica_handler.cc
    fschema/ir/sponge_handler.cc
    fschema/remap/remap.cc
    fschema/remap/internal/block_entity.cc
    fschema/remap/internal/component_extra.cc
    fschema/remap/internal/context.cc
    fschema/remap/internal/entity.cc
    fschema/remap/internal/item.cc
    fschema/remap/internal/nbt_utils.cc
    fschema/remap/internal/palette.cc
    fschema/remap/internal/text.cc
    fschema/remap/internal/walker.cc

    fschema/remap/internal/block_entity.h
    fschema/remap/internal/component_extra.h
    fschema/remap/internal/context.h
    fschema/remap/internal/entity.h
    fschema/remap/internal/families.h
    fschema/remap/internal/item.h
    fschema/remap/internal/nbt_utils.h
    fschema/remap/internal/palette.h
    fschema/remap/internal/rule.h
    fschema/remap/internal/text.h
    fschema/remap/internal/walker.h
    fschema/remap/internal/tables/attribute_renames.inc
    fschema/remap/internal/tables/banner_colors.inc
    fschema/remap/internal/tables/banner_patterns.inc
    fschema/remap/internal/tables/beacon_effects.inc
    fschema/remap/internal/tables/block_id_renames.inc
    fschema/remap/internal/tables/component_map.inc
    fschema/remap/internal/tables/decoration_types.inc
    fschema/remap/internal/tables/potion_effect_ids.inc
    fschema/remap/internal/tables/tile_entity_id_guesses.inc
)

target_sources(fschema_conversion
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
    FILES
      fschema/ir/format_handler.h
      fschema/ir/litematica_handler.h
      fschema/ir/sponge_handler.h
      fschema/remap/remap.h
)

target_link_libraries(fschema_conversion
  PUBLIC
    fschema::ir
    absl::inlined_vector
    absl::synchronization
  PRIVATE
    absl::flat_hash_map
    absl::time
)

# fschema::tools
add_library(fschema_tools INTERFACE)
add_library(fschema::tools ALIAS fschema_tools)
set_target_properties(fschema_tools PROPERTIES EXPORT_NAME tools)
target_compile_features(fschema_tools INTERFACE cxx_std_23)

target_sources(fschema_tools
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
    FILES
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
      fschema/editors/internal/state_transform.h
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
      fschema/filters/internal/neighbors.h
      fschema/inspectors/count.h
      fschema/inspectors/diff.h
      fschema/inspectors/floating_check.h
      fschema/inspectors/list_entity.h
      fschema/inspectors/list_material.h
      fschema/inspectors/list_tile_entity.h
      fschema/inspectors/support_check.h
      fschema/inspectors/version_check.h
      fschema/inspectors/internal/block_version_data.h
      fschema/inspectors/internal/block_versions.inc
      fschema/inspectors/internal/data_versions.inc
      fschema/third_party/pdqsort/pdqsort.h
)

target_link_libraries(fschema_tools
  INTERFACE
    fschema::ir
    absl::any_invocable
    absl::flat_hash_map
    absl::flat_hash_set
    hwy::hwy
    TBB::tbb
)

# fschema::geometry
if(FASTSCHEMA_BUILD_GEOMETRY)
    add_library(fschema_geometry STATIC)
    add_library(fschema::geometry ALIAS fschema_geometry)
    set_target_properties(fschema_geometry PROPERTIES EXPORT_NAME geometry)
    fschema_configure_library(fschema_geometry)

    target_sources(fschema_geometry
      PRIVATE
        fschema/geometry/atlas.cc
        fschema/geometry/block_props.cc
        fschema/geometry/block_volume.cc
        fschema/geometry/export.cc
        fschema/geometry/resource_pack.cc
        fschema/geometry/tessellate.cc
        fschema/geometry/tint.cc
        fschema/geometry/internal/ao.cc
        fschema/geometry/internal/chunk_dispatch.cc
        fschema/geometry/internal/cull.cc
        fschema/geometry/internal/merge.cc
        fschema/geometry/internal/png_decode.cc
        fschema/geometry/internal/png_encode.cc
        fschema/geometry/internal/resolver.cc
        fschema/third_party/simdjson/simdjson.cpp

        fschema/geometry/internal/ao.h
        fschema/geometry/internal/chunk_dispatch.h
        fschema/geometry/internal/cull.h
        fschema/geometry/internal/merge.h
        fschema/geometry/internal/png_decode.h
        fschema/geometry/internal/png_encode.h
        fschema/geometry/internal/resolver.h
        fschema/third_party/simdjson/simdjson.h
    )

    target_sources(fschema_geometry
      PUBLIC
        FILE_SET HEADERS
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES
          fschema/geometry/atlas.h
          fschema/geometry/block_props.h
          fschema/geometry/block_volume.h
          fschema/geometry/export.h
          fschema/geometry/geometry.h
          fschema/geometry/resource_pack.h
          fschema/geometry/tessellate.h
          fschema/geometry/tint.h
          fschema/geometry/types.h
    )

    target_link_libraries(fschema_geometry
      PUBLIC
        fschema::tools
      PRIVATE
        absl::hash
        absl::synchronization
        $<BUILD_INTERFACE:fast_float>
    )

    target_compile_definitions(fschema_geometry PUBLIC FSCHEMA_HAS_GEOMETRY=1)
endif()

# Umbrella target
add_library(fschema INTERFACE)
add_library(fschema::fschema ALIAS fschema)
set_target_properties(fschema PROPERTIES EXPORT_NAME fschema)
target_compile_features(fschema INTERFACE cxx_std_23)

target_link_libraries(fschema
  INTERFACE
    fschema::conversion
    fschema::tools
)

if(FASTSCHEMA_BUILD_GEOMETRY)
    target_link_libraries(fschema INTERFACE fschema::geometry)
endif()

# Install / Export
if(FASTSCHEMA_INSTALL)
    include(CMakePackageConfigHelpers)

    set(FSCHEMA_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/fschema")

    set(FSCHEMA_LIBRARY_TARGETS
        fschema_nbt
        fschema_litematica
        fschema_sponge
        fschema_ir
        fschema_conversion
    )
    set(FSCHEMA_INTERFACE_TARGETS
        fschema_tools
        fschema
    )
    if(FASTSCHEMA_BUILD_GEOMETRY)
        list(APPEND FSCHEMA_LIBRARY_TARGETS fschema_geometry)
    endif()

    install(TARGETS ${FSCHEMA_LIBRARY_TARGETS}
        EXPORT fschemaTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(TARGETS ${FSCHEMA_INTERFACE_TARGETS}
        EXPORT fschemaTargets
        FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(EXPORT fschemaTargets
        FILE fschemaTargets.cmake
        NAMESPACE fschema::
        DESTINATION ${FSCHEMA_INSTALL_CMAKEDIR}
    )

    set(FSCHEMA_CONFIG_IN "${CMAKE_CURRENT_BINARY_DIR}/fschemaConfig.cmake.in")
    file(WRITE "${FSCHEMA_CONFIG_IN}" [=[
@PACKAGE_INIT@

include(CMakeFindDependencyMacro)
find_dependency(absl)
find_dependency(TBB)
find_dependency(hwy)
find_dependency(libdeflate)

include("${CMAKE_CURRENT_LIST_DIR}/fschemaTargets.cmake")
check_required_components(fschema)
]=])

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

    function(fschema_add_test name)
        cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;LINKS")
        if(NOT arg_SOURCES)
            message(FATAL_ERROR "fschema_add_test(${name}): SOURCES is required")
        endif()

        add_executable(${name}
            ${arg_SOURCES}
            fschema/tests/testdata_util.h
        )
        target_compile_definitions(${name} PRIVATE
            FASTSCHEMA_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fschema/testdata")
        target_link_libraries(${name} PRIVATE
            ${arg_LINKS}
            GTest::gtest_main
        )

        gtest_discover_tests(${name} DISCOVERY_MODE PRE_TEST)

        # Copy TBB DLL next to the executable (Windows, shared TBB)
        if(WIN32 AND TARGET TBB::tbb)
            add_custom_command(TARGET ${name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:TBB::tbb>
                    $<TARGET_FILE_DIR:${name}>
            )
        endif()
    endfunction()

    # Parsing
    fschema_add_test(fschema_litematica_parse_test
        SOURCES fschema/tests/litematica_parse_test.cc
        LINKS fschema::litematica)
    fschema_add_test(fschema_nbt_parse_test
        SOURCES fschema/tests/nbt_parse_test.cc
        LINKS fschema::nbt)
    fschema_add_test(fschema_sponge_parse_test
        SOURCES fschema/tests/sponge_parse_test.cc
        LINKS fschema::sponge)

    # Tools
    fschema_add_test(fschema_filter_test
        SOURCES fschema/tests/filter_test.cc
        LINKS fschema::conversion fschema::tools)
    fschema_add_test(fschema_inspector_test
        SOURCES fschema/tests/inspector_test.cc
        LINKS fschema::conversion fschema::tools)
    fschema_add_test(fschema_editor_test
        SOURCES fschema/tests/editor_test.cc
        LINKS fschema::conversion fschema::tools)

    # Geometry (opt-in)
    if(FASTSCHEMA_BUILD_GEOMETRY)
        fschema_add_test(fschema_geometry_block_props_test
            SOURCES fschema/geometry/tests/block_props_test.cc
            LINKS fschema::geometry)
        fschema_add_test(fschema_geometry_block_volume_test
            SOURCES fschema/geometry/tests/block_volume_test.cc
            LINKS fschema::geometry)
        fschema_add_test(fschema_geometry_resource_pack_test
            SOURCES fschema/geometry/tests/resource_pack_test.cc
                    fschema/geometry/tests/geometry_test_util.h
            LINKS fschema::geometry)
        fschema_add_test(fschema_geometry_resolve_test
            SOURCES fschema/geometry/tests/resolve_test.cc
                    fschema/geometry/tests/resolve_test_util.h
            LINKS fschema::geometry)
        fschema_add_test(fschema_geometry_resolve_rotation_test
            SOURCES fschema/geometry/tests/resolve_rotation_test.cc
                    fschema/geometry/tests/resolve_test_util.h
            LINKS fschema::geometry)
        fschema_add_test(fschema_geometry_png_decode_test
            SOURCES fschema/geometry/tests/png_decode_test.cc
                    fschema/geometry/tests/png_writer.h
            LINKS fschema::geometry)
        fschema_add_test(fschema_geometry_atlas_test
            SOURCES fschema/geometry/tests/atlas_test.cc
            LINKS fschema::geometry)
        fschema_add_test(fschema_geometry_tessellate_test
            SOURCES fschema/geometry/tests/tessellate_test.cc
                    fschema/geometry/tests/geometry_test_util.h
            LINKS fschema::geometry)
        fschema_add_test(fschema_geometry_fluid_tessellate_test
            SOURCES fschema/geometry/tests/fluid_tessellate_test.cc
                    fschema/geometry/tests/geometry_test_util.h
            LINKS fschema::geometry)
        fschema_add_test(fschema_geometry_glb_test
            SOURCES fschema/geometry/tests/glb_test.cc
                    fschema/geometry/tests/geometry_test_util.h
            LINKS fschema::geometry)
        fschema_add_test(fschema_geometry_parallel_test
            SOURCES fschema/geometry/tests/parallel_test.cc
                    fschema/geometry/tests/geometry_test_util.h
            LINKS fschema::geometry)
        fschema_add_test(fschema_geometry_cull_test
            SOURCES fschema/geometry/tests/cull_test.cc
                    fschema/geometry/tests/geometry_test_util.h
            LINKS fschema::geometry)
        fschema_add_test(fschema_geometry_ao_test
            SOURCES fschema/geometry/tests/ao_test.cc
                    fschema/geometry/tests/geometry_test_util.h
            LINKS fschema::geometry)
        fschema_add_test(fschema_geometry_merge_test
            SOURCES fschema/geometry/tests/merge_test.cc
                    fschema/geometry/tests/geometry_test_util.h
            LINKS fschema::geometry)
        fschema_add_test(fschema_geometry_schematic_test
            SOURCES fschema/geometry/tests/schematic_test.cc
                    fschema/geometry/tests/geometry_test_util.h
            LINKS fschema::geometry fschema::conversion)
        fschema_add_test(fschema_geometry_export_test
            SOURCES fschema/geometry/tests/export_test.cc
                    fschema/geometry/tests/geometry_test_util.h
            LINKS fschema::geometry)
    endif()
endif()

# Benchmarks (opt-in)
if(FASTSCHEMA_BUILD_BENCHMARKS)
    function(fschema_add_bench name)
        cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;LINKS")
        if(NOT arg_SOURCES)
            message(FATAL_ERROR "fschema_add_bench(${name}): SOURCES is required")
        endif()

        add_executable(${name}
            ${arg_SOURCES}
            fschema/tests/testdata_util.h
        )
        target_compile_definitions(${name} PRIVATE
            FASTSCHEMA_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fschema/testdata")
        target_link_libraries(${name} PRIVATE
            ${arg_LINKS}
            benchmark::benchmark
            hwy::hwy
        )

        # Copy TBB DLL next to the executable (Windows, shared TBB)
        if(WIN32 AND TARGET TBB::tbb)
            add_custom_command(TARGET ${name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:TBB::tbb>
                    $<TARGET_FILE_DIR:${name}>
            )
        endif()
    endfunction()

    # Parsing
    fschema_add_bench(fschema_sponge_parse_benchmark
        SOURCES fschema/benchmarks/sponge_parse_benchmark.cc
        LINKS fschema::sponge)
    fschema_add_bench(fschema_litematica_parse_benchmark
        SOURCES fschema/benchmarks/litematica_parse_benchmark.cc
        LINKS fschema::litematica)
    fschema_add_bench(fschema_nbt_parse_benchmark
        SOURCES fschema/benchmarks/nbt_parse_benchmark.cc
        LINKS fschema::nbt)

    # Conversion
    fschema_add_bench(fschema_convert_benchmark
        SOURCES fschema/benchmarks/convert_benchmark.cc
        LINKS fschema::conversion)

    # Tools
    fschema_add_bench(fschema_filter_benchmark
        SOURCES fschema/benchmarks/filter_benchmark.cc
        LINKS fschema::conversion fschema::tools)
    fschema_add_bench(fschema_inspector_benchmark
        SOURCES fschema/benchmarks/inspector_benchmark.cc
        LINKS fschema::conversion fschema::tools)
    fschema_add_bench(fschema_editor_benchmark
        SOURCES fschema/benchmarks/editor_benchmark.cc
        LINKS fschema::conversion fschema::tools)

    # Geometry (opt-in)
    if(FASTSCHEMA_BUILD_GEOMETRY)
        fschema_add_bench(fschema_geometry_benchmark
            SOURCES fschema/benchmarks/geometry_benchmark.cc
                    fschema/geometry/tests/png_writer.h
            LINKS fschema::geometry)
    endif()
endif()
