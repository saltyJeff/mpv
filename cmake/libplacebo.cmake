# mpv/cmake/libplacebo.cmake
# Custom CMake target for libplacebo from fetched source.

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(PLACEBO_UPSTREAM_DIR "${libplacebo_SOURCE_DIR}")
set(libplacebo_SOURCE_DIR "${libplacebo_BINARY_DIR}/msvc-source")
set(majorver 7)
set(apiver 371)
set(extra_defs "
#define PL_HAVE_VULKAN 1
#define PL_HAVE_VK_PROC_ADDR 1
#define PL_HAVE_LCMS 1
#define PL_HAVE_SHADERC 1
")

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_LIST_DIR}/apply_source_patches.py"
  "${CMAKE_CURRENT_LIST_DIR}/libplacebo-msvc-patches.json"
)
execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/apply_source_patches.py"
    "${CMAKE_CURRENT_LIST_DIR}/libplacebo-msvc-patches.json" "${PLACEBO_UPSTREAM_DIR}"
    --destination "${libplacebo_SOURCE_DIR}"
  COMMAND_ERROR_IS_FATAL ANY
)

configure_file(
  "${libplacebo_SOURCE_DIR}/src/include/libplacebo/config.h.in"
  "${libplacebo_BINARY_DIR}/include/libplacebo/config.h"
  @ONLY
)

file(MAKE_DIRECTORY "${libplacebo_BINARY_DIR}/src")
file(CONFIGURE OUTPUT "${libplacebo_BINARY_DIR}/src/config_internal.h" CONTENT
"#pragma once
#define BUILD_API_VER 371
#define BUILD_FIX_VER 0
#define PL_HAVE_DBGHELP 1

#ifdef _MSC_VER
#include <intrin.h>
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES 1
#endif
#ifndef __attribute__
#define __attribute__(x)
#endif
#ifndef __typeof__
#define __typeof__ typeof
#endif
#undef PL_NOINLINE
#define PL_NOINLINE __declspec(noinline)

#define __builtin_unreachable() __assume(0)
#define __builtin_popcount(x) (int)__popcnt(x)
#define __builtin_popcountl(x) (int)__popcnt(x)
#define __builtin_popcountll(x) (int)__popcnt64(x)

static inline int __builtin_ctz(unsigned int x) {
    unsigned long idx;
    return _BitScanForward(&idx, x) ? (int)idx : 32;
}

static inline int __builtin_ffsll(unsigned long long x) {
    unsigned long idx;
    return _BitScanForward64(&idx, x) ? (int)(idx + 1) : 0;
}

static inline int __builtin_clzll(unsigned long long x) {
    unsigned long idx;
    return _BitScanReverse64(&idx, x) ? 63 - (int)idx : 64;
}

#ifndef __cplusplus
typedef struct {
    long long __ll;
    long double __ld;
} max_align_t;
#endif
#endif
"
)

file(CONFIGURE OUTPUT "${libplacebo_BINARY_DIR}/src/version.h" CONTENT
"#pragma once
#define BUILD_VERSION \"v7.371.0\"
"
)

set(GLSL_PREPROC "${libplacebo_SOURCE_DIR}/tools/glsl_preproc/main.py")
set(PYTHON_ENV_DIRS
  "${PLACEBO_UPSTREAM_DIR}/3rdparty/jinja/src"
  "${PLACEBO_UPSTREAM_DIR}/3rdparty/markupsafe/src"
  "${PLACEBO_UPSTREAM_DIR}/3rdparty/glad"
)
cmake_path(CONVERT "${PYTHON_ENV_DIRS}" TO_NATIVE_PATH_LIST PYTHON_ENV_NATIVE)

