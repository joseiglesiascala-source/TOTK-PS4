#!/usr/bin/env bash
# Lays out the game data the PS4 runtime reads from /data/TOTK (scripts/prepare-game.sh's output, your own dump):
#
#   analysis/{main,subsdk0,sdk}.bin, main.npdm   the modules as loaded, the program metadata
#   extract/romfs/...                     the RomFS (about 16 GB)
#   spirv/<hash>.spv, spirv/results.tsv   the translated shaders and the translator's records
#   config.txt                            NX_* switches (written if missing; see README.md)
#
#   stage-data.sh [out dir]   (default ~/totk-build/data-stage/TOTK; files are hard links where possible, no copies)
#
# Then copy the folder's contents to /data/TOTK on the console over FTP (FileZilla, or scripts/upload-data.sh from
# Windows), once; a new build of the app needs only the new package. Never share it: it is the game.
set -euo pipefail
SRC="${RECOMP_SRC:-$HOME/totk-build/src/recomp}"
G="$SRC/private/totk"
OUT="${1:-$HOME/totk-build/data-stage/TOTK}"
for f in analysis/main.bin analysis/subsdk0.bin analysis/sdk.bin analysis/main.npdm spirv/results.tsv; do
    [ -f "$G/$f" ] || { echo "missing $G/$f (scripts/prepare-game.sh)" >&2; exit 1; }
done
[ -d "$G/extract/romfs" ] || { echo "missing $G/extract/romfs" >&2; exit 1; }
mkdir -p "$OUT/analysis" "$OUT/spirv" "$OUT/extract"
link() { ln -f "$1" "$2" 2>/dev/null || cp -f "$1" "$2"; }
link "$G/analysis/main.bin" "$OUT/analysis/main.bin"
for f in subsdk0.bin sdk.bin main.npdm; do link "$G/analysis/$f" "$OUT/analysis/$f"; done
link "$G/spirv/results.tsv" "$OUT/spirv/results.tsv"
find "$G/spirv" -maxdepth 1 -name '*.spv' -exec ln -f -t "$OUT/spirv" {} + 2>/dev/null ||
    find "$G/spirv" -maxdepth 1 -name '*.spv' -exec cp -f -t "$OUT/spirv" {} +
rm -rf "$OUT/extract/romfs"
cp -al "$G/extract/romfs" "$OUT/extract/romfs" 2>/dev/null || cp -a "$G/extract/romfs" "$OUT/extract/romfs"
if [ ! -f "$OUT/config.txt" ]; then
    cat > "$OUT/config.txt" <<'EOF'
# TOTK settings: one NX_<KEY>=<value> per line, read at startup ('#' comments). See README.md.
# NX_PS4_CORES=pinned        guest threads on their Switch core's PS4 core (default: balanced; or spread)
# NX_BUTTONS=nintendo        Circle = A, Cross = B (by position); default Cross = A
# NX_HANDHELD=1              the game's handheld mode (720p) instead of docked (1080p)
# NX_RENDER_RES=1600x900     the size the game renders at while docked (lighter on the GPU)
# NX_FSR=1                   upscale to 1080p with FidelityFX Super Resolution 1 instead of a plain stretch
# NX_MUTE=1                  no sound
EOF
fi
echo "$(find "$OUT" -type f | wc -l) files, $(du -sh "$OUT" | cut -f1) in $OUT"
echo "copy its contents to /data/TOTK on the console"
