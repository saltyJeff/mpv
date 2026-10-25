These Matroska fixtures contain two synthetic red frames at 64 x 64 and 5 fps.
They exercise the H.264, HEVC, VP9 and AV1 decoder paths without requiring a GPU
or a media encoder at test time. They contain no externally sourced media.

They were generated with FFmpeg's `color` source and the libx264, libx265,
libvpx-vp9 and libaom-av1 encoders, respectively. For example:

```sh
ffmpeg -f lavfi -i color=c=red:s=64x64:r=5:d=0.4 -an -frames:v 2 \
  -pix_fmt yuv420p -c:v libaom-av1 -cpu-used 8 -threads 1 av1.mkv
```
