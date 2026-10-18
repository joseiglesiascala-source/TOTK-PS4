#!/usr/bin/env bash
# BelmanteGu's Switch recomp (github.com/BelmanteGu/SuperMarioWonderRecomp: the recompiler, the shared runtime with NVN
# on Vulkan, and SMW's PS4 port) at the commit in patches/BASE.txt into ~/totk-build/src/recomp, with this port
# (patches/totk-ps4.patch: SMW's PS4 work + games/totk) applied on a branch "totk".
#   clone-recomp.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="${RECOMP_SRC:-$HOME/totk-build/src/recomp}"
BASE="$(cat "$HERE/../patches/BASE.txt")"
if [ ! -d "$SRC/.git" ]; then
    git clone https://github.com/BelmanteGu/SuperMarioWonderRecomp "$SRC"
fi
git -C "$SRC" fetch -q origin
if ! git -C "$SRC" rev-parse -q --verify totk >/dev/null; then
    git -C "$SRC" checkout -q "$BASE"
    git -C "$SRC" checkout -q -b totk
    git -C "$SRC" apply --index "$HERE/../patches/totk-ps4.patch"
    git -C "$SRC" -c user.name=TOTK -c user.email=totk@localhost commit -q -m "TOTK PS4 port (patches/totk-ps4.patch)"
fi
git -C "$SRC" log -2 --oneline
