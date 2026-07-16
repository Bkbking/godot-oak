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
  echo "ERROR: no se encuentra depthai-core en: $DEPTHAI_SOURCE" >&2
  exit 1
fi

echo "CMake: $CMAKE_BIN"
echo "Ninja: $NINJA_BIN"
echo "DepthAI source: $DEPTHAI_SOURCE"
echo "Install prefix: $PREFIX"
echo
echo "OpenCV support: ON"
echo "Dynamic calibration: ON"
echo

"$CMAKE_BIN" -S "$DEPTHAI_SOURCE" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_MAKE_PROGRAM="$NINJA_BIN" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DDEPTHAI_BOOTSTRAP_VCPKG=ON \
  -DDEPTHAI_VCPKG_INTERNAL_ONLY=OFF \
  -DDEPTHAI_OPENCV_SUPPORT=ON \
  -DDEPTHAI_MERGED_TARGET=ON \
  -DDEPTHAI_DYNAMIC_CALIBRATION_SUPPORT=ON \
  -DDEPTHAI_BUILD_EXAMPLES=OFF \
  -DDEPTHAI_BUILD_TESTS=OFF \
  -DDEPTHAI_PCL_SUPPORT=OFF \
  -DDEPTHAI_RTABMAP_SUPPORT=OFF \
  -DDEPTHAI_BASALT_SUPPORT=OFF \
  -DBUILD_SHARED_LIBS=ON

"$CMAKE_BIN" --build "$BUILD_DIR" --parallel 2
"$CMAKE_BIN" --install "$BUILD_DIR"

echo
echo "Verificando OpenCV..."
grep -E \
  "DEPTHAI_OPENCV_SUPPORT:BOOL=ON|DEPTHAI_MERGED_TARGET:BOOL=ON" \
  "$BUILD_DIR/CMakeCache.txt"

if ! find "$BUILD_DIR/vcpkg_installed/arm64-osx" \
  -name 'OpenCVConfig.cmake' \
  -print -quit | grep -q .; then
  echo "ERROR: OpenCV no aparece instalado por vcpkg." >&2
  exit 1
fi

echo
echo "DepthAI con OpenCV instalado en:"
echo "  $PREFIX"
