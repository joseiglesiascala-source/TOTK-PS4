# A/B benchmark of the Windows build (docs/WINDOWS.md): runs to gameplay, keeps the log as <TOTK_WIN>\bench\<name>.log
# and averages the replay timings of the intervals with at least -MinDraws draws a frame (the cave beneath Hyrule
# Castle: ~1,650), so runs that end in other scenes still compare.
#   bench-win.ps1 -Name cache-on [-Seconds 220] [-MinDraws 1500] [-Env @{NX_VERTEX_CACHE='0'}]
#   bench-win.ps1 -Summary            # the table of every run kept
param([string]$Name = '', [int]$Seconds = 220, [int]$MinDraws = 1500, [hashtable]$Env = @{}, [switch]$Summary)
$root = if ($env:TOTK_WIN) { $env:TOTK_WIN } else { 'E:\totk-win' }
$dir = Join-Path $root 'bench'
New-Item -ItemType Directory -Force $dir | Out-Null
function Measure-Log([string]$path) {
    $phases = @(); $frames = @()
    foreach ($l in Get-Content $path) {
        if ($l -match 'draw replay per frame: (\d+) draws, textures ([\d.]+) ms, sets ([\d.]+), vertex/index ([\d.]+), pass ([\d.]+), pipeline ([\d.]+), binds\+draw ([\d.]+)') {
            if ([int]$Matches[1] -ge $MinDraws) { $phases += ,@([double]$Matches[1], [double]$Matches[2], [double]$Matches[3], [double]$Matches[4], [double]$Matches[5], [double]$Matches[6], [double]$Matches[7]) }
            $lastDraws = [int]$Matches[1]
        } elseif ($l -match 'per frame over 60: ([\d.]+) fps, .* replay ([\d.]+) ms, .* upload ([\d.]+) MB') {
            if ($lastDraws -ge $MinDraws) { $frames += ,@([double]$Matches[2], [double]$Matches[3]) }
        }
    }
    if (-not $phases.Count) { return $null }
    $avg = { param($rows, $k) ($rows | ForEach-Object { $_[$k] } | Measure-Object -Average).Average }
    [pscustomobject]@{
        run = [IO.Path]::GetFileNameWithoutExtension($path); samples = $phases.Count
        draws = [math]::Round((& $avg $phases 0)); textures = [math]::Round((& $avg $phases 1), 2)
        sets = [math]::Round((& $avg $phases 2), 2); vtx_idx = [math]::Round((& $avg $phases 3), 2)
        pass = [math]::Round((& $avg $phases 4), 2); pipeline = [math]::Round((& $avg $phases 5), 2)
        binds_draw = [math]::Round((& $avg $phases 6), 2)
        replay_ms = if ($frames.Count) { [math]::Round((& $avg $frames 0), 2) } else { $null }
        upload_mb = if ($frames.Count) { [math]::Round((& $avg $frames 1), 1) } else { $null }
    }
}
if ($Summary) { Get-ChildItem $dir -Filter *.log | ForEach-Object { Measure-Log $_.FullName } | Format-Table -AutoSize; return }
if (-not $env:NX_INPUT_SCRIPT) { $env:NX_INPUT_SCRIPT = '70:A:0.2;75:A:0.2;80:A:0.2;95:A:0.2' }
foreach ($k in $Env.Keys) { Set-Item "env:$k" $Env[$k] }
try { & (Join-Path $PSScriptRoot 'run-win.ps1') -Seconds $Seconds | Out-Null }
finally { foreach ($k in $Env.Keys) { Remove-Item "env:$k" -ErrorAction SilentlyContinue } }
$log = Join-Path $dir "$Name.log"
Copy-Item (Join-Path $root 'data\nx.log') $log -Force
Measure-Log $log | Format-List
