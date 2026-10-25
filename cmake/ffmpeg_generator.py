#!/usr/bin/env python3
"""
Generates pure CMake build files and configuration headers for FFmpeg
tailored for Reemio (MSVC x64, Windows, native CMake, zero MSYS2).
"""

import argparse
import re
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
FFMPEG_SRC = args.source.resolve()
OUT_DIR = args.output.resolve()
CONFIG_DIR = OUT_DIR / "config"
FFMPEG_VERSION = (FFMPEG_SRC / "RELEASE").read_text(encoding="utf-8").strip()

def write_if_changed(path, content, encoding="utf-8"):
    if not path.exists() or path.read_text(encoding=encoding) != content:
        path.write_text(content, encoding=encoding)

# Ensure directories exist
CONFIG_DIR.mkdir(parents=True, exist_ok=True)
(CONFIG_DIR / "libavutil").mkdir(parents=True, exist_ok=True)
(CONFIG_DIR / "libavcodec").mkdir(parents=True, exist_ok=True)
(CONFIG_DIR / "libavformat").mkdir(parents=True, exist_ok=True)
(CONFIG_DIR / "libavfilter").mkdir(parents=True, exist_ok=True)

# 1. Component definitions
ENABLED_DECODERS = [
    # Video
    'h264', 'hevc', 'av1', 'vp9', 'vp8', 'mpeg1video', 'mpeg2video', 'mpeg4',
    'vc1', 'wmv3', 'mjpeg', 'theora', 'png', 'bmp', 'libaom_av1',
    # Audio
    'aac', 'aac_latm', 'ac3', 'eac3', 'dca', 'mp3', 'mp3float', 'flac',
    'opus', 'vorbis', 'truehd', 'alac',
    'pcm_s16le', 'pcm_s24le', 'pcm_s32le', 'pcm_f32le',
    'pcm_s16be', 'pcm_s24be', 'pcm_u8',
    # Subtitle
    'subrip', 'ass', 'ssa', 'movtext', 'webvtt', 'pgssub', 'dvdsub',
]

ENABLED_ENCODERS = ['png', 'mjpeg', 'bmp']

ENABLED_HWACCELS = [
    # NVDEC
    'h264_nvdec', 'hevc_nvdec', 'av1_nvdec', 'vp9_nvdec', 'vc1_nvdec',
    'mpeg2_nvdec', 'mpeg4_nvdec', 'mjpeg_nvdec', 'vp8_nvdec',
    # D3D11VA
    'h264_d3d11va', 'hevc_d3d11va', 'av1_d3d11va', 'vp9_d3d11va', 'vc1_d3d11va', 'mpeg2_d3d11va',
    'h264_d3d11va2', 'hevc_d3d11va2', 'av1_d3d11va2', 'vp9_d3d11va2', 'vc1_d3d11va2', 'mpeg2_d3d11va2',
    # DXVA2
    'h264_dxva2', 'hevc_dxva2', 'av1_dxva2', 'vp9_dxva2', 'vc1_dxva2', 'mpeg2_dxva2',
]

ENABLED_DEMUXERS = [
    'matroska', 'mov', 'mpegts', 'mpegps', 'mpegvideo', 'flv', 'avi',
    'hls', 'ogg', 'wav', 'flac', 'mp3', 'aac', 'ac3', 'eac3', 'dts',
    'srt', 'ass', 'webvtt', 'image2', 'concat',
]

ENABLED_PROTOCOLS = [
    'file', 'http', 'httpproxy', 'https', 'tls', 'tcp', 'udp',
    'crypto', 'concat', 'subfile', 'data', 'hls',
]

ENABLED_PARSERS = [
    'h264', 'hevc', 'av1', 'vp9', 'vp8', 'mpegvideo', 'mpeg4video',
    'vc1', 'mjpeg', 'aac', 'aac_latm', 'ac3', 'dca', 'mpegaudio',
    'flac', 'opus', 'vorbis',
]

ENABLED_BSFS = [
    'null', 'h264_mp4toannexb', 'hevc_mp4toannexb', 'aac_adtstoasc',
    'extract_extradata', 'vp9_superframe_split',
]

ENABLED_FILTERS = [
    'scale', 'format', 'null', 'anull', 'aresample', 'aformat',
    'buffer', 'buffersink', 'abuffer', 'abuffersink', 'atempo',
]

