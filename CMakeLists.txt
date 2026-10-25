cmake_minimum_required(VERSION 3.25)
project(mpv C CXX)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
  message(FATAL_ERROR "The native dependency build currently requires MSVC x64. Use Meson on Linux.")
endif()

include(CheckCSourceCompiles)
include(CMakePushCheckState)
cmake_push_check_state(RESET)
set(CMAKE_REQUIRED_FLAGS "/std:clatest /experimental:c11atomics /Zc:preprocessor")
check_c_source_compiles("
  #include <stdatomic.h>
  int function(int);
  typeof(function) *function_pointer;
  int main(void) { atomic_int value = 0; return atomic_load(&value); }
" MPV_HAVE_NATIVE_C23_ATOMICS)
cmake_pop_check_state()
if(NOT MPV_HAVE_NATIVE_C23_ATOMICS)
  message(FATAL_ERROR "MSVC must support C23 function types and C11 atomics. Use the msvc-2026 preset with an up-to-date C++ workload.")
endif()
enable_language(ASM_NASM)

# Set up compiler defaults for normal MSVC
if(MSVC)
  add_compile_options(
    $<$<COMPILE_LANGUAGE:C>:/std:clatest>
    $<$<COMPILE_LANGUAGE:C>:/experimental:c11atomics>
  )
  foreach(flag IN ITEMS
    /Zc:preprocessor
    /W3
    /wd4018
    /wd4146
    /wd4244
    /wd4267
    /wd4996
  )
    add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:${flag}>")
  endforeach()
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
endif()

# Include dependencies via FetchContent
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/dependencies.cmake")

# Python interpreter for generators
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# 1. Generate ebml_defs.inc and ebml_types.h
set(MATROSKA_PY "${CMAKE_CURRENT_SOURCE_DIR}/TOOLS/matroska.py")
set(EBML_DEFS_INC "${CMAKE_CURRENT_BINARY_DIR}/ebml_defs.inc")
set(EBML_TYPES_H "${CMAKE_CURRENT_BINARY_DIR}/ebml_types.h")

add_custom_command(
  OUTPUT "${EBML_DEFS_INC}"
  COMMAND "${Python3_EXECUTABLE}" "${MATROSKA_PY}" --generate-definitions "${EBML_DEFS_INC}"
  DEPENDS "${MATROSKA_PY}"
  COMMENT "Generating ebml_defs.inc"
  VERBATIM
)

add_custom_command(
  OUTPUT "${EBML_TYPES_H}"
  COMMAND "${Python3_EXECUTABLE}" "${MATROSKA_PY}" --generate-header "${EBML_TYPES_H}"
  DEPENDS "${MATROSKA_PY}"
  COMMENT "Generating ebml_types.h"
  VERBATIM
)

# 2. Generate version.h
set(VERSION_H "${CMAKE_CURRENT_BINARY_DIR}/version.h")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/MPV_VERSION" MPV_VERSION LIMIT_COUNT 1)
file(CONFIGURE OUTPUT "${VERSION_H}" CONTENT "
#ifndef MPV_VERSION_H_
#define MPV_VERSION_H_
#define VERSION \"${MPV_VERSION}-vksf\"
#define BUILDDATE __DATE__ \" \" __TIME__
#define MPVCOPYRIGHT \"Copyright (C) 2000-2026 mpv/reemio developers\"
#define MPV_CONFIGURATION \"\"
#endif
")

# 3. Generate config.h
set(CONFIG_H "${CMAKE_CURRENT_BINARY_DIR}/config.h")
if(WIN32)
  set(MPV_PLATFORM_CONFIG "
#define HAVE_WIN32_DESKTOP 1
#define HAVE_WIN32 1
#define HAVE_WIN32_THREADS 1
#define HAVE_WASAPI 1
#define HAVE_D3D11 1
#define HAVE_DXVA2 1
#define HAVE_CUDA_HWACCEL 1
#define HAVE_CUDA_INTEROP 1
#define HAVE_DOS_PATHS 1
#define HAVE_GLOB_WIN32 1
#define HAVE_WAYLAND 0
#define HAVE_PIPEWIRE 0
#define HAVE_ALSA 0
#define HAVE_VAAPI 0
#define HAVE_D3D_HWACCEL 1
#define HAVE_D3D9_HWACCEL 1
")
else()
  set(MPV_PLATFORM_CONFIG "
#define HAVE_WIN32_DESKTOP 0
#define HAVE_WASAPI 0
#define HAVE_D3D11 0
#define HAVE_DXVA2 0
#define HAVE_WAYLAND 1
#define HAVE_PIPEWIRE 1
#define HAVE_ALSA 1
#define HAVE_VAAPI 1
#define HAVE_VAAPI_WAYLAND 1
")
endif()

file(CONFIGURE OUTPUT "${CONFIG_H}" CONTENT "
#ifndef MPV_CONFIG_H_
#define MPV_CONFIG_H_

#define CONFIGURATION \"\"
#define DEFAULT_OPTICAL_DEVICE \"\"
#define FULLCONFIG \"vksf placebo vulkan ffmpeg ass lcms2 zlib\"
#define MPV_CONFDIR \"\"
#define PLATFORM \"${CMAKE_SYSTEM_NAME}\"

#define HAVE_VULKAN 1
#define HAVE_VKSF 1
#define HAVE_LIBPLACEBO 1
#define HAVE_LIBASS 1
#define HAVE_LCMS2 1
#define HAVE_ZLIB 1
#define HAVE_FFMPEG 1
#define HAVE_SHADERC 1
#define HAVE_SPIRV_CROSS 1
#define HAVE_CPLAYER 0
#define HAVE_LUA 0
#define HAVE_JAVASCRIPT 0
#define HAVE_LIBARCHIVE 0
#define HAVE_X11 0
#define HAVE_GL 0
#define HAVE_EGL 0
#define HAVE_DRM 0
#define HAVE_GBM 0
#define HAVE_PULSE 0
#define HAVE_JACK 0
#define HAVE_SNDIO 0
#define HAVE_RUBBERBAND 0
#define HAVE_UCHARDET 0
#define HAVE_ZIMG 0

${MPV_PLATFORM_CONFIG}

#endif
")

set(MPV_SOURCES
  audio/aframe.c
  audio/chmap.c
  audio/chmap_avchannel.c
  audio/chmap_sel.c
  audio/decode/ad_lavc.c
  audio/decode/ad_spdif.c
  audio/filter/af_drop.c
  audio/filter/af_format.c
  audio/filter/af_lavcac3enc.c
  audio/filter/af_lavfi_tempo.c
  audio/filter/af_scaletempo.c
  audio/filter/af_scaletempo2.c
  audio/filter/af_scaletempo2_internals.c
  audio/fmt-conversion.c
  audio/format.c
  audio/out/ao.c
  audio/out/ao_lavc.c
  audio/out/ao_null.c
  audio/out/ao_pcm.c
  audio/out/buffer.c

  common/av_common.c
  common/av_log.c
  common/codecs.c
  common/common.c
  common/encode_lavc.c
  common/msg.c
  common/playlist.c
  common/recorder.c
  common/stats.c
  common/tags.c
  common/version.c

  demux/codec_tags.c
  demux/cue.c
  demux/cache.c
  demux/demux.c
  demux/demux_cue.c
  demux/demux_disc.c
  demux/demux_edl.c
  demux/demux_lavf.c
  demux/demux_mf.c
  demux/demux_mkv.c
  demux/demux_mkv_timeline.c
  demux/demux_mpv.c
  demux/demux_null.c
  demux/demux_playlist.c
  demux/demux_raw.c
  demux/demux_timeline.c
  demux/dovi_split.c
  demux/ebml.c
  demux/packet.c
  demux/packet_pool.c
  demux/timeline.c
  ${EBML_DEFS_INC}
  ${EBML_TYPES_H}

  filters/f_async_queue.c
  filters/f_autoconvert.c
  filters/f_auto_filters.c
  filters/f_decoder_wrapper.c
  filters/f_demux_in.c
  filters/f_enhancement_pair.c
  filters/f_hwtransfer.c
  filters/f_lavfi.c
  filters/f_output_chain.c
  filters/f_swresample.c
  filters/f_swscale.c
  filters/f_utils.c
  filters/filter.c
  filters/frame.c
  filters/user_filters.c

  input/cmd.c
  input/dnd.c
  input/event.c
  input/input.c
  input/ipc.c
  input/keycodes.c

  misc/bstr.c
  misc/charset_conv.c
  misc/codepoint_width.c
  misc/dispatch.c
  misc/hash.c
  misc/io_utils.c
  misc/json.c
  misc/language.c
  misc/natural_sort.c
  misc/node.c
  misc/path_utils.c
  misc/random.c
  misc/rendezvous.c
  misc/thread_pool.c
  misc/thread_tools.c

  options/m_config_core.c
  options/m_config_frontend.c
  options/m_option.c
  options/m_property.c
  options/options.c
  options/parse_commandline.c
  options/parse_configfile.c
  options/path.c

  player/audio.c
  player/client.c
  player/command.c
  player/configfiles.c
  player/external_files.c
  player/loadfile.c
  player/main.c
  player/misc.c
  player/osd.c
  player/playloop.c
  player/screenshot.c
  player/scripting.c
  player/sub.c
  player/video.c
  player/vksf/vksf.c
  player/vksf/vksf_vo.c

  player/clipboard/clipboard.c
  player/clipboard/clipboard-vo.c

  stream/cookies.c
  stream/network.c
  stream/stream.c
  stream/stream_avdevice.c
  stream/stream_cb.c
  stream/stream_concat.c
  stream/stream_edl.c
  stream/stream_file.c
  stream/stream_lavf.c
  stream/stream_memory.c
  stream/stream_mf.c
  stream/stream_mpv.c
  stream/stream_null.c
  stream/stream_slice.c
  stream/stream_bstr.c
  stream/stream_env.c

  sub/ass_mp.c
  sub/dec_sub.c
  sub/draw_bmp.c
  sub/filter_sdh.c
  sub/img_convert.c
  sub/lavc_conv.c
  sub/osd.c
  sub/osd_libass.c
  sub/packer.c
  sub/sd_ass.c
  sub/sd_lavc.c

  video/csputils.c
  video/decode/vd_lavc.c
  video/filter/refqueue.c
  video/filter/vf_format.c
  video/filter/vf_sub.c
  video/fmt-conversion.c
  video/hwdec.c
  video/image_loader.c
  video/image_writer.c
  video/img_format.c
  video/mp_image.c
  video/mp_image_pool.c
  video/out/aspect.c
  video/out/bitmap_packer.c
  video/out/dither.c
  video/out/dr_helper.c
  video/out/filter_kernels.c
  video/out/gpu/context.c
  video/out/gpu/error_diffusion.c
  video/out/gpu/hwdec.c
  video/out/gpu/lcms.c
  video/out/gpu/libmpv_gpu.c
  video/out/gpu/osd.c
  video/out/gpu/ra.c
  video/out/gpu/shader_cache.c
  video/out/gpu/spirv.c
  video/out/gpu/spirv_shaderc.c
  video/out/gpu/user_shaders.c
  video/out/gpu/utils.c
  video/out/gpu/video.c
  video/out/gpu/video_shaders.c
  video/out/libmpv_sw.c
  video/out/vo.c
  video/out/vo_gpu.c
  video/out/vo_image.c
  video/out/vo_lavc.c
  video/out/vo_libmpv.c
  video/out/vo_null.c
  video/out/vo_tct.c
  video/out/vo_kitty.c
  video/out/win_state.c
  video/repack.c
  video/sws_utils.c

  video/out/placebo/ra_pl.c
  video/out/placebo/utils.c
  video/out/vo_gpu_next.c
  video/out/gpu_next/context.c
  video/out/vulkan/context.c
  video/out/vulkan/utils.c
  video/out/hwdec/hwdec_vulkan.c
  video/filter/vf_gpu_vulkan.c
  video/filter/vf_gpu.c

  osdep/io.c
  osdep/subprocess.c
  osdep/timer.c

  ta/ta.c
  ta/ta_talloc.c
  ta/ta_utils.c
)

if(WIN32)
  list(APPEND MPV_SOURCES
    audio/out/ao_wasapi.c
    audio/out/ao_wasapi_changenotify.c
    audio/out/ao_wasapi_utils.c
    osdep/windows_utils.c
    osdep/path-win.c
    osdep/timer-win32.c
    osdep/subprocess-win.c
    osdep/w32_keyboard.c
    osdep/glob-win.c
    osdep/language-win.c
    osdep/terminal-win.c
    osdep/w32_register.c
    osdep/als-voctrl.c
    input/ipc-win.c
    player/clipboard/clipboard-win.c
    video/out/w32_common.c
    video/out/win32/displayconfig.c
    video/out/win32/droptarget.c
    video/out/win32/menu.c
    video/out/vulkan/context_win.c
    video/out/gpu/d3d11_helpers.c
    video/out/d3d11/context.c
    video/out/d3d11/ra_d3d11.c
    video/out/d3d11/hwdec_d3d11va.c
    video/out/d3d11/hwdec_dxva2dxgi.c
    video/filter/vf_d3d11vpp.c
    video/d3d.c
    video/cuda.c
    video/out/hwdec/hwdec_cuda.c
    video/out/hwdec/hwdec_cuda_vk.c
    player/vksf/vksf_windows.c
  )
else()
  list(APPEND MPV_SOURCES
    audio/out/ao_pipewire.c
    audio/out/ao_alsa.c
    osdep/timer-linux.c
    osdep/threads-posix.c
    osdep/subprocess-posix.c
    osdep/path-unix.c
    player/vksf/vksf_wayland.c
  )
endif()

# Build libmpv as shared library
add_library(mpv SHARED ${MPV_SOURCES})
target_sources(mpv PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/windows-guids.c")
set_target_properties(mpv PROPERTIES C_STANDARD 23 C_STANDARD_REQUIRED YES)
target_compile_options(mpv PRIVATE /experimental:c11atomics /Zc:preprocessor)

foreach(asset IN ITEMS etc/input.conf etc/builtin.conf etc/menu.conf sub/osd_font.otf)
  set(output "${CMAKE_CURRENT_BINARY_DIR}/${asset}.inc")
  get_filename_component(output_dir "${output}" DIRECTORY)
  file(MAKE_DIRECTORY "${output_dir}")
  add_custom_command(
    OUTPUT "${output}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/TOOLS/file2string.py"
      "${CMAKE_CURRENT_SOURCE_DIR}/${asset}" "${output}" "${CMAKE_CURRENT_SOURCE_DIR}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${asset}" "${CMAKE_CURRENT_SOURCE_DIR}/TOOLS/file2string.py"
    VERBATIM
  )
  target_sources(mpv PRIVATE "${output}")
endforeach()
set_target_properties(mpv PROPERTIES
  OUTPUT_NAME "mpv-2"
  PREFIX "lib"
)

target_compile_definitions(mpv PRIVATE
  _GNU_SOURCE
  WIN32_LEAN_AND_MEAN
  _USE_MATH_DEFINES
  COBJMACROS
  _WIN32_WINNT=0x0A00
  WINVER=0x0A00
  UNICODE
  _UNICODE
)

target_include_directories(mpv PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_include_directories(mpv PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(mpv PRIVATE
  libplacebo::placebo
  libass::ass
  lcms2
  zlibstatic
  Vulkan::Headers
  FFmpeg::avcodec
  FFmpeg::avformat
  FFmpeg::avutil
  FFmpeg::swscale
  FFmpeg::swresample
  FFmpeg::avfilter
  spirv-cross-c
)

if(WIN32)
  target_link_libraries(mpv PRIVATE
    winmm
    shlwapi
    ole32
    oleaut32
    uuid
    avrt
    dwmapi
    d3d11
    dxgi
    dxva2
    gdi32
    imm32
    ntdll
    pathcch
    shcore
    user32
    uxtheme
    version
    ws2_32
  )
endif()

add_library(mpv::libmpv ALIAS mpv)

option(MPV_BUILD_TESTS "Enable the native DLL runtime smoke test" ${PROJECT_IS_TOP_LEVEL})
option(MPV_BUILD_RENDER_TESTS "Enable VKSF rendering tests (requires Tk and a Vulkan GPU)" OFF)
if(MPV_BUILD_TESTS OR MPV_BUILD_RENDER_TESTS)
  enable_testing()
endif()
if(MPV_BUILD_TESTS)
  add_test(NAME mpv_msvc_smoke
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test/msvc_smoke.py" "$<TARGET_FILE:mpv>"
  )
endif()
if(MPV_BUILD_RENDER_TESTS)
  add_test(NAME mpv_msvc_render_smoke
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test/msvc_render_smoke.py" "$<TARGET_FILE:mpv>"
  )
  set_tests_properties(mpv_msvc_render_smoke PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
endif()
