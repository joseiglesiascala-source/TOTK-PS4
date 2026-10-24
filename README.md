# TOTK: Tears of the Kingdom recomp for PS4

TOTK is an attempt to run **The Legend of Zelda: Tears of the Kingdom** (Switch, 1.0.0, `0100F2C0115B6000`) natively
on a jailbroken PS4 with the same approach as [SMW](https://github.com/iHaiDeeZ/SMW): BelmanteGu's static
recompiler turns the game's ARM code into C++ that is compiled for the PS4's CPU. The game's NVN graphics are
replayed on Vulkan through Mesa's RADV driver, ported to the PS4's GPU. There's no emulator.

You bring your own game: nothing copyrighted is in this repository. No game code, assets or keys. A package built
from your dump contains your game's code, so it is for your console only.

## Status

**It plays, far too slowly: 2-10 fps.** Development stopped at v00.72 (2026-10-24).

Tested on the console:

- It boots to the title screen and plays the opening movie (VP9 through FFmpeg).
- It loads a save (about 2 minutes) and plays the Great Sky Island with correct lighting, shadows, text and sound.
- The game's memory doesn't fit in the PS4's 4.5 GB of direct memory beside the GPU's copies. A memory manager maps
  64 KiB chunks on first touch and compresses cold ones in RAM with zstd.
- An in-game menu (L3+R3) sets the render resolution, FSR 1 upscaling, the frame counter, the CPU core layout,
  multi-core draw recording and the pass statistics.

Why it is slow, as measured on the console:

- The GPU is busy only about 10 ms a frame. The CPU is the limit.
- Every NVN draw is replayed through Vulkan and RADV on one Jaguar core, at 65-90 µs a draw. TOTK draws 1,000-2,300
  times a frame, so the replay takes 80-190 ms.
- The cost is spread over many small steps; none is above about 2% of the replay.
- The game's own logic takes about 35-70 ms in its slower frames.
- Multi-core draw recording (from the Breath of the Wild port) lost time in TOTK: the game rewrites uniform data
  inside its passes about 38 times a frame, and each rewrite makes the replay wait for every worker.

20 fps would need a different renderer, writing the PS4's GPU commands directly or translating draws on several
cores where the game records them, and faster recompiled code.

Settings in `/data/TOTK/config.txt` that trade quality for speed:

- `NX_PASS_EVERY` draws chosen render passes only every n-th frame, for example the shadow cascades:
  `d0 360x360 vk124 layer 1/4=2;c0 256x144 vk37, d0 256x144 vk126=2@1`. The pass names come from the pass
  statistics in `nx.log` (`NX_PASS_STATS`).
- `NX_PARALLEL=1` turns on multi-core draw recording; it is off by default.
- `NX_KERNEL_SPIN_US` sets how long a kernel wait spins before sleeping (50 by default, 0 turns it off).
- `NX_WRITE_WATCH=0` turns off page-protection tracking of CPU-written textures.

### What makes TOTK harder than Wonder

- **Memory.** The Switch gives the game about 3.2 GB, and the PS4 port also keeps GPU copies of textures. A PS4 app
  gets about 5 GB, and SMW already ran short of direct memory in courses.
- **CPU.** SMW replays about 600 draws a frame in about 25 ms on one Jaguar core. TOTK draws 1,000-2,300.
- **Storage.** The RomFS is about 17 GB and is streamed while you play. The PS4's drive read at 5-27 MB/s in
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