# 2. Parse configure to recursively resolve selects
configure_text = (FFMPEG_SRC / "configure").read_text(encoding="utf-8", errors="ignore")
selects = dict(re.findall(r'(\w+)_select="([^"]*)"', configure_text))
deps = dict(re.findall(r'(\w+)_deps="([^"]*)"', configure_text))

initial_components = set()
for d in ENABLED_DECODERS:
    initial_components.add(f"{d}_decoder")
for encoder in ENABLED_ENCODERS:
    initial_components.add(f"{encoder}_encoder")
for h in ENABLED_HWACCELS:
    initial_components.add(f"{h}_hwaccel")
for d in ENABLED_DEMUXERS:
    initial_components.add(f"{d}_demuxer")
for p in ENABLED_PROTOCOLS:
    initial_components.add(f"{p}_protocol")
for p in ENABLED_PARSERS:
    initial_components.add(f"{p}_parser")
for b in ENABLED_BSFS:
    initial_components.add(f"{b}_bsf")
for f in ENABLED_FILTERS:
    initial_components.add(f"{f}_filter")

# Base HW and system components
initial_components.update([
    'cuda', 'ffnvcodec', 'nvdec', 'd3d11va', 'dxva2', 'schannel',
    'zlib', 'network', 'protocol', 'demuxer', 'decoder', 'parser', 'bsf',
    'tls_protocol', 'tls_schannel', 'libaom', 'avcodec', 'avformat', 'avfilter', 'avutil',
    'swscale', 'swresample', 'safe_bitstream_reader',
])

resolved_components = set(initial_components)
changed = True
while changed:
    changed = False
    for c in list(resolved_components):
        if c in selects:
            for sub in selects[c].split():
                if sub not in resolved_components:
                    resolved_components.add(sub)
                    changed = True
        if c in deps:
            for sub in deps[c].split():
                if sub in selects and sub not in resolved_components:
                    resolved_components.add(sub)
                    changed = True

print(f"Resolved {len(resolved_components)} internal components.")

# 3. Generate config_components.h
all_components = set()

# From configure
for m in re.finditer(r'([A-Za-z0-9_]+)_(?:select|deps|suggest)=', configure_text):
    all_components.add(m.group(1).lower())

# From Makefiles
for sub in ["libavcodec", "libavformat", "libavfilter", "libavutil", "libswscale", "libswresample"]:
    mf = (FFMPEG_SRC / sub / "Makefile").read_text(encoding="utf-8", errors="ignore")
    for m in re.finditer(r'CONFIG_([A-Za-z0-9_]+)', mf):
        all_components.add(m.group(1).lower())

# From C and H files
for sub in ["libavcodec", "libavformat", "libavfilter", "libavutil"]:
    for cf in (FFMPEG_SRC / sub).glob("*.[ch]"):
        c_text = cf.read_text(encoding="utf-8", errors="ignore")
        for m in re.finditer(r'\bCONFIG_([A-Za-z0-9_]+)\b', c_text):
            all_components.add(m.group(1).lower())

# Image pipe demuxers in img2dec.c
img2dec_text = (FFMPEG_SRC / "libavformat" / "img2dec.c").read_text(encoding="utf-8", errors="ignore")
for m in re.finditer(r'IMAGEAUTO_DEMUXER(?:_EXT)?\([^,]+,\s*(?:[A-Za-z0-9_]+,\s*)?([A-Za-z0-9_]+)\)', img2dec_text):
    all_components.add(f"image_{m.group(1).lower()}_pipe_demuxer")

all_components.update(resolved_components)
print(f"Discovered {len(all_components)} total components for config_components.h")

config_components_lines = [
    "/* Automatically generated by ffmpeg_generator.py for Reemio MSVC */",
    "#ifndef FFMPEG_CONFIG_COMPONENTS_H",
    "#define FFMPEG_CONFIG_COMPONENTS_H",
    "",
]
for comp in sorted(all_components):
    val = 1 if comp in resolved_components else 0
    config_components_lines.append(f"#define CONFIG_{comp.upper()} {val}")

config_components_lines.extend([
    "",
    "#endif /* FFMPEG_CONFIG_COMPONENTS_H */",
])
write_if_changed((CONFIG_DIR / "config_components.h"), "\n".join(config_components_lines), encoding="utf-8")

