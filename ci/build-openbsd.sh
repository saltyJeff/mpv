#!/bin/sh
set -e

. ./ci/build-common.sh

mkdir -p subprojects
git clone https://code.videolan.org/videolan/libplacebo.git \
    --recurse-submodules --shallow-submodules subprojects/libplacebo
git -C subprojects/libplacebo checkout 48bd967903ac5f54380a89b07e24abccead7b753

meson setup build $common_args \
  -Dffmpeg:vulkan=auto \
  -Dffmpeg:werror=false \
  -Dlua=enabled \
  -Dopenal=enabled \
  -Dpulse=enabled \
  -Dvulkan=enabled \
  -Ddvdnav=enabled \
  -Dcdda=disabled
meson compile -C build
./build/mpv -v --no-config
