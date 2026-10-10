#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITA64="$DEVKITPRO/devkitA64"
project_dir="$PWD"
ffmpeg_revision="$(cat tools/ffmpeg-nvtegra-revision.txt)"
patch_revision="$(cat tools/patches/*.patch | shasum -a 256 | cut -d ' ' -f 1)"
mkdir -p deps/ffmpeg-nvtegra build-nvtegra
if [ ! -f deps/ffmpeg-nvtegra/lib/libavcodec.a ] || \
   [ "$(cat deps/ffmpeg-nvtegra/revision.txt 2>/dev/null || true)" != "$ffmpeg_revision" ] || \
   [ "$(cat deps/ffmpeg-nvtegra/patches.txt 2>/dev/null || true)" != "$patch_revision" ] || \
   [ "${REBUILD_FFMPEG:-0}" = 1 ]; then
  if [ ! -d upstream/ffmpeg-nvtegra ]; then
    git clone --depth 1 --branch nvtegra https://github.com/averne/FFmpeg.git upstream/ffmpeg-nvtegra
  fi
  if [ -f upstream/ffmpeg-nvtegra/PINNED_REVISION ]; then
    test "$(cat upstream/ffmpeg-nvtegra/PINNED_REVISION)" = "$ffmpeg_revision"
  elif [ "$(git -C upstream/ffmpeg-nvtegra rev-parse HEAD)" != "$ffmpeg_revision" ]; then
    git -C upstream/ffmpeg-nvtegra fetch --depth 1 origin "$ffmpeg_revision"
    git -C upstream/ffmpeg-nvtegra checkout --detach "$ffmpeg_revision"
  fi
  for patch in tools/patches/*.patch; do
    if git -C upstream/ffmpeg-nvtegra apply --check "$project_dir/$patch" 2>/dev/null; then
      git -C upstream/ffmpeg-nvtegra apply "$project_dir/$patch"
    elif ! git -C upstream/ffmpeg-nvtegra apply --reverse --check "$project_dir/$patch" 2>/dev/null; then
      echo "Cannot apply required FFmpeg patch: $patch" >&2
      exit 1
    fi
  done
  cd build-nvtegra
  if [ -f ffbuild/config.mak ]; then make clean; fi
  ../upstream/ffmpeg-nvtegra/configure --prefix="$project_dir/deps/ffmpeg-nvtegra" \
    --enable-cross-compile --cross-prefix="$DEVKITA64/bin/aarch64-none-elf-" \
    --arch=aarch64 --target-os=none --disable-everything --enable-decoder=h264 \
    --enable-hwaccel=h264_nvtegra --enable-nvtegra --enable-gpl \
    --enable-avcodec --enable-avutil --enable-swscale --disable-programs \
    --disable-doc --disable-network --disable-autodetect \
    --disable-pthreads --disable-w32threads --disable-os2threads \
    --extra-cflags="-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE -I$DEVKITPRO/libnx/include -D__SWITCH__ -D_DEFAULT_SOURCE" \
    --extra-ldflags="-march=armv8-a+crc+crypto -mtp=soft -fPIE -specs=$DEVKITPRO/libnx/switch.specs -L$DEVKITPRO/libnx/lib" \
    --extra-libs=-lnx
  make -j"${JOBS:-8}"
  make install
  printf '%s\n' "$ffmpeg_revision" > "$project_dir/deps/ffmpeg-nvtegra/revision.txt"
  printf '%s\n' "$patch_revision" > "$project_dir/deps/ffmpeg-nvtegra/patches.txt"
  cd "$project_dir"
fi
make -j"${JOBS:-8}"
