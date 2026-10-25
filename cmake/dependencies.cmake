# mpv/cmake/dependencies.cmake
# FetchContent orchestrator for all libmpv dependencies.

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
set(SKIP_INSTALL_ALL ON)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# 1. Vulkan Headers
FetchContent_Declare(
  vulkan_headers
  GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
  # Match libplacebo's pinned Vulkan-Headers submodule.
  GIT_TAG        74d8a6cb930c68ef617b202c3ff3c59d919e086b
)
set(VULKAN_HEADERS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(VULKAN_HEADERS_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(vulkan_headers)

# Header-only CUDA/NVDEC API. Keep this in FetchContent rather than an
# untracked nested Git checkout in the fork's source tree.
FetchContent_Declare(ffnvcodec
  GIT_REPOSITORY https://github.com/FFmpeg/nv-codec-headers.git
  GIT_TAG c69278340ab1d5559c7d7bf0edf615dc33ddbba7
  SOURCE_SUBDIR cmake-unused
)
FetchContent_MakeAvailable(ffnvcodec)
add_library(ffnvcodec INTERFACE)
target_include_directories(ffnvcodec INTERFACE "${ffnvcodec_SOURCE_DIR}/include")

# 2. zlib
FetchContent_Declare(
  zlib
  GIT_REPOSITORY https://github.com/madler/zlib.git
  GIT_TAG        v1.3.1
  GIT_SHALLOW    TRUE
  OVERRIDE_FIND_PACKAGE
)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(zlib)
if(TARGET zlibstatic AND NOT TARGET ZLIB::ZLIB)
  add_library(ZLIB::ZLIB ALIAS zlibstatic)
endif()
set(ZLIB_LIBRARIES ZLIB::ZLIB)
set(ZLIB_INCLUDE_DIRS "${zlib_SOURCE_DIR};${zlib_BINARY_DIR}")

# 3. Little CMS 2
FetchContent_Declare(
  lcms2
  GIT_REPOSITORY https://github.com/mm2/Little-CMS.git
  GIT_TAG        lcms2.16
  GIT_SHALLOW    TRUE
)
set(CMS_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(CMS_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(CMS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(lcms2)
include("${CMAKE_CURRENT_LIST_DIR}/lcms2.cmake")

# 4. FreeType
FetchContent_Declare(
  freetype
  GIT_REPOSITORY https://github.com/freetype/freetype.git
  GIT_TAG        VER-2-13-3
  GIT_SHALLOW    TRUE
)
set(FT_DISABLE_ZLIB OFF CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(freetype)

# 5. HarfBuzz
FetchContent_Declare(
  harfbuzz
  GIT_REPOSITORY https://github.com/harfbuzz/harfbuzz.git
  GIT_TAG        10.1.0
  GIT_SHALLOW    TRUE
)
set(HB_HAVE_FREETYPE ON CACHE BOOL "" FORCE)
set(HB_BUILD_SUBSET OFF CACHE BOOL "" FORCE)
set(HB_BUILD_UTILS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(harfbuzz)

# 6. Shaderc
FetchContent_Declare(
  shaderc
  GIT_REPOSITORY https://github.com/google/shaderc.git
  GIT_TAG        v2024.3
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  cmake-unused
)
set(SHADERC_SKIP_TESTS ON CACHE BOOL "" FORCE)
set(SHADERC_SKIP_EXAMPLES ON CACHE BOOL "" FORCE)
set(SHADERC_SKIP_COPYRIGHT_CHECK ON CACHE BOOL "" FORCE)
set(SHADERC_ENABLE_SHARED_CRT ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(shaderc)

# Shaderc's DEPS entries are not Git submodules. Pin the exact revisions
# from v2024.3/DEPS and let FetchContent manage the complete compiler closure.
FetchContent_Declare(shaderc_spirv_headers
  GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Headers.git
  GIT_TAG 2a9b6f951c7d6b04b6c21fe1bf3f475b68b84801
  SOURCE_SUBDIR cmake-unused
)
FetchContent_Declare(shaderc_spirv_tools
  GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Tools.git
  GIT_TAG 01c8438ee4ac52c248119b7e03e0b021f853b51a
  SOURCE_SUBDIR cmake-unused
)
FetchContent_Declare(shaderc_glslang
  GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
  GIT_TAG 467ce01c71e38cf01814c48987a5c0dadd914df4
  SOURCE_SUBDIR cmake-unused
)
FetchContent_MakeAvailable(shaderc_spirv_headers shaderc_spirv_tools shaderc_glslang)
set(SHADERC_SPIRV_HEADERS_DIR "${shaderc_spirv_headers_SOURCE_DIR}" CACHE PATH "" FORCE)
set(SHADERC_SPIRV_TOOLS_DIR "${shaderc_spirv_tools_SOURCE_DIR}" CACHE PATH "" FORCE)
set(SHADERC_GLSLANG_DIR "${shaderc_glslang_SOURCE_DIR}" CACHE PATH "" FORCE)
set(SHADERC_SKIP_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${shaderc_SOURCE_DIR}" "${shaderc_BINARY_DIR}" EXCLUDE_FROM_ALL)

FetchContent_Declare(spirv_cross
  GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Cross.git
  GIT_TAG ebe2aa0cd80f5eb5cd8a605da604cacf72205f3b
)
set(SPIRV_CROSS_SHARED OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_STATIC ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_C_API ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_HLSL ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SKIP_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(spirv_cross)

# 7. FriBidi
FetchContent_Declare(
  fribidi
  GIT_REPOSITORY https://github.com/fribidi/fribidi.git
  GIT_TAG        v1.0.16
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  cmake-unused
)
FetchContent_MakeAvailable(fribidi)
include("${CMAKE_CURRENT_LIST_DIR}/fribidi.cmake")

# 8. libass
FetchContent_Declare(
  libass
  GIT_REPOSITORY https://github.com/libass/libass.git
  GIT_TAG        0.17.3
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  cmake-unused
)
FetchContent_MakeAvailable(libass)
include("${CMAKE_CURRENT_LIST_DIR}/libass.cmake")

# 9. libplacebo
FetchContent_Declare(
  libplacebo
  GIT_REPOSITORY https://github.com/haasn/libplacebo.git
  GIT_TAG        3330a515d62139259c26239014f286e233bd3a5c
  GIT_SUBMODULES 3rdparty/glad 3rdparty/jinja 3rdparty/markupsafe 3rdparty/fast_float
  SOURCE_SUBDIR cmake-unused
)
FetchContent_MakeAvailable(libplacebo)
include("${CMAKE_CURRENT_LIST_DIR}/libplacebo.cmake")

# AV1 software decoding fallback. FFmpeg's native AV1 decoder only supplies
# hardware acceleration; libaom also handles machines without AV1 hardware.
FetchContent_Declare(aom
  GIT_REPOSITORY https://aomedia.googlesource.com/aom
  GIT_TAG v3.11.0
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR cmake-unused
)
set(ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ENABLE_TOOLS OFF CACHE BOOL "" FORCE)
set(ENABLE_DOCS OFF CACHE BOOL "" FORCE)
set(ENABLE_NASM ON CACHE BOOL "" FORCE)
set(CONFIG_AV1_ENCODER 0 CACHE STRING "" FORCE)
FetchContent_MakeAvailable(aom)
# NASM 3 split -hf into format-specific help; libaom 3.11 expects full help.
# Patch a build-tree copy so source overrides remain unchanged.
file(COPY "${aom_SOURCE_DIR}/" DESTINATION "${aom_BINARY_DIR}/source" PATTERN ".git" EXCLUDE)
set(aom_optimization "${aom_BINARY_DIR}/source/build/cmake/aom_optimization.cmake")
file(READ "${aom_optimization}" aom_optimization_text)
string(REPLACE [[${CMAKE_ASM_NASM_COMPILER} -hf]] [[${CMAKE_ASM_NASM_COMPILER} -h all]]
  aom_optimization_text "${aom_optimization_text}")
file(CONFIGURE OUTPUT "${aom_optimization}" CONTENT "${aom_optimization_text}" @ONLY)
add_subdirectory("${aom_BINARY_DIR}/source" "${aom_BINARY_DIR}/native" EXCLUDE_FROM_ALL)

# 10. FFmpeg
FetchContent_Declare(
  ffmpeg
  GIT_REPOSITORY https://github.com/FFmpeg/FFmpeg.git
  GIT_TAG        n7.1.5
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  cmake-unused
)
FetchContent_MakeAvailable(ffmpeg)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/ffmpeg" "${ffmpeg_BINARY_DIR}/native")