# 4. Generate config.h
config_h_content = """/* Automatically generated by ffmpeg_generator.py for Reemio MSVC */
#ifndef FFMPEG_CONFIG_H
#define FFMPEG_CONFIG_H

#define FFMPEG_CONFIGURATION "--toolchain=msvc"
#define FFMPEG_LICENSE "LGPL version 2.1 or later"
#define CONFIG_THIS_YEAR 2024
#define FFMPEG_DATADIR ""
#define AVCONV_DATADIR ""
#define CC_IDENT "MSVC"
#define OS_NAME "Windows"
#define EXTERN_PREFIX ""
#define EXTERN_ASM
#define BUILDSUF ""
#define SLIBSUF ".dll"
#define SWS_MAX_FILTER_SIZE 256

#define ARCH_AARCH64 0
#define ARCH_ARM 0
#define ARCH_IA64 0
#define ARCH_MIPS 0
#define ARCH_PPC 0
#define ARCH_RISCV 0
#define ARCH_SPARC 0
#define ARCH_X86 1
#define ARCH_X86_32 0
#define ARCH_X86_64 1

#define HAVE_INLINE_ASM 0
#define HAVE_X86ASM 1
#define HAVE_EBX_AVAILABLE 1
#define HAVE_EBP_AVAILABLE 1
#define HAVE_FAST_CLZ 1
#define HAVE_FAST_UNALIGNED 1
#define HAVE_SIMD_ALIGN_16 1
#define HAVE_SIMD_ALIGN_32 1
#define HAVE_SIMD_ALIGN_64 1
#define HAVE_ALIGNED_STACK 1

#define HAVE_ACCESS 1
#define HAVE_ALIGNED_MALLOC 1
#define HAVE_ARC4RANDOM 0
#define HAVE_BCRYPT 1
#define HAVE_CBRT 1
#define HAVE_CBRTF 1
#define HAVE_COPYSIGN 1
#define HAVE_COS 1
#define HAVE_COSF 1
#define HAVE_ERF 1
#define HAVE_EXP2 1
#define HAVE_EXP2F 1
#define HAVE_EXPF 1
#define HAVE_HYPOT 1
#define HAVE_ISFINITE 1
#define HAVE_ISINF 1
#define HAVE_ISNAN 1
#define HAVE_LDEXPF 1
#define HAVE_LLRINT 1
#define HAVE_LLRINTF 1
#define HAVE_LOG10F 1
#define HAVE_LOG2 1
#define HAVE_LOG2F 1
#define HAVE_LOGF 1
#define HAVE_LRINT 1
#define HAVE_LRINTF 1
#define HAVE_POWF 1
#define HAVE_RINT 1
#define HAVE_ROUND 1
#define HAVE_ROUNDF 1
#define HAVE_SIN 1
#define HAVE_SINF 1
#define HAVE_TRUNC 1
#define HAVE_TRUNCF 1

#define HAVE_IO_H 1
#define HAVE_DIRECT_H 1
#define HAVE_DOS_PATHS 1
#define HAVE_WINDOWS_H 1
#define HAVE_WINSOCK2_H 1
#define HAVE_WS2TCPIP_H 1
#define HAVE_CLOSESOCKET 1
#define HAVE_SOCKLEN_T 1
#define HAVE_STRUCT_ADDRINFO 1
#define HAVE_STRUCT_IPV6_MREQ 1
#define HAVE_STRUCT_SOCKADDR_IN6 1
#define HAVE_STRUCT_SOCKADDR_STORAGE 1
#define HAVE_STRUCT_POLLFD 1
#define HAVE_SCHANNEL 1
#define HAVE_DXVA_H 1
#define HAVE_DXVA2_H 1
#define HAVE_D3D11_H 1
#define HAVE_D3D9_H 1
#define HAVE_WINAPI_FAMILY_H 1
#define HAVE_LIBC_MSVCRT 1
#define HAVE_LOCAL_ALIGNED 1
#define HAVE_VIRTUALALLOC 1
#define HAVE_GETMODULEHANDLE 1
#define HAVE_GETPROCADDRESS 1
#define HAVE_LOADLIBRARY 1
#define HAVE_LOADLIBRARYEX 1

#define HAVE_PTHREADS 0
#define HAVE_W32THREADS 1
#define HAVE_THREADS 1
#define HAVE_STDATOMIC_H 1

#define CONFIG_STATIC 1
#define CONFIG_SHARED 0
#define CONFIG_SMALL 0
#define CONFIG_MEMORY_POISONING 0
#define CONFIG_SWSCALE_ALPHA 1
#define CONFIG_GRAY 0
#define CONFIG_HARDCODED_TABLES 0
#define CONFIG_RUNTIME_CPUDETECT 1

#define CONFIG_CUDA 1
#define CONFIG_FFNVCODEC 1
#define CONFIG_NVDEC 1
#define CONFIG_D3D11VA 1
#define CONFIG_DXVA2 1
#define CONFIG_SCHANNEL 1
#define CONFIG_ZLIB 1

#define HAVE_BIGENDIAN 0
#define HAVE_FAST_64BIT 1

// Architecture feature defaults
#define HAVE_ALTIVEC 0
#define HAVE_AMD3DNOW 0
#define HAVE_AMD3DNOWEXT 0
#define HAVE_ARMV5TE 0
#define HAVE_ARMV6 0
#define HAVE_ARMV6T2 0
#define HAVE_ARMV8 0
#define HAVE_AVX 0
#define HAVE_AVX2 0
#define HAVE_AVX2_EXTERNAL 0
#define HAVE_AVX2_INLINE 0
#define HAVE_AVX512 0
#define HAVE_AVX512ICL 0
#define HAVE_FMA3 0
#define HAVE_FMA4 0
#define HAVE_LASX 0
#define HAVE_LSX 0
#define HAVE_MMI 0
#define HAVE_MMX 0
#define HAVE_MMXEXT 0
#define HAVE_MMX_EXTERNAL 0
#define HAVE_MMX_INLINE 0
#define HAVE_MMXEXT_INLINE 0
#define HAVE_MSA 0
#define HAVE_NEON 0
#define HAVE_SSE 0
#define HAVE_SSE2 0
#define HAVE_SSE3 0
#define HAVE_SSSE3 0
#define HAVE_SSE4 0
#define HAVE_SSE42 0
#define HAVE_VFP 0
#define HAVE_VFPV3 0

#include "config_components.h"

#endif /* FFMPEG_CONFIG_H */
"""
# Keep runtime CPU detection and NASM dispatch; MSVC has no GNU inline asm.
cpu_header = (FFMPEG_SRC / "libavutil/x86/cpu.h").read_text(encoding="utf-8")
instruction_sets = set(re.findall(
    r'^#define (?:EXTERNAL|INLINE)_([A-Z0-9]+)(?:_FAST|_SLOW)?\(',
    cpu_header, re.MULTILINE,
))
external_sets = {name for name in instruction_sets if not name.startswith('AMD3D')}
for instruction in sorted(instruction_sets):
    for suffix in ("", "_EXTERNAL", "_INLINE"):
        macro = f"HAVE_{instruction}{suffix}"
        value = int(instruction in external_sets and suffix != '_INLINE')
        config_h_content = re.sub(rf'^#define {macro} \d+\n', '', config_h_content, flags=re.MULTILINE)
        config_h_content = config_h_content.replace(
            '#include "config_components.h"',
            f'#define {macro} {value}\n#include "config_components.h"',
        )

