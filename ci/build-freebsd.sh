#!/bin/sh
set -e

. ./ci/build-common.sh

export CFLAGS="$CFLAGS -isystem/usr/local/include -march=native"
export CXXFLAGS="$CXXFLAGS -isystem/usr/local/include"
export LDFLAGS="$LDFLAGS -L/usr/local/lib"

# TODO: re-add -Ddvbin=enabled

mkdir -p subprojects
git clone https://code.videolan.org/videolan/libplacebo.git \
    --recurse-submodules --shallow-submodules subprojects/libplacebo
git -C subprojects/libplacebo checkout 48bd967903ac5f54380a89b07e24abccead7b753

meson setup build $common_args \
  -Db_sanitize=address,undefined \
  -Diconv=disabled \
  -Dlua=enabled \
  -Degl-drm=enabled \
  -Dopenal=enabled \
  -Dsndio=enabled \
  -Dvdpau=enabled \
  -Dvulkan=enabled \
  -Doss-audio=enabled \
  $(pkg info -q libdvdnav && echo -Ddvdnav=enabled) \
  $(pkg info -q libcdio-paranoia && echo -Dcdda=enabled) \
  $(pkg info -q pipewire && echo -Dpipewire=enabled)

meson compile -C build
./build/mpv -v --no-config
