#!/usr/bin/env bash
# Build a Genesis AppImage from an installed CMake tree.
#
# Usage:
#   cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --parallel
#   cmake --install build --prefix build/AppDir/usr
#   ./packaging/linux/build-appimage.sh build/AppDir
set -euo pipefail

APPDIR="${1:-build/AppDir}"
APPIMAGETOOL="${APPIMAGETOOL:-appimagetool}"

if [[ ! -d "$APPDIR/usr/bin" ]]; then
    echo "ERROR: $APPDIR/usr/bin not found. Run cmake --install first." >&2
    exit 1
fi

mkdir -p "$APPDIR/usr/share/applications" \
         "$APPDIR/usr/share/icons/hicolor/256x256/apps"

cp packaging/linux/genesis.desktop "$APPDIR/usr/share/applications/"
cp packaging/linux/genesis.desktop "$APPDIR/genesis.desktop"

if [[ -f packaging/linux/genesis.png ]]; then
    cp packaging/linux/genesis.png "$APPDIR/usr/share/icons/hicolor/256x256/apps/"
    cp packaging/linux/genesis.png "$APPDIR/genesis.png"
    cp packaging/linux/genesis.png "$APPDIR/.DirIcon"
fi

cat > "$APPDIR/AppRun" <<'EOF'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
export LD_LIBRARY_PATH="$HERE/usr/lib:$LD_LIBRARY_PATH"
exec "$HERE/usr/bin/Genesis" "$@"
EOF
chmod +x "$APPDIR/AppRun"

if ! command -v "$APPIMAGETOOL" >/dev/null 2>&1; then
    echo "appimagetool not on PATH. Download from"
    echo "  https://github.com/AppImage/AppImageKit/releases"
    echo "and set APPIMAGETOOL env var to its path."
    exit 1
fi

ARCH=x86_64 "$APPIMAGETOOL" "$APPDIR" "Genesis-x86_64.AppImage"
echo "Built: Genesis-x86_64.AppImage"
