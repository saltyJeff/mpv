# Native Windows dependency build

The Windows build uses MSVC, CMake and NASM throughout the media stack. It
does not require Meson, autotools, a POSIX shell, pkg-config, or MSYS2 libraries.
Python and Perl are host tools for generating build inputs.

Prerequisites:

- Visual Studio 2026 with the Desktop development with C++ workload and Windows SDK.
- CMake 4.2 or newer for the Visual Studio 2026 generator.
- Git, Python 3.10 or newer, Perl, and NASM on PATH. A native Windows Perl
  installation can be used; Perl is not a runtime dependency of libmpv.
- Network access for the first FetchContent configuration.

From the `mpv` source directory:

```powershell
cmake --preset msvc-2026
cmake --build --preset msvc-2026-release --parallel
ctest --test-dir ../build/mpv-msvc-2026 -C Release --output-on-failure
```

For Debug, use `msvc-2026-debug` and `ctest -C Debug`. The DLL is written to
`../build/mpv-msvc-2026/<configuration>/libmpv-2.dll`. Reemio's Windows runner
links `mpv::libmpv`, builds the same target in its own build tree, and stages
the DLL beside the executable.

FFmpeg keeps minimal optimization (`/O1`) and debug symbols in Debug builds:
its disabled-component branches depend on dead-code elimination. Runtime
checks remain enabled for mpv and the app; FFmpeg's compiler policy is scoped
to its own CMake directory.

The C sources use MSVC's C23 mode (`/std:clatest`) and experimental C11
atomics. Compiler support and C library support differ: the Windows SDK does
not currently provide every C23 library header. Compatibility fallbacks are
guarded separately from the language version.

`dependencies.cmake` pins the dependency sources, including NVIDIA's header-only
codec API and Shaderc's
glslang/SPIRV-Tools/SPIRV-Headers closure. Dependencies link statically into
libmpv; Windows system libraries and the MSVC runtime remain dynamic. The
Vulkan loader comes from the installed graphics driver in System32, with no
Vulkan SDK or bundled replacement loader required.

FFmpeg's native configuration is a curated playback profile, defined in
`ffmpeg_generator.py`. It includes the common video/audio/subtitle decoders,
NVDEC/D3D11VA/DXVA2, software AV1 through libaom, screenshot encoders,
HTTP/HTTPS with Schannel, and the filters used by mpv. NASM supplies x86 SIMD
routines with runtime CPU dispatch; GNU inline assembly stays disabled. This
profile does not promise every optional codec, muxer, script engine or input
device supported by a full upstream distribution. FFmpeg Vulkan decoding is
currently disabled; Vulkan presentation is provided by libplacebo.

Generated FFmpeg headers, component registries and source lists live in the
binary directory. The generator takes explicit source/output paths and
preserves unchanged outputs. To change dependency versions or features,
update the pins/profile and reconfigure; do not edit generated files.

Libplacebo uses the same revision as the fork's Meson wrap. Its MSVC patches
apply to a build-tree copy and verify both the input and output hashes. A
revision mismatch fails configuration instead of silently skipping a patch.
The five patched libplacebo files were checked individually against MSVC 14.51:
they fix Win32 callback syntax, static Vulkan initializers, and C compound
literals used in C++ code. Older function-type workarounds are omitted because
the VS 2026 compiler accepts the upstream declarations.
Libaom also uses a build-tree copy for its NASM 3 help-command compatibility
fix. Source overrides are supported through CMake's standard
`FETCHCONTENT_SOURCE_DIR_<NAME>` variables.

The smoke test uses generated local fixtures. It checks the public libmpv and
VKSF exports, initialization, Unicode paths, PCM/PNG and H.264/HEVC/VP9/AV1
decoding, atempo, and HTTP transport. GPU presentation, hardware decoding and HDR/ICC behavior
also need playback validation with a compatible display and driver.

Linux retains Reemio's existing Meson integration in `cmake/libmpv.cmake`.
The native CMake source/configuration profile currently targets Windows x64.
