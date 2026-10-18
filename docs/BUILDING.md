# Building TOTK

TOTK is BelmanteGu's Switch static recompiler and runtime ([SuperMarioWonderRecomp](https://github.com/BelmanteGu/SuperMarioWonderRecomp)),
with SMW's PS4 port and a `games/totk` target. The package is built from **your own dump**: its `eboot.bin` is
mostly the game's code, recompiled to x86-64, so it can't be downloaded and must never be shared.

## Layout

| Where | What |
|---|---|
| this repo (`~/totk` in WSL) | scripts, toolchain, PS4 runtime (`port/`), `patches/totk-ps4.patch` |
| `~/totk-build/src/recomp` | the recompiler checkout, branch `totk` (`scripts/clone-recomp.sh`) |
| `~/totk-build/src/recomp/private/totk` | your game: `analysis/`, `exefs/`, `extract/romfs`, `gen/`, `spirv/` |
| `~/totk-build/build/{mesa,totk}` | RADV (Crema's build, copied from SMW) and the eboot |
| `~/totk-build/out`, `out/` | packages; `~/totk-build/oelf/vXX.YY.oelf` keeps every build's symbols |

## Steps

1. **Build tree**: `scripts/seed-build-tree.sh` copies the sysroot, Mesa, Vulkan-Headers 1.4.357, the fixed PkgTool
   and the .NET SDK from `~/smw-build` (SMW must have been set up once).
2. **Recompiler**: `scripts/clone-recomp.sh`. After changes there: `scripts/save-patch.sh "message"`.
3. **Your game**: put `prod.keys` (and `title.keys`) with the TOTK 1.0.0 XCI or NSP in one folder, then
   `scripts/prepare-game.sh <folder>` (steps `import recompile shaders`, or any of them).
   - The modules are generated for main `0x4000000000`, sdk `0x4010000000`, subsdk0 `0x4018000000`. The script
     stops if a module doesn't fit below the next one.
   - `SystemResourceSize` comes from `analysis/main.npdm` at run time.
4. **Eboot**: `scripts/rebuild-totk.sh` (logs in `~/totk-build/logs`). Without a dump:
   `TOTK_GEN=$(scripts/make-stub-gen.sh) scripts/rebuild-totk.sh` builds the runtime alone (it stops at boot).
5. **Package**: `TOTK_VERSION=00.01 scripts/package-totk.sh`, then from Windows (Git Bash)
   `scripts/upload-to-console.sh out/TOTK-v00.01.pkg`.
6. **Data**: `scripts/stage-data.sh`, then copy `~/totk-build/data-stage/TOTK` to `/data/TOTK` on the console
   (`scripts/upload-data.py`).
