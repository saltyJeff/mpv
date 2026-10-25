# mpv/cmake/libass.cmake
# Custom CMake target for libass from fetched source.

set(LIBASS_DIR "${libass_SOURCE_DIR}/libass")

set(LIBASS_SOURCES
  "${LIBASS_DIR}/c/c_be_blur.c"
  "${LIBASS_DIR}/c/c_blend_bitmaps.c"
  "${LIBASS_DIR}/c/c_blur.c"
  "${LIBASS_DIR}/c/c_rasterizer.c"
  "${LIBASS_DIR}/ass.c"
  "${LIBASS_DIR}/ass_bitmap.c"
  "${LIBASS_DIR}/ass_bitmap_engine.c"
  "${LIBASS_DIR}/ass_blur.c"
  "${LIBASS_DIR}/ass_cache.c"
  "${LIBASS_DIR}/ass_drawing.c"
  "${LIBASS_DIR}/ass_filesystem.c"
  "${LIBASS_DIR}/ass_font.c"
  "${LIBASS_DIR}/ass_fontselect.c"
  "${LIBASS_DIR}/ass_library.c"
  "${LIBASS_DIR}/ass_outline.c"
  "${LIBASS_DIR}/ass_parse.c"
  "${LIBASS_DIR}/ass_rasterizer.c"
  "${LIBASS_DIR}/ass_render.c"
  "${LIBASS_DIR}/ass_render_api.c"
  "${LIBASS_DIR}/ass_shaper.c"
  "${LIBASS_DIR}/ass_string.c"
  "${LIBASS_DIR}/ass_strtod.c"
  "${LIBASS_DIR}/ass_utils.c"
)

add_library(ass STATIC ${LIBASS_SOURCES})

target_compile_definitions(ass PRIVATE
  _GNU_SOURCE
  CONFIG_RASTERIZER=1
  CONFIG_SOURCEVERSION="0.17.3"
  HAVE_STRDUP=1
  HAVE_FSTAT=1
)

if(WIN32)
  target_sources(ass PRIVATE "${LIBASS_DIR}/ass_directwrite.c")
  target_compile_definitions(ass PRIVATE CONFIG_DIRECTWRITE=1)
  target_link_libraries(ass PRIVATE dwrite.lib)
endif()

target_include_directories(ass PUBLIC
  "${LIBASS_DIR}"
  "${libass_BINARY_DIR}/include"
)

# Installed-style headers are used by mpv; upstream stores them in libass/.
file(MAKE_DIRECTORY "${libass_BINARY_DIR}/include/ass")
foreach(header IN ITEMS ass.h ass_types.h)
  configure_file("${LIBASS_DIR}/${header}" "${libass_BINARY_DIR}/include/ass/${header}" COPYONLY)
endforeach()

target_link_libraries(ass PUBLIC
  freetype
  fribidi
  harfbuzz
)

add_library(libass::ass ALIAS ass)

