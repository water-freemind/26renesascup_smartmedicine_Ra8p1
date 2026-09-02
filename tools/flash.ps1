# ============================================================================
# flash.ps1 - Flash RA8P1 firmware via SEGGER J-Link Commander
#
# Usage (repo root):
#   pwsh -File tools\flash.ps1              # flash latest elf in build\Debug
#   pwsh -File tools\flash.ps1 -Elf <path>  # flash a specific elf
#
# J-Link.exe search order (first existing wins):
#   1. Bundled with Renesas VSCode extension (used on this machine, V7.96n):
#        C:\Users\Zhanglongsheng\.renesas\platform\DebugComp\Dialog\ARM\Segger\JLink.exe
#   2. Registry install dirs (D:\JLINK\JLink_V960 etc. - no longer present here)
#   3. JLink.exe on PATH
#
# Known harmless messages (ignore):
#   - "Bank 0 @ 0x02000000: Skipped. Contents already match"
#       = board already runs this exact firmware
#   - "Writing target memory failed"
#       = secondary ELF load segments (RAM/checksum) cannot be written;
#         present in every historical flash log, main code bank unaffected
# ============================================================================
param(
    [string]$Elf = ""
)

$ErrorActionPreference = "Stop"

# ---- 1. locate JLink.exe ----------------------------------------------------
$candidates = @(
    "C:\Users\Zhanglongsheng\.renesas\platform\DebugComp\Dialog\ARM\Segger\JLink.exe",
    "D:\JLINK\JLink_V960\JLink.exe",
    "D:\JLIINK\JLink_V830\JLink.exe",
    "C:\Program Files\SEGGER\JLink\JLink.exe",
    "C:\Program Files (x86)\SEGGER\JLink\JLink.exe"
)
$jlink = $null
foreach ($c in $candidates) {
    if (Test-Path $c) { $jlink = $c; break }
}
if (-not $jlink) {
    $jlink = (Get-Command JLink.exe -ErrorAction SilentlyContinue).Source
}
if (-not $jlink) {
    Write-Error "JLink.exe not found. Install SEGGER J-Link or add its path to the candidate list."
}

# ---- 2. locate elf ----------------------------------------------------------
if (-not $Elf) {
    $Elf = Join-Path $PSScriptRoot "..\build\Debug\26renesascup_smartmedicine_Ra8p1.elf"
}
$Elf = (Resolve-Path $Elf).Path
if (-not (Test-Path $Elf)) { Write-Error "ELF not found: $Elf" }

# ---- 3. generate temporary J-Link command script ----------------------------
$tmp = Join-Path $PSScriptRoot "..\.tmp"
New-Item -ItemType Directory -Force -Path $tmp | Out-Null
$cmd = @"
device R7KA8P1KF
si SWD
speed 1000
connect
r
loadfile $Elf
r
g
sleep 8000
q
"@
$script = Join-Path $tmp "flash_out43.jlink"
Set-Content -Path $script -Value $cmd -Encoding Ascii

Write-Host "==> J-Link : $jlink"
Write-Host "==> ELF    : $Elf"
Write-Host "==> Flashing (about 10 s)..."
& $jlink -CommanderScript $script
if ($LASTEXITCODE -eq 0) { Write-Host "==> J-Link finished (exit 0)" }
else { Write-Warning "==> J-Link exit $LASTEXITCODE (normal if main bank already matched)" }
