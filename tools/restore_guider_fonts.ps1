# ============================================================================
# restore_guider_fonts.ps1
# ----------------------------------------------------------------------------
# GUI Guider 每次"Generate Code"都会按 fontConfig.base_chars + 界面文本重新
# 收集字符并完整重写 generated/assets/fonts/lv_font_SourceHanSerifSC_*.c。
# 这些内嵌字体只是 OSPI tiny_ttf 动态字体（g_tiny_font）的兜底，若重新生成
# 后字形集膨胀（本会话实测单字号 bitmap 13KB -> 44KB，12 个字号合计导致
# 1MB 内部 Flash 溢出 ~47KB、链接失败），运行本脚本把字体恢复为仓库内的
# 精简版本即可，界面文字仍由 OSPI simhei 动态字体渲染，不受影响。
#
# 用法（在仓库根目录）：
#     powershell -ExecutionPolicy Bypass -File tools\restore_guider_fonts.ps1
# ============================================================================
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$fontsRel = "gui/RA8P1/generated/assets/fonts"
$fontsDir = Join-Path $root ($fontsRel -replace "/", "\")

if (-not (Test-Path $fontsDir)) {
    Write-Host "fonts dir not found: $fontsDir" -ForegroundColor Red
    exit 1
}

# 用 git 把字体恢复为仓库内的精简版本（HEAD）
git -C $root checkout HEAD -- $fontsRel
if ($LASTEXITCODE -ne 0) {
    Write-Host "git checkout failed" -ForegroundColor Red
    exit 1
}

# 报告恢复后的字体数据规模
$total = 0
Get-ChildItem $fontsDir -Filter "lv_font_SourceHanSerifSC_*.c" | ForEach-Object {
    $txt = Get-Content -LiteralPath $_.FullName -Raw -Encoding UTF8
    $m = [regex]::Match($txt, "glyph_bitmap\[\]\s*=\s*\{(.*?)\}", "Singleline")
    if ($m.Success) {
        $n = (($m.Groups[1].Value -split "," | Where-Object { $_.Trim() -ne "" }).Count)
        $total += $n
        Write-Host ("{0,-40} bitmap={1,7} bytes" -f $_.Name, $n)
    }
}
Write-Host ("------------------------------------------------------------")
Write-Host ("Total embedded font bitmap: {0} bytes (headroom OK)" -f $total)
Write-Host "Next: rebuild + flash (build\Debug: mingw32-make -j8)" -ForegroundColor Yellow
