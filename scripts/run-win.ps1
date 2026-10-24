# Run the Windows build (scripts/build-win.cmd) on the PS4's data stage (docs/WINDOWS.md).
#   run-win.ps1 [-Seconds N] [-Ps4Like] [-Validation] [-Headless] [-DumpEvery N]
# Log: <TOTK_WIN>\data\nx.log (the previous one kept as nx.log.prev). Frames (-DumpEvery): data\TOTK\frames\*.ppm.
# -Ps4Like: the PS4's defaults (platform_ps4.cpp: 1280x720 render, FSR, pipelined submits, dynamic UBOs), to
# reproduce what the console draws. -Validation: VK_LAYER_KHRONOS_validation (needs the Vulkan SDK) -> data\validation.txt.
param([int]$Seconds = 0, [switch]$Ps4Like, [switch]$Validation, [switch]$Headless, [int]$DumpEvery = 0)
$ErrorActionPreference = 'Stop'
$root = if ($env:TOTK_WIN) { $env:TOTK_WIN } else { 'E:\totk-win' }
$exe = Join-Path $root 'build\totk.exe'
$env:NX_DATA_ROOT = Join-Path $root 'data\TOTK'
$env:NX_LOG = Join-Path $root 'data\nx.log'
if (-not $env:NX_MEM_LIMIT_GB) { $env:NX_MEM_LIMIT_GB = '16' }
if ($Ps4Like) {
    $env:NX_RENDER_RES = '1280x720'; $env:NX_FSR = '1'; $env:NX_PIPELINED = '1'; $env:NX_DYNAMIC_UBO = '1'
}
if ($Validation) {
    # The runtime's NVN path has no debug messenger: the layer writes its own log (duplicates capped at 10).
    $env:VK_INSTANCE_LAYERS = 'VK_LAYER_KHRONOS_validation'
    if (-not $env:VK_LAYER_PATH) {
        $sdk = Get-ChildItem C:\VulkanSDK -Directory -ErrorAction SilentlyContinue | Sort-Object Name | Select-Object -Last 1
        if ($sdk) { $env:VK_LAYER_PATH = Join-Path $sdk.FullName 'Bin' }
    }
    $env:VK_KHRONOS_VALIDATION_DEBUG_ACTION = 'VK_DBG_LAYER_ACTION_LOG_MSG'
    $env:VK_KHRONOS_VALIDATION_LOG_FILENAME = Join-Path $root 'data\validation.txt'
    $env:VK_KHRONOS_VALIDATION_REPORT_FLAGS = 'error,warn'
}
if ($Headless) { $env:NX_HEADLESS = '1'; $env:NX_MUTE = '1' }
if ($DumpEvery -gt 0) { $env:NX_DUMP_FRAMES = "$DumpEvery" }
$p = Start-Process -FilePath $exe -WorkingDirectory $root -PassThru `
    -RedirectStandardError (Join-Path $root 'data\stderr.txt')
if ($Seconds -gt 0) {
    if (-not $p.WaitForExit($Seconds * 1000)) { Stop-Process -Id $p.Id -Force; "stopped after $Seconds s" }
    else { "exited: $($p.ExitCode)" }
} else { "started pid $($p.Id)" }
