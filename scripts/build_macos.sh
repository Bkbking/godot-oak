#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT/scripts/find_tools.sh"

CMAKE_BIN="${CMAKE_BIN:-$(find_tool cmake)}"
NINJA_BIN="${NINJA_BIN:-$(find_tool ninja)}"

DEPTHAI_PREFIX="${DEPTHAI_PREFIX:-$ROOT/third_party/depthai-install}"
BUILD_DIR="$ROOT/build/macos-arm64-debug"
ADDON_BIN="$ROOT/godot/addons/godot_oak/bin"
PLUGIN="$ADDON_BIN/libgodot_oak.macos.dylib"

if [[ ! -d "$DEPTHAI_PREFIX" ]]; then
  echo "ERROR: no se encuentra DepthAI en: $DEPTHAI_PREFIX" >&2
  echo "Ejecuta primero scripts/install_depthai_macos.sh" >&2
  exit 1
fi

mkdir -p "$ADDON_BIN"

echo "Configurando godot-oak..."
"$CMAKE_BIN" -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_MAKE_PROGRAM="$NINJA_BIN" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_PREFIX_PATH="$DEPTHAI_PREFIX"

echo "Compilando godot-oak..."
"$CMAKE_BIN" --build "$BUILD_DIR" --parallel 4

if [[ ! -f "$PLUGIN" ]]; then
  echo "ERROR: no se generó $PLUGIN" >&2
  exit 1
fi

echo "Empaquetando librerías de DepthAI..."

# Copiar todas las dylibs instaladas por DepthAI.
while IFS= read -r -d '' dylib; do
  cp -f "$dylib" "$ADDON_BIN/"
done < <(find "$DEPTHAI_PREFIX/lib" \
  -maxdepth 1 \
  -type f \
  -name '*.dylib' \
  -print0)

# libusb procede del árbol vcpkg usado para construir DepthAI.
LIBUSB_PATH="$(
  find "$ROOT/build/depthai-macos-arm64" \
    -path '*/arm64-osx/lib/libusb-1.0.dylib' \
    -type f \
    -print -quit 2>/dev/null || true
)"

if [[ -z "$LIBUSB_PATH" || ! -f "$LIBUSB_PATH" ]]; then
  echo "ERROR: no se encontró libusb-1.0.dylib." >&2
  echo "Se esperaba dentro de build/depthai-macos-arm64." >&2
  exit 1
fi

cp -f "$LIBUSB_PATH" "$ADDON_BIN/libusb-1.0.dylib"

echo "Corrigiendo referencias dinámicas..."

# Procesar el plugin y todas las dylibs empaquetadas.
while IFS= read -r -d '' binary; do
  basename_binary="$(basename "$binary")"

  # Las librerías deben identificarse mediante @rpath.
  if [[ "$binary" == *.dylib && "$binary" != "$PLUGIN" ]]; then
    install_name_tool -id "@rpath/$basename_binary" "$binary"
  fi

  # Sustituir dependencias empaquetadas por referencias relativas al addon.
  while IFS= read -r dependency; do
    dependency_basename="$(basename "$dependency")"

    if [[ -f "$ADDON_BIN/$dependency_basename" ]]; then
      install_name_tool \
        -change "$dependency" \
        "@loader_path/$dependency_basename" \
        "$binary" 2>/dev/null || true
    fi
  done < <(
    otool -L "$binary" |
      tail -n +2 |
      awk '{print $1}'
  )

  chmod 755 "$binary"
done < <(find "$ADDON_BIN" -maxdepth 1 -type f -name '*.dylib' -print0)

echo "Verificando binario..."
file "$PLUGIN"
nm -gU "$PLUGIN" | grep godot_oak_library_init

echo
echo "Dependencias del plugin:"
otool -L "$PLUGIN"

echo
echo "Runtime empaquetado:"
find "$ADDON_BIN" -maxdepth 1 -type f -name '*.dylib' -print | sort

echo
echo "Build completado: $PLUGIN"
