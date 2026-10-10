#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build
cxx="${CXX:-clang++}"
"$cxx" -std=c++17 -Wall -Wextra -fsanitize=address,undefined \
  -Iinclude tests/wire_test.cpp -o build/wire-test
./build/wire-test
# Use pkg-config if available; Homebrew fallback works on the development Mac.
if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists libpng; then
  png_flags="$(pkg-config --cflags --libs libpng)"
elif [ -d /opt/homebrew/opt/libpng ]; then
  png_flags="-I/opt/homebrew/opt/libpng/include -L/opt/homebrew/opt/libpng/lib -lpng -lz"
else
  echo 'Install libpng development headers to run cursor tests.' >&2
  exit 1
fi
# Intentional splitting of compiler flags from pkg-config.
# shellcheck disable=SC2086
"$cxx" -std=c++17 -Wall -Wextra -fsanitize=address,undefined \
  -Iinclude tests/cursor_test.cpp $png_flags -o build/cursor-test
./build/cursor-test
python3 tests/test_sender.py

python3 tests/test_ffmpeg_refs.py

"$cxx" -std=c++17 -Wall -Wextra -fsanitize=address,undefined -Iinclude tests/session_test.cpp -o build/session-test
./build/session-test
"$cxx" -std=c++17 -Wall -Wextra -fsanitize=address,undefined -Iinclude tests/stability_test.cpp -o build/stability-test
./build/stability-test

if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists libavcodec libavutil libswscale sdl2 libpng; then
  python3 tests/test_video.py
else
  echo 'Host Video stress test skipped: install FFmpeg/SDL2 development headers.'
fi
"$cxx" -std=c++17 -Wall -Wextra -fsanitize=address,undefined -Iinclude tests/performance_test.cpp -o build/performance-test
./build/performance-test
"$cxx" -std=c++17 -Wall -Wextra -fsanitize=address,undefined -Iinclude tests/h264_stream_test.cpp -o build/h264-stream-test
./build/h264-stream-test
