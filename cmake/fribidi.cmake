# mpv/cmake/fribidi.cmake
# Custom CMake target for GNU FriBidi with automatic table generation.

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(FRIBIDI_VERSION "1.0.16")
set(FRIBIDI_MAJOR_VERSION 1)
set(FRIBIDI_MINOR_VERSION 0)
set(FRIBIDI_MICRO_VERSION 16)
set(FRIBIDI_INTERFACE_VERSION 4)
set(SIZEOF_INT 4)

set(PACKAGE "fribidi")
set(PACKAGE_NAME "GNU FriBidi")
set(PACKAGE_BUGREPORT "https://github.com/fribidi/fribidi/issues")
if(MSVC)
  set(FRIBIDI_MSVC_BUILD_PLACEHOLDER "#define FRIBIDI_BUILD 1")
else()
  set(FRIBIDI_MSVC_BUILD_PLACEHOLDER "/* #undef FRIBIDI_BUILD */")
endif()

configure_file(
  "${fribidi_SOURCE_DIR}/lib/fribidi-config.h.in"
  "${fribidi_BINARY_DIR}/fribidi-config.h"
  @ONLY
)

file(WRITE "${fribidi_BINARY_DIR}/config.h" "
#ifndef FRIBIDI_CONFIG_H_INTERNAL
#define FRIBIDI_CONFIG_H_INTERNAL
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_MEMORY_H 1
#define STDC_HEADERS 1
#define HAVE_STRINGIZE 1
#endif
")

set(GEN_TAB_DIR "${fribidi_SOURCE_DIR}/gen.tab")
set(UNIDATA_DIR "${GEN_TAB_DIR}/unidata")

# Helper macro to create generator executables
macro(fribidi_add_generator NAME)
  add_executable(${NAME} ${ARGN})
  target_compile_definitions(${NAME} PRIVATE
    HAVE_STRINGIZE=1
    DONT_HAVE_FRIBIDI_CONFIG_H=1
    HAVE_STDLIB_H=1
    HAVE_STRING_H=1
    STDC_HEADERS=1
    _CRT_SECURE_NO_WARNINGS
  )
  target_include_directories(${NAME} PRIVATE
    "${fribidi_SOURCE_DIR}/lib"
    "${fribidi_BINARY_DIR}"
  )
endmacro()

# 1. gen-unicode-version
fribidi_add_generator(fribidi_gen_unicode_version "${GEN_TAB_DIR}/gen-unicode-version.c")
set(UNICODE_VERSION_H "${fribidi_BINARY_DIR}/fribidi-unicode-version.h")

add_custom_command(
  OUTPUT "${UNICODE_VERSION_H}"
  COMMAND "${Python3_EXECUTABLE}" -c
          "import subprocess, sys; open(sys.argv[1], 'wb').write(subprocess.check_output(sys.argv[2:]))"
          "${UNICODE_VERSION_H}"
          "$<TARGET_FILE:fribidi_gen_unicode_version>"
          "${UNIDATA_DIR}/ReadMe.txt"
          "${UNIDATA_DIR}/BidiMirroring.txt"
          "gen-unicode-version"
  DEPENDS fribidi_gen_unicode_version "${UNIDATA_DIR}/ReadMe.txt" "${UNIDATA_DIR}/BidiMirroring.txt"
  COMMENT "Generating fribidi-unicode-version.h"
  VERBATIM
)
add_custom_target(fribidi_unicode_version_target DEPENDS "${UNICODE_VERSION_H}")

# Macro for table generators
macro(fribidi_gen_tab PROG_NAME SOURCE_FILE OUT_NAME)
  set(GEN_EXE "fribidi_${PROG_NAME}")
  fribidi_add_generator(${GEN_EXE} "${GEN_TAB_DIR}/${SOURCE_FILE}" "${GEN_TAB_DIR}/packtab.c")
  add_dependencies(${GEN_EXE} fribidi_unicode_version_target)
  set(TAB_OUT "${fribidi_BINARY_DIR}/${OUT_NAME}")
  add_custom_command(
    OUTPUT "${TAB_OUT}"
    COMMAND "${Python3_EXECUTABLE}" -c
            "import subprocess, sys; open(sys.argv[1], 'wb').write(subprocess.check_output(sys.argv[2:]))"
            "${TAB_OUT}"
            "$<TARGET_FILE:${GEN_EXE}>"
            2
            ${ARGN}
            "${PROG_NAME}"
    DEPENDS ${GEN_EXE} ${ARGN} "${UNICODE_VERSION_H}"
    COMMENT "Generating ${OUT_NAME}"
    VERBATIM
  )
  list(APPEND FRIBIDI_GENERATED_HEADERS "${TAB_OUT}")
endmacro()

set(FRIBIDI_GENERATED_HEADERS "${UNICODE_VERSION_H}")
fribidi_gen_tab(gen-bidi-type-tab "gen-bidi-type-tab.c" "bidi-type.tab.i" "${UNIDATA_DIR}/UnicodeData.txt")
fribidi_gen_tab(gen-joining-type-tab "gen-joining-type-tab.c" "joining-type.tab.i" "${UNIDATA_DIR}/UnicodeData.txt" "${UNIDATA_DIR}/ArabicShaping.txt")
fribidi_gen_tab(gen-arabic-shaping-tab "gen-arabic-shaping-tab.c" "arabic-shaping.tab.i" "${UNIDATA_DIR}/UnicodeData.txt")
fribidi_gen_tab(gen-mirroring-tab "gen-mirroring-tab.c" "mirroring.tab.i" "${UNIDATA_DIR}/BidiMirroring.txt")
fribidi_gen_tab(gen-brackets-tab "gen-brackets-tab.c" "brackets.tab.i" "${UNIDATA_DIR}/BidiBrackets.txt" "${UNIDATA_DIR}/UnicodeData.txt")
fribidi_gen_tab(gen-brackets-type-tab "gen-brackets-type-tab.c" "brackets-type.tab.i" "${UNIDATA_DIR}/BidiBrackets.txt")

set(FRIBIDI_SOURCES
  "${fribidi_SOURCE_DIR}/lib/fribidi-arabic.c"
  "${fribidi_SOURCE_DIR}/lib/fribidi-bidi-types.c"
  "${fribidi_SOURCE_DIR}/lib/fribidi-bidi.c"
  "${fribidi_SOURCE_DIR}/lib/fribidi-brackets.c"
  "${fribidi_SOURCE_DIR}/lib/fribidi-char-sets-cap-rtl.c"
  "${fribidi_SOURCE_DIR}/lib/fribidi-char-sets-cp1255.c"
  "${fribidi_SOURCE_DIR}/lib/fribidi-char-sets-cp1256.c"
  "${fribidi_SOURCE_DIR}/lib/fribidi-char-sets-iso8859-6.c"
  "${fribidi_SOURCE_DIR}/lib/fribidi-char-sets-iso8859-8.c"
  "${fribidi_SOURCE_DIR}/lib/fribidi-char-sets-utf8.c"
  "${fribidi_SOURCE_DIR}/lib/fribidi-char-sets.c"
  "${fribidi_SOURCE_DIR}/lib/fribidi-deprecated.c"
  "${fribidi_SOURCE_DIR}/lib/fribidi-joining-types.c"
  "${fribidi_SOURCE_DIR}/lib/fribidi-joining.c"
  "${fribidi_SOURCE_DIR}/lib/fribidi-mirroring.c"
  "${fribidi_SOURCE_DIR}/lib/fribidi-run.c"
  "${fribidi_SOURCE_DIR}/lib/fribidi-shape.c"
  "${fribidi_SOURCE_DIR}/lib/fribidi.c"
  ${FRIBIDI_GENERATED_HEADERS}
)

add_library(fribidi STATIC ${FRIBIDI_SOURCES})
target_compile_definitions(fribidi PUBLIC FRIBIDI_LIB_STATIC)
target_compile_definitions(fribidi PRIVATE HAVE_CONFIG_H=1 _CRT_SECURE_NO_WARNINGS)
target_include_directories(fribidi PUBLIC
  "${fribidi_SOURCE_DIR}/lib"
  "${fribidi_BINARY_DIR}"
)

add_library(Fribidi::fribidi ALIAS fribidi)

