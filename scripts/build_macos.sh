#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT/scripts/find_tools.sh"
CMAKE_BIN="${CMAKE_BIN:-$(find_tool cmake)}"
NINJA_BIN="${NINJA_BIN:-$(find_tool ninja)}"
DEPTHAI_PREFIX="${DEPTHAI_PREFIX:-$ROOT/third_party/depthai-install}"
BUILD_DIR="$ROOT/build/macos-arm64-debug"

"$CMAKE_BIN" -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_MAKE_PROGRAM="$NINJA_BIN" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_PREFIX_PATH="$DEPTHAI_PREFIX"

"$CMAKE_BIN" --build "$BUILD_DIR" --parallel 4

LIB="$ROOT/godot/addons/godot_oak/bin/libgodot_oak.macos.dylib"
file "$LIB"
nm -gU "$LIB" | grep godot_oak_library_init
echo "Build completado: $LIB"
