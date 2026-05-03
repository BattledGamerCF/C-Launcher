#!/usr/bin/env bash
# Generate genesis.icns from genesis.png. Must run on macOS (uses sips + iconutil).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/genesis.png"
OUT="$HERE/genesis.icns"
TMP="$(mktemp -d)/genesis.iconset"
mkdir -p "$TMP"

for sz in 16 32 64 128 256 512; do
    sips -z "$sz" "$sz"        "$SRC" --out "$TMP/icon_${sz}x${sz}.png"        >/dev/null
    sips -z "$((sz*2))" "$((sz*2))" "$SRC" --out "$TMP/icon_${sz}x${sz}@2x.png" >/dev/null
done
sips -z 1024 1024 "$SRC" --out "$TMP/icon_512x512@2x.png" >/dev/null

iconutil -c icns "$TMP" -o "$OUT"
echo "Wrote $OUT"