# NASM consumes the same feature values as the C dispatchers.
asm_macros = dict(re.findall(r'^#define ((?:HAVE|ARCH|CONFIG)_\w+) (\d+)\s*$',
                             config_h_content, re.MULTILINE))
asm_macros.update({f'CONFIG_{name.upper()}': '1' if name in resolved_components else '0'
                   for name in all_components})
# PREFIX is tested with %ifdef by x86inc.asm; omit it for the x64 ABI.
asm_macros.update({'PIC': '1', 'CONFIG_PIC': '1'})
write_if_changed(CONFIG_DIR / 'config.asm',
                 '\n'.join(f'%define {name} {value}' for name, value in sorted(asm_macros.items())) + '\n')

write_if_changed((CONFIG_DIR / "config.h"), config_h_content, encoding="utf-8")

# 5. Generate libavutil/avconfig.h and ffversion.h
write_if_changed((CONFIG_DIR / "libavutil" / "avconfig.h"), """#ifndef AVUTIL_AVCONFIG_H
#define AVUTIL_AVCONFIG_H
#define AV_HAVE_BIGENDIAN 0
#define AV_HAVE_FAST_UNALIGNED 1
#endif /* AVUTIL_AVCONFIG_H */
""", encoding="utf-8")

write_if_changed((CONFIG_DIR / "libavutil" / "ffversion.h"), f"""#ifndef AVUTIL_FFVERSION_H
#define AVUTIL_FFVERSION_H
#define FFMPEG_VERSION "{FFMPEG_VERSION}"
#endif /* AVUTIL_FFVERSION_H */
""", encoding="utf-8")

