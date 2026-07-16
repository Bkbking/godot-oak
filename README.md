# godot-oak

MVP para mostrar el stream RGB de una Luxonis OAK-D Pro W dentro de Godot mediante GDExtension C++.

## Alcance actual

- macOS Apple Silicon.
- Godot 4.7.
- DepthAI v3.
- RGB 640×360 a 30 FPS.
- `ImageTexture` temporalmente; todavía no es zero-copy.

## Preparación

```bash
./scripts/bootstrap.sh
```

Si ya compilaste `depthai-core`, reutiliza esa instalación indicando `DEPTHAI_PREFIX`. Si no:

```bash
PATH="$PWD/third_party/pkgconf-install/bin:$PATH" \
PKG_CONFIG="$PWD/third_party/pkgconf-install/bin/pkgconf" \
DEPTHAI_SOURCE="$PWD/third_party/depthai-core" \
./scripts/install_depthai_macos.sh
```

## Compilar

```bash
DEPTHAI_PREFIX="$PWD/third_party/depthai-install" \
./scripts/build_macos.sh
```

Después abre `godot/project.godot` con Godot 4.7 y pulsa Play.
