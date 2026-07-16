#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT/scripts/find_tools.sh"

CMAKE_BIN="${CMAKE_BIN:-$(find_tool cmake)}"
NINJA_BIN="${NINJA_BIN:-$(find_tool ninja)}"

DEPTHAI_PREFIX="${DEPTHAI_PREFIX:-$ROOT/third_party/depthai-install}"
DEPTHAI_BUILD="$ROOT/build/depthai-macos-arm64"
VCPKG_PREFIX="$DEPTHAI_BUILD/vcpkg_installed/arm64-osx"
VCPKG_CMAKE_MODULES="$VCPKG_PREFIX/share/opencv4;$VCPKG_PREFIX/share/ffmpeg"

BUILD_DIR="$ROOT/build/macos-arm64-debug"
ADDON_BIN="$ROOT/godot/addons/godot_oak/bin"
PLUGIN="$ADDON_BIN/libgodot_oak.macos.dylib"

if [[ ! -d "$DEPTHAI_PREFIX" ]]; then
  echo "ERROR: no se encuentra DepthAI en: $DEPTHAI_PREFIX" >&2
  exit 1
fi

if [[ ! -d "$VCPKG_PREFIX" ]]; then
  echo "ERROR: no se encuentra el prefijo vcpkg de DepthAI:" >&2
  echo "  $VCPKG_PREFIX" >&2
  exit 1
fi

mkdir -p "$ADDON_BIN"

echo "Configurando godot-oak..."
"$CMAKE_BIN" -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_MAKE_PROGRAM="$NINJA_BIN" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
  -DCMAKE_PREFIX_PATH="$DEPTHAI_PREFIX;$VCPKG_PREFIX" \
  -DCMAKE_MODULE_PATH="$VCPKG_CMAKE_MODULES"

echo "Compilando godot-oak..."
"$CMAKE_BIN" --build "$BUILD_DIR" --parallel 4

if [[ ! -f "$PLUGIN" ]]; then
  echo "ERROR: no se generó $PLUGIN" >&2
  exit 1
fi

echo "Limpiando runtime anterior..."
find "$ADDON_BIN" \
  -maxdepth 1 \
  -type f \
  -name '*.dylib' \
  ! -name 'libgodot_oak.macos.dylib' \
  -delete

copy_dylibs() {
  local directory="$1"

  if [[ ! -d "$directory" ]]; then
    return
  fi

  while IFS= read -r -d '' dylib; do
    cp -Lf "$dylib" "$ADDON_BIN/$(basename "$dylib")"
  done < <(
    find "$directory" \
      -maxdepth 1 \
      -type f \
      -name '*.dylib' \
      -print0
  )
}

echo "Empaquetando librerías de DepthAI..."
copy_dylibs "$DEPTHAI_PREFIX/lib"

echo "Empaquetando librerías de OpenCV y vcpkg..."
copy_dylibs "$VCPKG_PREFIX/lib"

echo "Corrigiendo referencias dinámicas..."

while IFS= read -r -d '' binary; do
  binary_name="$(basename "$binary")"

  if [[ "$binary" != "$PLUGIN" ]]; then
    install_name_tool \
      -id "@rpath/$binary_name" \
      "$binary" 2>/dev/null || true
  fi

  while IFS= read -r dependency; do
    dependency_name="$(basename "$dependency")"

    if [[ -f "$ADDON_BIN/$dependency_name" ]]; then
      install_name_tool \
        -change "$dependency" \
        "@loader_path/$dependency_name" \
        "$binary" 2>/dev/null || true
    fi
  done < <(
    otool -L "$binary" |
      tail -n +2 |
      awk '{print $1}'
  )

  chmod 755 "$binary"
done < <(
  find "$ADDON_BIN" \
    -maxdepth 1 \
    -type f \
    -name '*.dylib' \
    -print0
)

echo "Comprobando dependencias locales no resueltas..."

unresolved=0

while IFS= read -r -d '' binary; do
  while IFS= read -r dependency; do
    case "$dependency" in
      /usr/lib/*|/System/*|@loader_path/*|@rpath/*)
        ;;
      *)
        echo "Dependencia potencialmente no empaquetada:"
        echo "  $(basename "$binary") -> $dependency"
        unresolved=1
        ;;
    esac
  done < <(
    otool -L "$binary" |
      tail -n +2 |
      awk '{print $1}'
  )
done < <(
  find "$ADDON_BIN" \
    -maxdepth 1 \
    -type f \
    -name '*.dylib' \
    -print0
)

if [[ "$unresolved" -ne 0 ]]; then
  echo "ERROR: quedan dependencias externas sin empaquetar." >&2
  exit 1
fi

echo
echo "Verificando plugin..."
file "$PLUGIN"
nm -gU "$PLUGIN" | grep godot_oak_library_init

echo
echo "Dependencias del plugin:"
otool -L "$PLUGIN"

echo
echo "Runtime empaquetado:"
find "$ADDON_BIN" \
  -maxdepth 1 \
  -type f \
  -name '*.dylib' \
  -print |
  sort

echo
echo "Build completado: $PLUGIN"