function(generate_placebo_shader INPUT_REL OUTPUT_NAME)
  set(INPUT_SRC "${libplacebo_SOURCE_DIR}/${INPUT_REL}")
  set(OUTPUT_SRC "${libplacebo_BINARY_DIR}/${OUTPUT_NAME}")
  get_filename_component(OUT_DIR "${OUTPUT_SRC}" DIRECTORY)
  file(MAKE_DIRECTORY "${OUT_DIR}")
  add_custom_command(
    OUTPUT "${OUTPUT_SRC}"
    COMMAND ${CMAKE_COMMAND} -E env "PYTHONPATH=${PYTHON_ENV_NATIVE}"
            "${Python3_EXECUTABLE}" "${GLSL_PREPROC}" -s "${INPUT_SRC}" "${OUTPUT_SRC}"
    DEPENDS "${INPUT_SRC}" "${GLSL_PREPROC}"
    COMMENT "Pre-processing shader ${INPUT_REL}"
    VERBATIM
  )
  set(GEN_PLACEBO_SHADERS ${GEN_PLACEBO_SHADERS} "${OUTPUT_SRC}" PARENT_SCOPE)
endfunction()

set(GEN_PLACEBO_SHADERS "")
generate_placebo_shader("src/renderer.c" "renderer.gen.c")
generate_placebo_shader("src/shaders.c" "shaders.gen.c")
generate_placebo_shader("src/shaders/colorspace.c" "shaders/colorspace.gen.c")
generate_placebo_shader("src/shaders/custom.c" "shaders/custom.gen.c")
generate_placebo_shader("src/shaders/custom_mpv.c" "shaders/custom_mpv.gen.c")
generate_placebo_shader("src/shaders/deinterlacing.c" "shaders/deinterlacing.gen.c")
generate_placebo_shader("src/shaders/dithering.c" "shaders/dithering.gen.c")
generate_placebo_shader("src/shaders/film_grain.c" "shaders/film_grain.gen.c")
generate_placebo_shader("src/shaders/film_grain_av1.c" "shaders/film_grain_av1.gen.c")
generate_placebo_shader("src/shaders/film_grain_h274.c" "shaders/film_grain_h274.gen.c")
generate_placebo_shader("src/shaders/icc.c" "shaders/icc.gen.c")
generate_placebo_shader("src/shaders/lut.c" "shaders/lut.gen.c")
generate_placebo_shader("src/shaders/sampling.c" "shaders/sampling.gen.c")

# Vulkan utils_gen.c
# vulkan-headers provides vk.xml in registry/
FetchContent_GetProperties(vulkan_headers SOURCE_DIR VULKAN_HEADERS_DIR)
if(NOT VULKAN_HEADERS_DIR)
  set(VULKAN_HEADERS_DIR "${vulkan-headers_SOURCE_DIR}")
endif()
set(VULKAN_REGISTRY_XML "${VULKAN_HEADERS_DIR}/registry/vk.xml")
set(UTILS_GEN_PY "${libplacebo_SOURCE_DIR}/src/vulkan/utils_gen.py")
set(UTILS_GEN_C "${libplacebo_BINARY_DIR}/vulkan/utils_gen.c")
file(MAKE_DIRECTORY "${libplacebo_BINARY_DIR}/vulkan")

add_custom_command(
  OUTPUT "${UTILS_GEN_C}"
  COMMAND ${CMAKE_COMMAND} -E env "PYTHONPATH=${PYTHON_ENV_NATIVE}"
          "${Python3_EXECUTABLE}" "${UTILS_GEN_PY}" none "${VULKAN_REGISTRY_XML}" "${UTILS_GEN_C}"
  DEPENDS "${UTILS_GEN_PY}" "${VULKAN_REGISTRY_XML}"
  COMMENT "Generating Vulkan format tables utils_gen.c"
  VERBATIM
)