# 6. Generate list files (*_list.c)
def make_list_c(struct_name, list_name, items, prefix="ff_"):
    lines = [
        "/* Automatically generated by ffmpeg_generator.py for Reemio */",
        f"static const {struct_name} * const {list_name}[] = {{"
    ]
    for item in items:
        lines.append(f"    &{prefix}{item},")
    lines.append("    NULL")
    lines.append("};")
    lines.append("")
    return "\n".join(lines)

# codec_list.c
codec_items = [f"{d}_decoder" for d in ENABLED_DECODERS]
codec_items += [f"{encoder}_encoder" for encoder in ENABLED_ENCODERS]
# Upstream orders external AV1 software decoders before the hardware-only
# native decoder. Preserve that priority rather than the profile's list order.
selected_codecs = set(codec_items)
declarations = re.findall(
    r'^extern const FFCodec ff_(\w+);',
    (FFMPEG_SRC / 'libavcodec/allcodecs.c').read_text(encoding='utf-8'),
    re.MULTILINE,
)
codec_items = [name for name in declarations if name in selected_codecs]
missing_codecs = selected_codecs - set(codec_items)
if missing_codecs:
    raise ValueError(f'Unknown FFmpeg codecs: {sorted(missing_codecs)}')
write_if_changed((CONFIG_DIR / "libavcodec" / "codec_list.c"), make_list_c("FFCodec", "codec_list", codec_items), encoding="utf-8")

# parser_list.c
parser_items = [f"{p}_parser" for p in ENABLED_PARSERS]
write_if_changed((CONFIG_DIR / "libavcodec" / "parser_list.c"), make_list_c("AVCodecParser", "parser_list", parser_items), encoding="utf-8")

# bsf_list.c
bsf_items = [f"{b}_bsf" for b in ENABLED_BSFS]
write_if_changed((CONFIG_DIR / "libavcodec" / "bsf_list.c"), make_list_c("FFBitStreamFilter", "bitstream_filters", bsf_items), encoding="utf-8")

# demuxer_list.c
demuxer_items = [f"{d}_demuxer" for d in ENABLED_DEMUXERS]
write_if_changed((CONFIG_DIR / "libavformat" / "demuxer_list.c"), make_list_c("FFInputFormat", "demuxer_list", demuxer_items), encoding="utf-8")

# muxer_list.c
write_if_changed((CONFIG_DIR / "libavformat" / "muxer_list.c"), make_list_c("FFOutputFormat", "muxer_list", []), encoding="utf-8")

# protocol_list.c
protocol_items = sorted(c for c in resolved_components if c.endswith('_protocol'))
write_if_changed((CONFIG_DIR / "libavformat" / "protocol_list.c"), make_list_c("URLProtocol", "url_protocols", protocol_items), encoding="utf-8")

# filter_list.c
filter_items = [
    'vf_scale', 'vf_format', 'vf_null', 'af_anull', 'af_aresample', 'af_aformat',
    'asrc_abuffer', 'vsrc_buffer', 'asink_abuffer', 'vsink_buffer', 'af_atempo',
]
write_if_changed((CONFIG_DIR / "libavfilter" / "filter_list.c"), make_list_c("AVFilter", "filter_list", filter_items), encoding="utf-8")

