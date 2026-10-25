# Little CMS 2.16 has Meson/autotools projects, but no CMake project.
set(LCMS2_SOURCES
  cmsalpha cmscam02 cmscgats cmscnvrt cmserr cmsgamma cmsgmt cmshalf
  cmsintrp cmsio0 cmsio1 cmslut cmsmd5 cmsmtrx cmsnamed cmsopt cmspack
  cmspcs cmsplugin cmsps2 cmssamp cmssm cmstypes cmsvirt cmswtpnt cmsxform
)
list(TRANSFORM LCMS2_SOURCES PREPEND "${lcms2_SOURCE_DIR}/src/")
list(TRANSFORM LCMS2_SOURCES APPEND ".c")
add_library(lcms2 STATIC ${LCMS2_SOURCES})
target_include_directories(lcms2 PUBLIC "${lcms2_SOURCE_DIR}/include")
add_library(LCMS2::LCMS2 ALIAS lcms2)