set(PLACEBO_SOURCES
  "${libplacebo_SOURCE_DIR}/src/cache.c"
  "${libplacebo_SOURCE_DIR}/src/colorspace.c"
  "${libplacebo_SOURCE_DIR}/src/common.c"
  "${libplacebo_SOURCE_DIR}/src/convert.cc"
  "${libplacebo_SOURCE_DIR}/src/dither.c"
  "${libplacebo_SOURCE_DIR}/src/dispatch.c"
  "${libplacebo_SOURCE_DIR}/src/dummy.c"
  "${libplacebo_SOURCE_DIR}/src/filters.c"
  "${libplacebo_SOURCE_DIR}/src/format.c"
  "${libplacebo_SOURCE_DIR}/src/gamut_mapping.c"
  "${libplacebo_SOURCE_DIR}/src/glsl/spirv.c"
  "${libplacebo_SOURCE_DIR}/src/glsl/spirv_shaderc.c"
  "${libplacebo_SOURCE_DIR}/src/gpu.c"
  "${libplacebo_SOURCE_DIR}/src/gpu/utils.c"
  "${libplacebo_SOURCE_DIR}/src/log.c"
  "${libplacebo_SOURCE_DIR}/src/options.c"
  "${libplacebo_SOURCE_DIR}/src/pl_alloc.c"
  "${libplacebo_SOURCE_DIR}/src/pl_string.c"
  "${libplacebo_SOURCE_DIR}/src/swapchain.c"
  "${libplacebo_SOURCE_DIR}/src/tone_mapping.c"
  "${libplacebo_SOURCE_DIR}/src/utils/dolbyvision.c"
  "${libplacebo_SOURCE_DIR}/src/utils/frame_queue.c"
  "${libplacebo_SOURCE_DIR}/src/utils/upload.c"
  ${GEN_PLACEBO_SHADERS}
  "${libplacebo_SOURCE_DIR}/src/vulkan/command.c"
  "${libplacebo_SOURCE_DIR}/src/vulkan/context.c"
  "${libplacebo_SOURCE_DIR}/src/vulkan/formats.c"
  "${libplacebo_SOURCE_DIR}/src/vulkan/gpu.c"
  "${libplacebo_SOURCE_DIR}/src/vulkan/gpu_buf.c"
  "${libplacebo_SOURCE_DIR}/src/vulkan/gpu_tex.c"
  "${libplacebo_SOURCE_DIR}/src/vulkan/gpu_pass.c"
  "${libplacebo_SOURCE_DIR}/src/vulkan/malloc.c"
  "${libplacebo_SOURCE_DIR}/src/vulkan/swapchain.c"
  "${libplacebo_SOURCE_DIR}/src/vulkan/utils.c"
  ${UTILS_GEN_C}
)

add_library(placebo STATIC ${PLACEBO_SOURCES})
set_target_properties(placebo PROPERTIES C_STANDARD 23 CXX_STANDARD 20)

target_compile_definitions(placebo PUBLIC PL_STATIC=1)
target_compile_definitions(placebo PRIVATE
  _GNU_SOURCE
  _USE_MATH_DEFINES
)

if(MSVC)
  target_compile_options(placebo PRIVATE
    /std:clatest
    /experimental:c11atomics
    /Zc:preprocessor
  )
endif()

target_include_directories(placebo PUBLIC
  "${libplacebo_SOURCE_DIR}/src/include"
  "${libplacebo_BINARY_DIR}/include"
)

target_include_directories(placebo PRIVATE
  "${libplacebo_SOURCE_DIR}/src"
  "${libplacebo_BINARY_DIR}"
  "${libplacebo_BINARY_DIR}/src"
  "${PLACEBO_UPSTREAM_DIR}/3rdparty/fast_float"
  "${PLACEBO_UPSTREAM_DIR}/3rdparty/glad"
  "${lcms2_SOURCE_DIR}/include"
)

target_link_libraries(placebo PUBLIC
  Vulkan::Headers
  lcms2
  shaderc
)

add_library(mpv_vulkan_loader STATIC "${CMAKE_CURRENT_LIST_DIR}/vulkan-loader-win.c")
target_link_libraries(mpv_vulkan_loader PRIVATE Vulkan::Headers)
target_link_libraries(placebo PUBLIC mpv_vulkan_loader shlwapi)

add_library(libplacebo::placebo ALIAS placebo)

