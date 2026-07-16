#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$ROOT/third_party"

if [[ ! -d "$ROOT/third_party/godot-cpp/.git" ]]; then
  git clone --depth 1 https://github.com/godotengine/godot-cpp.git "$ROOT/third_party/godot-cpp"
else
  echo "godot-cpp ya existe."
fi

cat <<MSG
Dependencia Godot preparada.
DepthAI debe estar instalado. Puedes reutilizar la instalación anterior o ejecutar:
  DEPTHAI_SOURCE=/ruta/a/depthai-core ./scripts/install_depthai_macos.sh
MSG