# 7. Parse Makefiles to resolve source files
def parse_makefile(lib_dir, extra_conds=None):
    # FFmpeg splits codecs and BSFs across included Makefiles. Architecture C
    # dispatchers are needed even when external/inline assembly is disabled.
    conds = set(resolved_components) | {'threads', 'w32threads', 'libc_msvcrt'}
    conds.update(extra_conds or [])
    conds.add('x86asm')
    conds.update(instruction.lower() for instruction in external_sets)
    visited = set()
    variables = {}

    def expand(value):
        return re.sub(r'\$\(([^)]+)\)', lambda m: variables.get(m[1], ''), value)

    def read_makefile(path):
        if path in visited or not path.exists():
            return
        visited.add(path)
        text = path.read_text(encoding='utf-8', errors='strict')
        text = re.sub(r'\\\n', ' ', text)
        for line in text.splitlines():
            include = re.match(r'-?include \$\(SRC_PATH\)/(.*)', line)
            if include:
                relative = include[1].replace('$(ARCH)', 'x86')
                read_makefile(FFMPEG_SRC / relative)
                continue
            match = re.match(r'([A-Z][A-Z0-9_-]*)(?:-\$\((CONFIG_|HAVE_)(\w+)\))?\s*(\+?=)\s*(.*)', line)
            if not match:
                continue
            name, kind, condition, operation, value = match.groups()
            if condition is not None:
                name += '-yes' if condition.lower() in conds else '-no'
            value = expand(value)
            variables[name] = (variables.get(name, '') + ' ' + value) if operation == '+=' else value

    read_makefile(FFMPEG_SRC / lib_dir / 'Makefile')
    read_makefile(FFMPEG_SRC / lib_dir / 'x86' / 'Makefile')
    sources = set()
    for name in ('OBJS', 'OBJS-yes'):
        for word in variables.get(name, '').split():
            if word.endswith('.o'):
                source = word[:-2] + '.c'
                if (FFMPEG_SRC / lib_dir / source).is_file():
                    sources.add(source)
    assembly = set()
    for name in ('X86ASM-OBJS', 'X86ASM-OBJS-yes'):
        for word in variables.get(name, '').split():
            if word.endswith('.o'):
                source = word[:-2] + '.asm'
                if (FFMPEG_SRC / lib_dir / source).is_file():
                    assembly.add(source)
    if lib_dir == 'libavutil':
        assembly.add('x86/emms.asm')
    # Negated conditions and the generic TLS registry use special Make syntax.
    if lib_dir == 'libavutil':
        sources.add('hwcontext_stub.c')
    if lib_dir == 'libavformat':
        sources.add('tls_schannel.c')
    return sorted(sources), sorted(assembly)

avutil_sources, avutil_assembly = parse_makefile("libavutil", extra_conds=['threads', 'cuda', 'd3d11va', 'dxva2'])
# Add compat/w32pthreads and file_open if needed
swscale_sources, swscale_assembly = parse_makefile("libswscale")
swresample_sources, swresample_assembly = parse_makefile("libswresample")
avcodec_sources, avcodec_assembly = parse_makefile("libavcodec", extra_conds=['nvdec', 'd3d11va', 'dxva2'])
avformat_sources, avformat_assembly = parse_makefile("libavformat", extra_conds=['schannel', 'zlib', 'tls_schannel'])
avfilter_sources, avfilter_assembly = parse_makefile("libavfilter", extra_conds=['threads'])

print(f"Sources: avutil={len(avutil_sources)}, swscale={len(swscale_sources)}, swresample={len(swresample_sources)}, avcodec={len(avcodec_sources)}, avformat={len(avformat_sources)}, avfilter={len(avfilter_sources)}")

# 8. Write sources.cmake
def to_cmake_list(var_name, lib_dir, files):
    lines = [f"set({var_name}"]
    for f in files:
        lines.append(f'  "${{FFMPEG_SRC}}/{lib_dir}/{f}"')
    lines.append(")\n")
    return "\n".join(lines)

sources_cmake = [
    "# Automatically generated by ffmpeg_generator.py for Reemio",
    to_cmake_list("AVUTIL_SOURCES", "libavutil", avutil_sources),
    to_cmake_list("AVUTIL_ASM_SOURCES", "libavutil", avutil_assembly),
    to_cmake_list("SWSCALE_SOURCES", "libswscale", swscale_sources),
    to_cmake_list("SWSCALE_ASM_SOURCES", "libswscale", swscale_assembly),
    to_cmake_list("SWRESAMPLE_SOURCES", "libswresample", swresample_sources),
    to_cmake_list("SWRESAMPLE_ASM_SOURCES", "libswresample", swresample_assembly),
    to_cmake_list("AVCODEC_SOURCES", "libavcodec", avcodec_sources),
    to_cmake_list("AVCODEC_ASM_SOURCES", "libavcodec", avcodec_assembly),
    to_cmake_list("AVFORMAT_SOURCES", "libavformat", avformat_sources),
    to_cmake_list("AVFORMAT_ASM_SOURCES", "libavformat", avformat_assembly),
    to_cmake_list("AVFILTER_SOURCES", "libavfilter", avfilter_sources),
    to_cmake_list("AVFILTER_ASM_SOURCES", "libavfilter", avfilter_assembly),
]
write_if_changed((OUT_DIR / "sources.cmake"), "\n".join(sources_cmake), encoding="utf-8")
print(f"Wrote {OUT_DIR / 'sources.cmake'} successfully.")
