param(
    [string] $Path = (Join-Path $PSScriptRoot '..\configuration.xml')
)

$ErrorActionPreference = 'Stop'
$resolvedPath = (Resolve-Path -LiteralPath $Path).Path
$failures = [System.Collections.Generic.List[string]]::new()
$warnings = [System.Collections.Generic.List[string]]::new()

try
{
    [xml] $xml = [System.IO.File]::ReadAllText($resolvedPath, [System.Text.Encoding]::UTF8)
}
catch
{
    Write-Error "Cannot parse configuration XML: $($_.Exception.Message)"
    exit 2
}

function Get-GeneralOption([string] $key)
{
    $node = $xml.SelectSingleNode("/raConfiguration/generalSettings/option[@key='$key']")
    if ($null -eq $node) { return $null }
    return $node.GetAttribute('value')
}

function Get-PropertyValue([string] $id)
{
    $node = $xml.SelectSingleNode("//property[@id='$id']")
    if ($null -eq $node) { return $null }
    return $node.GetAttribute('value')
}

function Require-Equal([string] $label, $actual, $expected)
{
    if ($actual -ne $expected)
    {
        $failures.Add("${label}: expected '$expected', got '$actual'")
    }
}

function Require-PinSetting($pinCfg, [string] $configurationId, [string] $altId)
{
    $node = @($pinCfg.SelectNodes("./configSetting[@configurationId='$configurationId' and @altId='$altId']"))
    if ($node.Count -ne 1)
    {
        $failures.Add("Pin setting missing or duplicated: $configurationId => $altId")
    }
}

Require-Equal 'XML root' $xml.DocumentElement.Name 'raConfiguration'
Require-Equal 'Target device' (Get-GeneralOption '#TargetName#') 'R7KA8P1KFLCAC'
Require-Equal 'CPU' (Get-GeneralOption 'CPU') 'RA8P1'
Require-Equal 'FSP version' (Get-GeneralOption '#FSPVersion#') '6.3.0'

$activePinCfg = @($xml.SelectNodes('/raConfiguration/raPinConfiguration/pincfg[@active="true"]'))
if ($activePinCfg.Count -ne 1)
{
    $failures.Add("Expected exactly one active pincfg, got $($activePinCfg.Count)")
}
else
{
    Require-PinSetting $activePinCfg[0] 'p402' 'p402.canfd0.crx0'
    Require-PinSetting $activePinCfg[0] 'p704' 'p704.canfd0.ctx0'
    Require-PinSetting $activePinCfg[0] 'p814' 'p814.usbfs.usb_dp'
    Require-PinSetting $activePinCfg[0] 'p815' 'p815.usbfs.usb_dm'
}

Require-Equal 'MIPI DSI data lanes' (Get-PropertyValue 'module.driver.mipi_dsi.num_lanes') '1'
Require-Equal 'GLCDC horizontal pixels' (Get-PropertyValue 'module.driver.display.input0.hsize') '480'
Require-Equal 'GLCDC vertical pixels' (Get-PropertyValue 'module.driver.display.input0.vsize') '640'
Require-Equal 'GLCDC panel clock divider' (Get-PropertyValue 'module.driver.display.clock_div_ratio') 'module.driver.display.clock_div_ratio.panel_clk_divisor_6'
Require-Equal 'SDRAM support' (Get-PropertyValue 'config.bsp.fsp.sdram.enabled') 'config.bsp.fsp.sdram.enabled.enabled'
Require-Equal 'LVGL DAVE2D backend' (Get-PropertyValue 'config.lvgl.lvgl.lv_use_draw_dave2d') 'config.lvgl.lvgl.lv_use_draw_dave2d.disable'

$lvglDrwStack = $xml.SelectSingleNode("//stack[@requires='module.middleware.rm_lvgl_port.requires.dave2d_port']")
if ($null -ne $lvglDrwStack)
{
    $failures.Add('LVGL DRW stack must be absent while the DAVE2D draw backend is disabled')
}

