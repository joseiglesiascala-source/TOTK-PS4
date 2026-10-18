# TOTK: Tears of the Kingdom recomp for PS4

TOTK is an attempt to run **The Legend of Zelda: Tears of the Kingdom** (Switch, 1.0.0, `0100F2C0115B6000`) natively
on a jailbroken PS4 with the same approach as [SMW](https://github.com/iHaiDeeZ/SMW): BelmanteGu's static
recompiler turns the game's ARM code into C++ that is compiled for the PS4's CPU. The game's NVN graphics are
replayed on Vulkan through Mesa's RADV driver, ported to the PS4's GPU. There's no emulator.

You bring your own game: nothing copyrighted is in this repository. No game code, assets or keys. A package built
from your dump contains your game's code, so it is for your console only.

## Status

**Early work. The game doesn't run yet.**

| Step | State |
|---|---|
| Runtime with three code modules (`main`, `subsdk0`, `sdk`) | done |
| Separate PS4 app (`TOTK00001`, data in `/data/TOTK`), own art | done |
| Runtime builds and packages against stub modules (`TOTK-v00.00`) | done, a smoke test only |
| zstd dictionary support in the shader extractor | done, untested on real data |
| Import, recompile and translate the shaders from a TOTK 1.0.0 dump | waiting for a dump |
| Switch services, NVN features and patches TOTK needs beyond Wonder | to do |
| Boot to the title screen on a PS4 | to do |

### What makes TOTK harder than Wonder

- **Memory.** The Switch gives the game about 3.2 GB, and the PS4 port also keeps GPU copies of textures. A PS4 app
  gets about 5 GB, and SMW already ran short of direct memory in courses.
- **CPU.** SMW replays about 600 draws a frame in about 25 ms on one Jaguar core. Hyrule draws many more, so the
  NVN replay will need to run on more than one core.
- **Storage.** The RomFS is about 16 GB and is streamed while you play. The PS4's drive read at 5-27 MB/s in
  SMW's tests.

## Building

See [docs/BUILDING.md](docs/BUILDING.md); the work list is [docs/GAPS.md](docs/GAPS.md). In short (WSL Ubuntu, the SMW build tree already set up):

```sh
bash scripts/seed-build-tree.sh                        # ~/totk-build from ~/smw-build, ~/totk -> this repo
bash scripts/clone-recomp.sh                           # the recompiler at patches/BASE.txt + patches/totk-ps4.patch
bash scripts/prepare-game.sh <folder with keys + dump> # import, recompile, shaders -> private/totk
bash scripts/rebuild-totk.sh                           # eboot
TOTK_VERSION=00.01 bash scripts/package-totk.sh        # out/TOTK-v00.01.pkg
bash scripts/stage-data.sh                             # data for /data/TOTK
```

## Credits

See [CREDITS.md](CREDITS.md). [MacNativeGames](https://github.com/ly05010419/MacNativeGames) (MIT), which runs TOTK
on Apple silicon, is the reference for what the game needs from the Switch OS and NVN.
