# TOTK on Windows (debug build)

A native x64 build of the same recompiled game and NVN→Vulkan runtime as the PS4 port. It runs on the PC's NVIDIA
GPU, so the renderer can be inspected locally with validation, RenderDoc and frame dumps, without a package per test.
It is a debugging tool, not a release target.

## Status (2026-10-20)

- **It boots and reaches gameplay unattended** on the RTX 5070 Ti: intro text, the opening cutscene's dialogue, then
  "Beneath Hyrule Castle" with the hearts and button prompts. It runs at about 30 fps (the game's own cap) using
  5.2-5.5 GB, with no crash in 3 minutes.
- **FIXED (recomp `2038780`): the world is lit.** Link, the cave walls and pillars beneath Hyrule Castle render, and the
  title screen shows its 3D temple background, at 30 fps. Cause and fix: "Root cause" below. Screenshots:
  `E:\totk-win\shots\ts0-*.png`.
- Before that, the world was black here too, as on the PS4: the bug was in the shared NVN layer, not in RADV/GNM.
  - v1 (runtime `886006d`, alias sync and depth clamp on): new game, opening in the tunnels: black.
  - v3 (runtime `ce9b461` merged: alias sync off, `NX_DEPTH_CLAMP=0`): the title screen renders correctly. Continue
    (scripted A, `NX_INPUT_SCRIPT`) loads "Beneath Hyrule Castle": only dark green fog over black, no lit geometry.
    **Depth clamp off does not fix it on NVIDIA**, so the shadow map's depth-0 clamp is not the whole cause.
- Screenshots (frame dumps, not in the repo: the game's own images): `E:\totk-win\shots\win-v{1,2,3}-*.png`
  (v2 = title screen).
- The first run creates a save (`data\TOTK\user`). Later runs stop at the title screen: press A, or set
  `NX_INPUT_SCRIPT=70:A:0.2;75:A:0.2;80:A:0.2;95:A:0.2` (gameplay from ~100 s).

## Layout

Everything is in `E:\totk-win` (`TOTK_WIN`), outside this repo and WSL, for fast builds:

| Where | What |
|---|---|
| `recomp\` | a clone of `~/totk-build/src/recomp`, branch `totk`: Windows commit `c19717d`, the PS4 side merged in, black-world fix and tools `19af98e` (also branch `win-build` in WSL) |
| `gen\{main,subsdk0,sdk}` | copied from `~/totk-build/src/recomp/private/totk/gen` (same bases as the PS4 build) |
| `third_party\Vulkan-Headers-1.4.357` | copied from `~/totk-build/third_party` |
| `third_party\opus` | libopus 1.5.2 (xiph.org source, BSD), static, built with clang-cl `/arch:AVX2` |
| `data\TOTK` | copied from `~/totk-build/data-stage/TOTK` (romfs.pak/idx, spirv, analysis), 17 GB |
| `data\nx.log` | the log (the previous run's in `nx.log.prev`); `data\TOTK\frames\*.ppm` holds frame dumps |
| `build\totk.exe` | the build (253 MB) |

## Toolchain

- Visual Studio Community 2026 (18.9) with **C++ Clang tools for Windows** (clang-cl 22.1.3), added for this.
  MSVC alone cannot build the generated code, which uses `__int128`.
- CMake 4.2, Ninja 1.13, Python 3.12 (Pillow, for converting frame dumps) were already installed.
- The **LunarG Vulkan SDK 1.4.363** is used for its validation layer only. The headers stay at 1.4.357, the version
  `vk_gen` was generated from. The NVIDIA driver provides `vulkan-1.dll`.

## Build and run

```bat
scripts\build-win.cmd configure   & rem first time, or after CMakeLists changes
scripts\build-win.cmd             & rem incremental: ninja -j8 (each generated file takes up to ~1.5 GB)
```

A full build takes about 65 minutes at `-j8` on this PC (1,252 steps, ~25 s per generated file).

```powershell
scripts\run-win.ps1 -Seconds 180 -DumpEvery 600    # windowed; every 600th presented frame -> frames\NNNNN.ppm
scripts\run-win.ps1 -Ps4Like                       # PS4 defaults: 1280x720 + FSR, pipelined submits, dynamic UBOs
scripts\run-win.ps1 -Validation                    # VK_LAYER_KHRONOS_validation from C:\VulkanSDK -> data\validation.txt
```

With no save in `data\TOTK\user`, a new game starts and the opening plays by itself up to the first gameplay in the
tunnels beneath the castle. No input is needed to reach the black world.

To get the source changes: commit in `E:\totk-win\recomp`, then `git fetch /mnt/e/totk-win/recomp totk:win-build`
from WSL (the branch `win-build` there has `c19717d`). To pick up the PS4 side's newer commits: `git pull` in
`E:\totk-win\recomp` from `//wsl.localhost/Ubuntu/home/shiro/totk-build/src/recomp`. Keep `core.autocrlf=true` in that
clone: its files were checked out with CRLF, and changing the setting marks every file as modified.

## What had to change for Windows (recomp `c19717d`)

1. `games/totk/game.cpp` calls `setenv`, which the Windows C library lacks. A `_putenv_s` shim for `_WIN32` keeps the
   same rule: a value already set stays.
2. **hwopus.** Without libopus the sdk's `nn::codec::DecodeOpusInterleavedWithHardware` dereferenced null about 800
   frames in, as the PS4 did before `hle_opus.cpp`. `-DNX_OPUS_DIR=<prefix>` now builds `hle_opus.cpp` with
   `NX_HAS_OPUS` and links `opus.lib`, like the PS4 build's PacBrew libopus.
   - libopus's own CMake assumes MSVC (no per-file `-msse4.1`/`-mavx2`). With clang-cl it needs
     `CMAKE_C_FLAGS=/DWIN32 /D_WINDOWS /arch:AVX2`.
3. The game modules are compiled with `/clang:-ffp-contract=off` under clang-cl, as on the PS4 and Linux: no fused
   multiply-add the guest didn't do.

## First validation run (150 s on runtime `886006d`, `data\validation.txt`)

Leads for the black world. Each message appears up to 10 times (the layer's duplicate limit).

| VUID | What | Why it matters |
|---|---|---|
| `VkRenderingInfo-colorAttachmentCount-06087` | rendering into an image created with only `TRANSFER_SRC/DST\|SAMPLED` usage | draws into a texture that was never made a render target: undefined, likely dropped, on NVIDIA. If that texture is a shadow/AO/light buffer, the composite reads zeros. **Check first.** |
| `vkCmdDrawIndexed-imageLayout-00344` | `fp_t_tcb_8` (set 2, binding 128) sampled as `SHADER_READ_ONLY_OPTIMAL` while the image is in `GENERAL` | a descriptor's layout doesn't match the image's: wrong reads on some GPUs |
| `VkGraphicsPipelineCreateInfo-renderPass-06582` | a pipeline with a **BC4** color attachment | a render target aliased as a compressed format: that draw cannot write |
| `VkImageViewCreateInfo-usage-02275` | an `R8G8B8A8_SRGB` view with storage usage | sRGB formats can't be storage images on NVIDIA |
| `vkCmdDrawIndexed-dynamicRenderingUnusedAttachments-08912` | the pipeline has a format for attachment 2, the pass binds none | needs `VK_EXT_dynamic_rendering_unused_attachments`, or the format set to UNDEFINED |
| `VkShaderModuleCreateInfo-pCode-08740/08742` | SPIR-V uses `DrawParameters` and `SPV_EXT_fragment_shader_interlock` without the feature/extension enabled | enable `shaderDrawParameters` and `VK_EXT_fragment_shader_interlock` at device creation |
| `RuntimeSpirv-maxCombinedClipAndCullDistances-00710` | ClipDistance[8] + CullDistance[1] = 9 > 8 | the translator should size the clip array to what's written |
| `vkCmdCopyBufferToImage-imageOffset-09104` | a 3D R8 32³ image uploaded with depth 32 into a mip of depth 16 | upload extent per mip is wrong for 3D textures |
| `vkQueueSubmit-pSignalSemaphores-00067` | a present semaphore reused while the swapchain may still hold it | present sync only, not the black world |

## Root cause of the black world (recomp `2038780`)

The game guards its clears with conditional rendering: for each target,
`SetRenderEnableConditional(RENDER_IF_NOT_EQUAL, X)` → clear → `ReportCounter(type 16 TIMESTAMP_TOP, X)` →
`SetRenderEnable(true)`. On Maxwell a timestamp report writes **value 0** (plus the time), and the slot at X+16 stays
0, so the condition is false and these clears **don't run**. The runtime did two things differently:

1. `SetRenderEnableConditional` was replayed as "always on", and clears ignored `render_enable` anyway.
2. `ReportCounter` wrote a running number as the value of every report, timestamps included.

So every guarded clear ran. One of them clears the half-resolution depth (800×450, written by the depth downsample
`f5fa432b1a4f00d8` / `d57c13410e7b55bb`) right before the **light volume** `1c1a145f2341e191` is drawn: back faces
(cull front), depth test GREATER against that depth. With the depth at 1.0, no light-volume pixel passed. Its output
(target `2E`, blue channel) is the gate of the deferred lights (`_336 = clamp(G-buffer flag 2 · 0.25 + 2E.z)`); the
terrain has no flag 2 (`c11[32].w = 0`), so every terrain light multiplied by 0.

Fix: conditions are evaluated at replay from the two 16-byte reports (mode 0 equal, 1 not equal), clears respect a
disabled render state, and timestamp reports (types 0 and 16) carry value 0. Bisect switches: `NX_COND_RENDER=0`,
`NX_COUNTER_FAKE_VALUES=1`.

How it was found: a **probe shader**. The class-0 light `f36ed92c72249fef` was decompiled (spirv-cross), its output
replaced by intermediate values, recompiled (glslangValidator, block types renamed `*_t` so the variable names stay
the original ones) and dropped over `data\TOTK\spirv\<hash>.spv` for one run (original backed up), with
`NX_NO_DEPTH_TEST_FS=<hash>` and `NX_DIAG_FLOAT=1`. That showed the gate at 0 and `2E` sampled as 0; `NX_DIAG_CMDS=1`
then showed the guarded clears and `NX_TRACE_COND=1` the compared values (`[n time] [0 0]`).

Not needed for this scene, kept as Maxwell behaviour: targets the fragment shader doesn't write get no colour writes
(`19af98e`; the terrain's OMAP is 0xF0F0).

## Black-world investigation on the PC (recomp `19af98e`)

How TOTK lights the world (read from the shaders and the storyboard):

1. The **G-buffer** pass writes c0 (material ID in R, ×255), c1 (albedo), c3 (normals). c0 is cleared to 0 first.
2. A **classifier** (fs `f43252e25d01957e` full-res, `06a345940cd18bca` / `8b4b7755bcb83bb9` half-res) reads c0.R,
   masks it (`c3[1]`: 0xAF, 0x8E, 0x38) and writes `FragDepth = id/255·0.5 + 0.5` into a **D16** target cleared to 0.5.
3. **Lighting quads**, one per material class, are drawn with depth **EQUAL** at that class's depth (vertex shader:
   `z = (c11[25].x/255·0.5 + 0.5 − 0.5)·2`, depth mode −1..1). Full-res classes: 0 and 6 (instanced ×240 screen tiles,
   culled by a per-tile flag at `vp_s0[tile·16 + 16844]`), 8–13 (full screen). Half-res: 24, 48.
4. The half-res outputs and the full-res lighting go into the composite (fs `1e584cb25715ffca`), then fog, tonemap.

What was measured (`NX_DIAG_FLOAT`, `NX_DIAG_DEPTH`, `NX_EQUAL_AS`, `NX_TRACE_*`):

| Finding | Evidence | Status |
|---|---|---|
| **Colour targets the shader doesn't write got zeros.** The terrain/rock G-buffer shaders (`b2376c55538c83aa`, `75e03c13a2784b64`) have OMAP `0xF0F0`: they write targets 1 and 3 only. Maxwell leaves target 0 alone; Vulkan leaves it undefined and NVIDIA wrote 0 over it. | original shader headers in `private/totk/shaders_extracted/*/code_<hash>.bin` (+0x30, word 18) | **fixed** in `19af98e` (colour write mask 0 for targets with no shader output; `NX_UNWRITTEN_TARGETS=1` = old way). A first lit object (a pale blade, top centre) now shows. |
| The full-res lighting pass outputs **exactly 0** at every pixel; the half-res EQUAL pass too. | `diag: float …_1e584cb25715ffca: … nz 0%` | open |
| Quads are placed right: forced to ALWAYS + depth write, they land at 0.5627 (class 32) / 0.5941 (class 48). | `NX_EQUAL_AS=7 NX_EQUAL_WRITE=1 NX_DIAG_DEPTH=1` | ok |
| With `NX_EQUAL_AS=6` (GEQUAL) the full-res pass covers 100%; with 3 (LEQUAL) 0%. | | the stored class is below every quad that runs |
| D16 stores the classifier's 0.5 as 32767 (0.499992): a quad at exactly 0.5 can miss by one unit. | depth stats 0.499992 .. 0.750973 | real, but `NX_D16_AS_D32=1` alone does not light anything |
| Most geometry is **class 0** (c0 cleared to 0, terrain doesn't write it). G-buffer R values in the cave: 0 (93%, incl. empty), 8, 9, 11, 12, 128. | storyboard histogram | expected: same on the Switch |
| Class 0's quad (fs `f36ed92c72249fef`, vs `af5bb99208ab0ef7`) is tile-culled by flags in a storage buffer; **all 240 flags are 1**, the range is shared with the compute that writes it (`NX_TRACE_WRITTEN`). | `NX_TRACE_VS_WORDS=16844,240,16` | not the cause |
| Even with **no depth test** for `f36ed92c`, it adds nothing: its output (and alpha) is 0. Its alpha is `(t²)·(flag bit 0 of a G-buffer byte)·(1 − exp(…))`. | `NX_NO_DEPTH_TEST_FS=f36ed92c72249fef` | **next lead** |
| The half-res classes 24/48 don't occur in this scene (classifier mask 0x38 gives 0/8). | traced `c11[25]`, masks | expected |
| Ruled out: depth bias (never on for EQUAL draws), depth bounds (recorded only), stencil (D16 has none), depth clip control / range (0..1, −1..1 on), c1 constant data, inline uniform updates, the lighting uniforms (sun direction, colours plausible), the BC4 targets (a boot-time 32³ noise volume and 64² tiles only). | | |

(Solved: see "Root cause" above. The table is the trail that led there.)

### Debug switches added on this build

| Switch | What |
|---|---|
| `NX_DIAG_FLOAT=1` | with `NX_DIAG_EVERY`: float targets (RGBA16F, R32F, RG16F) logged per channel: min..max, mean, share not 0 |
| `NX_DIAG_DEPTH=1` | the depth target of every storyboard pass logged (min .. max) |
| `NX_D16_AS_D32=1` | D16 made as D32 float |
| `NX_EQUAL_AS=<VkCompareOp>` / `NX_EQUAL_WRITE=1` | depth EQUAL (no write) draws tested with that op / writing their depth |
| `NX_NO_DEPTH_BIAS=1` | no depth bias anywhere (a bias on an EQUAL draw is logged) |
| `NX_UNWRITTEN_TARGETS=1` | the old behaviour for targets the shader doesn't write |
| `NX_GRAPHICS_SHARED=1` | vertex/fragment storage writes join the submit's shared written ranges |
| `NX_TRACE_VS_WORDS=off,count,stride` | in the draw trace: vertex storage 0 words (guest memory, a frame late) |
| `NX_TRACE_WRITTEN=1` | written-range binds and reads |
| (always) | `render into a compressed image: …` once per texture/level |
| `NX_COND_RENDER=0` / `NX_COUNTER_FAKE_VALUES=1` | the old always-on conditions / running-number counter values |
| `NX_SKIP_CLEARS_AFTER_FS=<hash>` | the clears after a draw with that fragment shader are skipped |
| `NX_DIAG_CMDS=1` | targets, depth state, counters and conditions into the storyboard's `passes.txt` |
| `NX_TRACE_COND=1` | the counter values each condition compared |
| `NX_DIAG_FLOAT=1` (RGBA8) | RGBA8 targets too, with the share of each low bit of the alpha byte (G-buffer flags) |

The parent session's tools (`a6e21cc`): `NX_NO_DEPTH_TEST_FS`, `NX_TRACE_UNIFORM`, `NX_TRACE_FS_STORAGE`,
`NX_TRACE_CLEAR_DS`. One test cycle (rebuild of a runtime file + a 200 s run to gameplay) takes about 5 minutes.

## Next steps

- **The PS4 needs `2038780`**: merge branch `win-build` (WSL repo) into `totk`, rebuild, package. The fix is in the
  shared NVN layer, so RADV gets it the same way.
- The PS4-only defaults added while chasing this (`NX_DEPTH_CLAMP=0`, alias sync off, `NX_GRAPHICS_WRITE_BACK=0`,
  `NX_EQUAL_EXACT=1`) can be re-checked on the PC now that the world draws.
- Open validation errors from the first run (rendering into a BC4 image through an RGBA16UI view, the image layout
  mismatch, the 3D upload extent) are separate from the black world.
- RenderDoc: its winget install queued behind the stuck Vulkan SDK installer and never ran.
- The Vulkan SDK installer was still paused at the end of this session (console title starting "Select"). Pressing
  Esc in that window lets it finish. The layer already works through `VK_LAYER_PATH=C:\VulkanSDK\1.4.363.0\Bin`
  (`run-win.ps1 -Validation` sets it).