$lcdClockDivider = $xml.SelectSingleNode('/raConfiguration/raClockConfiguration/node[@id="board.clock.lcdclk.div"]')
if ($null -eq $lcdClockDivider)
{
    $failures.Add('LCDCLK divider node is missing')
}
else
{
    Require-Equal 'LCDCLK divider' $lcdClockDivider.GetAttribute('option') 'board.clock.lcdclk.div.2'
}

$threadPriorities = @{}
foreach ($context in $xml.SelectNodes('/raConfiguration/raModuleConfiguration/context'))
{
    $symbolNode = $context.SelectSingleNode('./property[@id="_symbol"]')
    $priorityNode = $context.SelectSingleNode('./property[@id="rtos.awsfreertos.thread.priority"]')
    if (($null -ne $symbolNode) -and ($null -ne $priorityNode))
    {
        $threadPriorities[$symbolNode.GetAttribute('value')] = [int] $priorityNode.GetAttribute('value')
    }
}

foreach ($requiredThread in @('Motor_thread', 'Camera_thread', 'LVGL_thread'))
{
    if (-not $threadPriorities.ContainsKey($requiredThread))
    {
        $failures.Add("Required thread missing: $requiredThread")
    }
}

if ($threadPriorities.ContainsKey('Motor_thread') -and
    $threadPriorities.ContainsKey('Camera_thread') -and
    $threadPriorities.ContainsKey('LVGL_thread'))
{
    if (-not (($threadPriorities['Motor_thread'] -gt $threadPriorities['Camera_thread']) -and
              ($threadPriorities['Camera_thread'] -gt $threadPriorities['LVGL_thread'])))
    {
        $failures.Add('Thread priority invariant failed: Motor > Camera > LVGL')
    }
}

$moduleIds = @{}
foreach ($module in $xml.SelectNodes('/raConfiguration/raModuleConfiguration/module'))
{
    $moduleIds[$module.GetAttribute('id')] = $true
}
foreach ($stack in $xml.SelectNodes('/raConfiguration/raModuleConfiguration//stack'))
{
    $moduleId = $stack.GetAttribute('module')
    if (-not $moduleIds.ContainsKey($moduleId))
    {
        $failures.Add("Stack references missing module: $moduleId")
    }
}

foreach ($parent in $xml.SelectNodes('//*[property]'))
{
    $duplicates = @($parent.property) |
        Group-Object { $_.GetAttribute('id') } |
        Where-Object { $_.Count -gt 1 }
    foreach ($duplicate in $duplicates)
    {
        $failures.Add("Duplicate property '$($duplicate.Name)' in $($parent.Name)")
    }
}

Write-Host "RASC configuration audit: $resolvedPath"
Write-Host "Target: $(Get-GeneralOption '#TargetName#'); FSP: $(Get-GeneralOption '#FSPVersion#')"
if ($activePinCfg.Count -eq 1)
{
    Write-Host "Active pincfg: $($activePinCfg[0].GetAttribute('name'))"
}
Write-Host "Threads: Motor=$($threadPriorities['Motor_thread']), Camera=$($threadPriorities['Camera_thread']), LVGL=$($threadPriorities['LVGL_thread'])"
Write-Host "MIPI lanes: $(Get-PropertyValue 'module.driver.mipi_dsi.num_lanes')"
Write-Host "SDRAM support: $(Get-PropertyValue 'config.bsp.fsp.sdram.enabled')"

foreach ($warning in $warnings) { Write-Warning $warning }
if ($failures.Count -gt 0)
{
    foreach ($failure in $failures) { Write-Host "[FAIL] $failure" -ForegroundColor Red }
    Write-Host "Audit failed with $($failures.Count) issue(s)." -ForegroundColor Red
    exit 1
}

Write-Host 'Audit passed.' -ForegroundColor Green
exit 0
