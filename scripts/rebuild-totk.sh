#!/usr/bin/env bash
# Configure + build with logs (~/totk-build/logs/{configure,build}.log) and an error summary.
#   rebuild-totk.sh [clean]
# JOBS (default 8): the generated code's files are large; each compile takes up to ~1.5 GB of memory.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD="${TOTK_BUILD:-$HOME/totk-build/build/totk}"
[ "${1:-}" = clean ] && rm -rf "$BUILD"
bash "$HERE/logrun.sh" configure bash "$HERE/configure-totk.sh" || { tail -40 "$HOME/totk-build/logs/configure.log"; exit 1; }
# A link that fails must not leave the last eboot behind for package-totk.sh (v00.25 once shipped the old one).
# Ninja makes it again from the executable when that is up to date.
rm -f "$BUILD/totk_eboot/eboot.bin"
bash "$HERE/logrun.sh" build bash -c "source '$HERE/ps4-env.sh' && ninja -C '$BUILD' -j${JOBS:-8} -k 0"
rc=$?
L="$HOME/totk-build/logs/build.log"
tail -1 "$L"; echo "errors: $(grep -c 'error:' "$L")"
grep "error:" "$L" | sed 's/.*src\/wonder\///' | sort | uniq -c | sort -rn | head -${TOP:-60} || true
# The build's own status: a failed build must stop a `rebuild-totk.sh && package-totk.sh` chain.
exit $rc
