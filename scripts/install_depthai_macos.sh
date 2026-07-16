#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT/scripts/find_tools.sh"
CMAKE_BIN="${CMAKE_BIN:-$(find_tool cmake)}"
NINJA_BIN="${NINJA_BIN:-$(find_tool ninja)}"
DEPTHAI_SOURCE="${DEPTHAI_SOURCE:-$ROOT/third_party/depthai-core}"
PREFIX="${DEPTHAI_PREFIX:-$ROOT/third_party/depthai-install}"
BUILD_DIR="$ROOT/build/depthai-macos-arm64"

if [[ ! -f "$DEPTHAI_SOURCE/CMakeLists.txt" ]]; then
  echo "No se encuentra depthai-core en: $DEPTHAI_SOURCE" >&2
  exit 1
fi

"$CMAKE_BIN" -S "$DEPTHAI_SOURCE" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_MAKE_PROGRAM="$NINJA_BIN" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DDEPTHAI_OPENCV_SUPPORT=OFF \
  -DDEPTHAI_BUILD_EXAMPLES=OFF \
  -DDEPTHAI_BUILD_TESTS=OFF \
  -DBUILD_SHARED_LIBS=ON

"$CMAKE_BIN" --build "$BUILD_DIR" --parallel 2
"$CMAKE_BIN" --install "$BUILD_DIR"
echo "DepthAI instalado en $PREFIX"
