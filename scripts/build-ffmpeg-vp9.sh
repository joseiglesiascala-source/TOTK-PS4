#!/usr/bin/env bash
# FFmpeg's VP9 decoder for the PS4 build (static libavcodec + libavutil, nothing else): the pre-rendered movies
# (Event/Movie/*.webm, VP9 1080p30) are decoded by the runtime's movie decoder (hle_movie.cpp) with it. The SDK's
# own path ends on the Tegra's hardware decoder, which the PS4 does not have.
#
# Built with rpcs3's PS4 toolchain wrappers (clang 21 on the OpenOrbis C runtime; ~/rpcs3/toolchain), the one this
# port's sysroot libraries come from, from the FFmpeg 7.1.1 source rpcs3's scripts/build-deps.sh fetched. With
# FFmpeg's x86 assembly (nasm): the VP9 SSSE3/AVX paths are picked at run time (the Jaguar has AVX, not AVX2).
#
#   scripts/build-ffmpeg-vp9.sh        -> ~/totk-build/ffmpeg-vp9/{include,lib}
set -euo pipefail
TC="${TC:-$HOME/rpcs3/toolchain}"
source "$TC/env.sh"
SRC="${FFMPEG_SRC:-$HOME/rpcs3-build/src/deps/ffmpeg}"
OUT="${FFMPEG_VP9_PREFIX:-$HOME/totk-build/ffmpeg-vp9}"
BLD="$HOME/totk-build/build/ffmpeg-vp9"
[ -x "$SRC/configure" ] || { echo "no FFmpeg source at $SRC (rpcs3's scripts/build-deps.sh ffmpeg fetches it)"; exit 1; }
rm -rf "$BLD"; mkdir -p "$BLD"
cd "$BLD"
"$SRC/configure" --prefix="$OUT" \
    --enable-cross-compile --target-os=freebsd --arch=x86_64 --cpu=btver2 \
    --cc="$TC/bin/ps4-cc" --cxx="$TC/bin/ps4-c++" --ld="$TC/bin/ps4-cc" \
    --ar="$PS4_AR" --ranlib="$PS4_RANLIB" --nm=llvm-nm-21 --strip=llvm-strip-21 \
    --pkg-config=false --enable-static --disable-shared --enable-pic \
    --disable-programs --disable-doc --disable-network --disable-autodetect --disable-iconv \
    --x86asmexe=nasm --disable-debug --disable-everything \
    --enable-avcodec --enable-avutil --disable-avformat --disable-avdevice --disable-avfilter \
    --disable-swscale --disable-swresample \
    --enable-decoder=vp9 --enable-parser=vp9 \
    --extra-cflags="-O2"
# musl's libc.a has these, so configure finds them, but they are Linux system calls the PS4's FreeBSD kernel would take
# as other calls (as in rpcs3's recipe): CPU counts come from sysconf.
sed -i -E 's/^#define HAVE_(SCHED_GETAFFINITY|GETAUXVAL|PRCTL) 1$/#define HAVE_\1 0/' config.h
grep -E "HAVE_(SCHED_GETAFFINITY|GETAUXVAL|PRCTL|PTHREADS|X86ASM) " config.h
make -j"${JOBS:-8}"
make install
echo "ffmpeg-vp9 installed in $OUT"
