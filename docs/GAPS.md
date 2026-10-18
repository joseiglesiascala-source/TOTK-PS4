# What TOTK needs beyond Wonder

A comparison of the recomp runtime (branch `totk`) with [MacNativeGames](https://github.com/ly05010419/MacNativeGames),
which runs TOTK 1.0.0 on Apple silicon (2026-10-18). Mac paths are relative to its `games/totk/`. "Inferred" means
not read in code but concluded.

## Done

- Three modules (main, subsdk0, sdk) loaded and linked; `SystemResourceSize` from `analysis/main.npdm`.
  - Mac reports 32 MiB.
  - subsdk0 is the movie library.
- The recompiler refuses `DT_RELR`, which 1.4.3 uses (1.0.0 does not, inferred). Without the check, pointers would
  be left unrelocated without any error.
- Translator fixes from MacNativeGames (`totk-ast-side-effects.patch`, `totk-dead-node-calls.patch`).
  - Without them, atomics whose result is unused are dropped.
  - TOTK's sun-visibility kernel lost its `atomicMin` that way.
- zstd dictionaries (`Pack/ZsDic.pack.zs`) in `tools/maxwell/containers.py`, with the Python `zstandard` module only.

## Checked on the 1.0.0 dump (2026-10-18)

- **Sizes.** main is 70 MiB, sdk 13 MiB, subsdk0 5.6 MiB. All have DT_INIT at +0x30 and no RELR.
- **Relocations.** main has 503,796 RELA and 640 JMPREL entries.
- **SystemResourceSize.** 0x1800000 (24 MiB) from main.npdm. Mac uses 32 MiB.
- **Audio.** The sdk has `AudioRenderSystem::Update`, `CommandListProcessor::Process` and
  `CreateAudioRendererManagerByDfc`, so the recomp's in-process audio path (Wonder's) should apply.
- **Entry point.** The sdk has SDK 16's `nn::init::Start` with three callbacks.
- **RomFS.** 304,207 files in 2,664 folders, 17.04 GB.

## To check at the first boot

- **Constructor order.** The recomp runs DT_INIT sdk → subsdk0 → main. Mac's notes say main → subsdk0 → sdk.
- **Memory accounting.**
  - TOTK's allocators use the alias region (64 GiB at 0x70_0000_0000 on Mac).
  - GetInfo 7/22 should count mapped alias pages.
  - MapPhysicalMemory should fail with 0x10801 past the 3.2 GiB pool. In the recomp it always succeeds.
  - Ref: Mac `runtime/services.c:118-150`.

## Services (Mac answers at the nn:: function level)

| Area | Missing in the recomp |
|---|---|
| fs | RenameFile, MountSaveDataReadOnly, IsSaveDataExisting, MountTemporaryStorage, SetPriorityOnCurrentThread, InitializeForApplication |
| am / oe | ISelfController focus and notification commands, IAudioController volume balance (reads 0 now), TryPopNotificationMessage, GetProgramTotalActiveTime, GetPreviousProgramIndex, FinishStartupLogo |
| time | ClockSnapshot, CalculateSpanBetween, user clock differences (IStaticService 400/401/500/501) |
| hid | the 13 `nn::hid::detail::*` vibration and six-axis imports |
| nfp (amiibo) | everything: answer "no device"; the Attach*Event calls want handles |
| hwopus | everything (Mac: libopus, `runtime/opus_codec.c`) |
| swkbd | everything; used once, to name a horse (main+0x1e5cb58, at most 9 characters) |
| movie | `movie::Decoder`, 34 symbols. 28 `Event/Movie/*.webm` files, VP9 profile 0, 1080p30; the game aborts at the first cutscene (the blood moon) without it. The PS4 needs a software VP9 decoder (libvpx). |
| prepo | stub (probably fine) |

Saves:
- `save/<uid>/slot_XX/progress.sav`.
- A temporary storage that is wiped at every start.
- Cache storage.
- Other titles' saves (the BotW link) report not found.

## NVN

98 entry points that Mac implements have no recomp HLE. They return 0 today (`nvn_trace.cpp`). Those that change
the picture:

- `nvnDeviceSetWindowOriginMode`, `nvnDeviceSetDepthMode` (near = -w depth)
- `nvnCommandBufferCopyCommands` (TOTK splices recorded commands)
- events: `nvnEventInitialize/Signal/GetValue/Finalize`, `nvnCommandBufferSignalEvent`
- separate textures and samplers: `BindSeparateTexture(s)/Sampler(s)` (+ Deferred), `nvnDeviceGetSeparateTexture/SamplerHandle`
- copies and clears: `CopyBufferToBuffer`, `CopyBufferToTexture`, `CopyTextureToBuffer`, `ClearTexture(ui)`
- `nvnTextureReadTexels(Strided)`
- `SetSampleMask`, `SetPrimitiveRestart`, `SetAlphaRef`, `SetLineWidth`, `SetPointSize`

Memory pools:
- TOTK uses physical and virtual pools.
- Mac aliases 64 KiB pages so that CPU writes through either view meet.
- The recomp only records virtual mappings (`nvn_objects.cpp`).

Not needed on PS4:
- The vertex-counter lowering (Metal lacks subgroup ops in vertex shaders).
- Cube-group merging (a Metal speed-up).

## Game patches (Mac `runtime/game_patches.c`, 1.0.0)

| Patch | For the PS4 |
|---|---|
| Render resolution, main+0xc630bc | useful: lower the internal resolution (docked 1600x900 / 1280x720) |
| Real-time frame step | optional; it also needs the GameOver RomFS overlay, or retry hangs |
| 60 fps interval, 1440p output | not needed |
| Tagged menu pointer crash at main+0x15ecb0c during save load | needed: Mac fixes it in its SIGSEGV handler |

## Memory (Mac's figures)

- **Graphics heap:** 197 MiB, of which DynamicTextureAllocator is 111 MiB.
- **Mac process:** 4.5-5.3 GiB, growing to 6.25 GiB over 3 hours.
- **Metal allocations:** 2.9-3.3 GiB at 2K.
- **The PS4:** about 5 GB for an app. ASTC textures are decoded to bigger formats on the CPU.
