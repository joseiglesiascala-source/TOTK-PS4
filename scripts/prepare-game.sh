#!/usr/bin/env bash
# Turns your own The Legend of Zelda: Tears of the Kingdom 1.0.0 dump (0100F2C0115B6000, XCI or NSP) into what the PS4
# build needs, with the recomp's tools (docs in ~/totk-build/src/recomp: docs/nvn/TRANSLATOR.md, SHADERS.md):
#
#   1. import    the XCI/NSP with your keys -> analysis/{main,subsdk0,sdk}.bin + main.npdm, exefs/, extract/romfs
#                (about 16 GB)
#   2. recompile main, subsdk0 and sdk to C++                              -> gen/{main,subsdk0,sdk}
#   3. shaders   extract the Maxwell blocks and translate them to SPIR-V (.NET 10 + Ryujinx's translator)
#                                                                            -> spirv/<hash>.spv, results.tsv
#
#   prepare-game.sh <folder with prod.keys, title.keys and the XCI/NSP> [steps...]
#   (steps: import recompile shaders; default all three)
#
# Everything lands in ~/totk-build/src/recomp/private/totk (the tools only write under private/). Nothing here is
# shared or committed: it is your game. Needs: clang, cmake, ninja, python3-venv, curl; spirv-val (apt install
# spirv-tools) validates the shaders if present.
set -euo pipefail
IN="$(readlink -f "${1:?folder with prod.keys, title.keys and the XCI/NSP}")"; shift
STEPS="${*:-import recompile shaders}"
SRC="${RECOMP_SRC:-$HOME/totk-build/src/recomp}"
G="$SRC/private/totk"
T="$SRC/private/third_party"
VENV="$SRC/private/venv"
TITLE=0100F2C0115B6000
mkdir -p "$G" "$T"
cd "$SRC"

has() { [[ " $STEPS " == *" $1 "* ]]; }
venv() {
    [ -x "$VENV/bin/python" ] || python3 -m venv "$VENV"
    "$VENV/bin/python" -m pip -q install capstone lz4 zstandard fonttools
}

if has import; then
    echo "== import"
    cmake -S android/wonder/importer -B private/build/importer -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
    ninja -C private/build/importer nximport
    shopt -s nullglob nocaseglob
    games=("$IN"/*.xci "$IN"/*.nsp)
    shopt -u nullglob nocaseglob
    [ ${#games[@]} -gt 0 ] || { echo "no .xci / .nsp in $IN" >&2; exit 1; }
    keys=("$IN/prod.keys"); [ -f "$IN/title.keys" ] && keys+=("$IN/title.keys")
    NX_IMPORT_NOSYNC=1 private/build/importer/nximport --title "$TITLE" --exefs --out "$G" "${keys[@]}" "${games[@]}"
    sync
    ls -la "$G/analysis"
fi

if has recompile; then
    echo "== recompile"
    venv
    # Where each module is generated for (games/totk/game.cpp, scripts/make-stub-gen.sh): main at the base, sdk
    # 256 MiB above it, subsdk0 at +384 MiB; the runtime reserves 512 MiB for the modules (memory.cpp).
    declare -A BASE=([main]=0x4000000000 [sdk]=0x4010000000 [subsdk0]=0x4018000000)
    declare -A ROOM=([main]=0x10000000 [sdk]=0x8000000 [subsdk0]=0x8000000)
    for mod in main subsdk0 sdk; do
        [ -f "$G/exefs/$mod" ] || { echo "missing $G/exefs/$mod (import step)" >&2; exit 1; }
        # The module's size in memory (text, ro, data and bss) must fit below the next module.
        size=$("$VENV/bin/python" - "$G/exefs/$mod" <<'EOF'
import struct, sys
raw = open(sys.argv[1], "rb").read()
end = max(struct.unpack_from("<III", raw, 0x10 + i * 0x10)[1] + struct.unpack_from("<III", raw, 0x10 + i * 0x10)[2]
          for i in range(3))
print(end + struct.unpack_from("<I", raw, 0x3C)[0])
EOF
)
        printf '%-8s %d MiB at %s\n' "$mod" $((size >> 20)) "${BASE[$mod]}"
        [ "$size" -le $((ROOM[$mod])) ] || { echo "$mod is $size bytes: more than the $((ROOM[$mod] >> 20)) MiB it has" >&2; exit 1; }
        "$VENV/bin/python" -m recomp "$G/exefs/$mod" "$G/gen/$mod" --name "$mod" --base "${BASE[$mod]}"
    done
    du -sh "$G/gen"/*
fi

if has shaders; then
    echo "== shaders"
    venv
    if [ ! -x "$T/dotnet/dotnet" ]; then
        mkdir -p "$T/dotnet"
        curl -L https://builds.dotnet.microsoft.com/dotnet/Sdk/10.0.401/dotnet-sdk-10.0.401-linux-x64.tar.gz | tar -xz -C "$T/dotnet"
    fi
    if [ ! -d "$T/ryubing/src" ]; then
        mkdir -p "$T/ryubing"
        curl -L https://codeberg.org/Software_arch/Ryubing/archive/a82350bb77.tar.gz | tar -xz -C "$T/ryubing" --strip-components=1
    fi
    # Invariant globalization: WSL's Ubuntu has no libicu, which .NET would otherwise require.
    export DOTNET_ROOT="$T/dotnet" DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1
    "$DOTNET_ROOT/dotnet" build tools/maxwell/spirv_harness/SpirvHarness.csproj -c Release -m:2
    H="private/build/spirv_harness/bin/Release/net10.0/spirv_harness.dll"
    P="$VENV/bin/python"; X=tools/maxwell/containers.py; O="$G/shaders_extracted"; R="$G/extract/romfs"
    mkdir -p "$O"
    # Tears of the Kingdom's shaders (1.0.0, checked): the shader archives in Shader/ (FSHA), Lib/ (TextureShader.bnsh
    # and the agl, gsys and terrain packs: 8,575 stages), the effects (1,563 of 1,577 Effect/*.esetb.byml.zs carry
    # their own BNSH) and the UI layouts (UI/LayoutArchive/*.blarc.zs). The boot pack holds no shaders. The PS4 has
    # no translator at run time, so this set must be complete. Most files are zstd with the dictionaries in
    # Pack/ZsDic.pack.zs (containers.py reads them). Files without shaders extract nothing and their folder is removed.
    while IFS= read -r -d '' f; do
        rel="${f#"$R"/}"
        n="$(printf '%s' "$rel" | sed 's#[/ ]#_#g; s#\.zs$##')"
        "$P" $X "$f" --extract "$O/$n" --unique >/dev/null 2>&1 || true
        [ -n "$(ls -A "$O/$n" 2>/dev/null | grep -v manifest.tsv)" ] || rm -rf "$O/$n"
    done < <(find "$R/Shader" "$R/Lib" "$R/Effect" "$R/UI/LayoutArchive" -type f -print0 \
                  2>/dev/null)
    "$P" tools/maxwell/exefs_shaders.py "$G/analysis/main.bin" --extract "$O/exefs"
    echo "$(ls "$O" | wc -l) shader containers extracted"
    if command -v spirv-val >/dev/null; then val=(--spirv-val "$(command -v spirv-val)"); else val=(--no-val); fi
    "$DOTNET_ROOT/dotnet" "$H" --batch "$O" --out "$G/spirv" --jobs "${SHADER_JOBS:-6}" "${val[@]}"
    echo "$(ls "$G/spirv" | grep -c '\.spv$') shaders"
fi
echo "done: $G (next: scripts/rebuild-totk.sh, scripts/package-totk.sh, scripts/stage-data.sh)"
