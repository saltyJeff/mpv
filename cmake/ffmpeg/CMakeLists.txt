# FFmpeg uses constant feature branches whose unused references must be
# eliminated, including in Debug. Keep this compiler policy local to FFmpeg.
string(REGEX REPLACE "[/-]RTC[^ \t]*" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
# FFmpeg's Schannel implementation passes UTF-8/ANSI hostnames to SSPI.
# Flutter's parent directory enables Unicode Win32 aliases for its own UI.
remove_definitions(-DUNICODE -D_UNICODE)
include("${CMAKE_CURRENT_LIST_DIR}/../ffmpeg.cmake")
