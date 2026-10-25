# mpv/cmake/ffmpeg.cmake
# Native CMake MSVC build for FFmpeg libraries.

set(FFMPEG_SRC "${ffmpeg_SOURCE_DIR}")
set(FFMPEG_GENERATED_DIR "${ffmpeg_BINARY_DIR}/generated")
set(FFMPEG_CONFIG_DIR "${FFMPEG_GENERATED_DIR}/config")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_LIST_DIR}/ffmpeg_generator.py"
  "${FFMPEG_SRC}/configure"
)
execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/ffmpeg_generator.py"
    --source "${FFMPEG_SRC}" --output "${FFMPEG_GENERATED_DIR}"
  COMMAND_ERROR_IS_FATAL ANY
)

include("${FFMPEG_GENERATED_DIR}/sources.cmake")

set(FFMPEG_COMMON_INCLUDES
  "${FFMPEG_CONFIG_DIR}"
  "${FFMPEG_SRC}"
  "${ffnvcodec_SOURCE_DIR}/include"
  "${zlib_SOURCE_DIR}"
  "${zlib_BINARY_DIR}"
  "${FFMPEG_SRC}/libavutil/x86"
)

set(FFMPEG_COMMON_DEFINES
  HAVE_AV_CONFIG_H
  _CRT_SECURE_NO_WARNINGS
  _CRT_NONSTDC_NO_WARNINGS
  _USE_MATH_DEFINES
  WIN32_LEAN_AND_MEAN
)

set(FFMPEG_MSVC_FLAGS
  /experimental:c11atomics
  /Zc:preprocessor
  /wd4018
  /wd4101
  /wd4244
  /wd4267
  /wd4305
  /wd4996
  /utf-8
)

# 1. libavutil
add_library(avutil STATIC ${AVUTIL_SOURCES} ${AVUTIL_ASM_SOURCES})
target_include_directories(avutil PUBLIC ${FFMPEG_COMMON_INCLUDES})
target_compile_definitions(avutil PRIVATE ${FFMPEG_COMMON_DEFINES})
target_compile_options(avutil PRIVATE "$<$<COMPILE_LANGUAGE:C>:${FFMPEG_MSVC_FLAGS}>")
target_link_libraries(avutil PUBLIC bcrypt user32 d3d11 dxgi dxva2 ffnvcodec)
add_library(FFmpeg::avutil ALIAS avutil)

# 2. libswscale
add_library(swscale STATIC ${SWSCALE_SOURCES} ${SWSCALE_ASM_SOURCES})
target_include_directories(swscale PUBLIC ${FFMPEG_COMMON_INCLUDES})
target_compile_definitions(swscale PRIVATE ${FFMPEG_COMMON_DEFINES})
target_compile_options(swscale PRIVATE "$<$<COMPILE_LANGUAGE:C>:${FFMPEG_MSVC_FLAGS}>")
target_link_libraries(swscale PUBLIC avutil)
add_library(FFmpeg::swscale ALIAS swscale)

# 3. libswresample
add_library(swresample STATIC ${SWRESAMPLE_SOURCES} ${SWRESAMPLE_ASM_SOURCES})
target_include_directories(swresample PUBLIC ${FFMPEG_COMMON_INCLUDES})
target_compile_definitions(swresample PRIVATE ${FFMPEG_COMMON_DEFINES})
target_compile_options(swresample PRIVATE "$<$<COMPILE_LANGUAGE:C>:${FFMPEG_MSVC_FLAGS}>")
target_link_libraries(swresample PUBLIC avutil)
add_library(FFmpeg::swresample ALIAS swresample)

# 4. libavcodec
add_library(avcodec STATIC ${AVCODEC_SOURCES} ${AVCODEC_ASM_SOURCES})
target_include_directories(avcodec PUBLIC ${FFMPEG_COMMON_INCLUDES})
target_include_directories(avcodec PRIVATE
  "${FFMPEG_SRC}/libavcodec"
  "${aom_BINARY_DIR}/source"
)
target_compile_definitions(avcodec PRIVATE ${FFMPEG_COMMON_DEFINES})
target_compile_options(avcodec PRIVATE "$<$<COMPILE_LANGUAGE:C>:${FFMPEG_MSVC_FLAGS}>")
target_link_libraries(avcodec PUBLIC avutil zlibstatic d3d11 dxgi dxva2 aom)
add_library(FFmpeg::avcodec ALIAS avcodec)

# 5. libavformat
add_library(avformat STATIC ${AVFORMAT_SOURCES} ${AVFORMAT_ASM_SOURCES})
target_include_directories(avformat PUBLIC ${FFMPEG_COMMON_INCLUDES})
target_compile_definitions(avformat PRIVATE ${FFMPEG_COMMON_DEFINES})
target_compile_options(avformat PRIVATE "$<$<COMPILE_LANGUAGE:C>:${FFMPEG_MSVC_FLAGS}>")
target_link_libraries(avformat PUBLIC avcodec avutil ws2_32 secur32 zlibstatic)
add_library(FFmpeg::avformat ALIAS avformat)

# 6. libavfilter
add_library(avfilter STATIC ${AVFILTER_SOURCES} ${AVFILTER_ASM_SOURCES})
target_include_directories(avfilter PUBLIC ${FFMPEG_COMMON_INCLUDES})
target_compile_definitions(avfilter PRIVATE ${FFMPEG_COMMON_DEFINES})
target_compile_options(avfilter PRIVATE "$<$<COMPILE_LANGUAGE:C>:${FFMPEG_MSVC_FLAGS}>")
target_link_libraries(avfilter PUBLIC avutil avcodec swscale swresample)
add_library(FFmpeg::avfilter ALIAS avfilter)

file(SHA256 "${FFMPEG_CONFIG_DIR}/config.asm" ffmpeg_asm_config_hash)
foreach(ffmpeg_target IN ITEMS avutil swscale swresample avcodec avformat avfilter)
  target_include_directories(${ffmpeg_target} PRIVATE
    "${FFMPEG_SRC}/lib${ffmpeg_target}/x86"
  )
  target_compile_options(${ffmpeg_target} PRIVATE
    "$<$<AND:$<COMPILE_LANGUAGE:C>,$<CONFIG:Debug>>:/O1>"
    "$<$<COMPILE_LANGUAGE:ASM_NASM>:-p${FFMPEG_CONFIG_DIR}/config.asm>"
    "$<$<COMPILE_LANGUAGE:ASM_NASM>:-DREEMIO_CONFIG_${ffmpeg_asm_config_hash}>"
  )
endforeach()
