param(
    [string] $JLinkDll = "C:\Users\Zhanglongsheng\.renesas\platform\DebugComp\Dialog\ARM\Segger\JLink_x64.dll",
    [string] $Device = "R7KA8P1KF",
    [int] $SpeedKHz = 4000,
    [UInt32] $RttAddress = 0x220002C0,
    [UInt32] $FrameAddress = 0x22041D80
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $JLinkDll)) {
    throw "找不到 J-Link DLL：$JLinkDll"
}

$dllForCSharp = $JLinkDll.Replace("\", "\\")
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class RttJLink {
  const string D = "$dllForCSharp";
  [DllImport(D, CallingConvention=CallingConvention.Cdecl)] public static extern int JLINKARM_Open();
  [DllImport(D, CallingConvention=CallingConvention.Cdecl)] public static extern int JLINKARM_Close();
  [DllImport(D, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Ansi)] public static extern int JLINKARM_ExecCommand(string command, IntPtr output, int size);
  [DllImport(D, CallingConvention=CallingConvention.Cdecl)] public static extern int JLINKARM_TIF_Select(int tif);
  [DllImport(D, CallingConvention=CallingConvention.Cdecl)] public static extern void JLINKARM_SetSpeed(int khz);
  [DllImport(D, CallingConvention=CallingConvention.Cdecl)] public static extern int JLINKARM_Connect();
  [DllImport(D, CallingConvention=CallingConvention.Cdecl)] public static extern int JLINKARM_ReadMem(uint address, uint size, byte[] data);
  [DllImport(D, CallingConvention=CallingConvention.Cdecl)] public static extern int JLINK_RTTERMINAL_Control(int command, IntPtr data);
  [DllImport(D, CallingConvention=CallingConvention.Cdecl)] public static extern int JLINK_RTTERMINAL_Read(int bufferIndex, byte[] data, int size);
}
"@

function Invoke-JLink([string] $Name, [int] $Result) {
    if ($Result -lt 0) { throw "J-Link 操作失败：$Name ($Result)" }
}

Invoke-JLink "打开探针" ([RttJLink]::JLINKARM_Open())
try {
    Invoke-JLink "选择芯片" ([RttJLink]::JLINKARM_ExecCommand("device=$Device", [IntPtr]::Zero, 0))
    Invoke-JLink "选择 SWD" ([RttJLink]::JLINKARM_TIF_Select(1))
    [RttJLink]::JLINKARM_SetSpeed($SpeedKHz)
    Invoke-JLink "连接目标" ([RttJLink]::JLINKARM_Connect())
    Invoke-JLink "设置 RTT 地址" ([RttJLink]::JLINKARM_ExecCommand(("SetRTTAddr 0x{0:X8}" -f $RttAddress), [IntPtr]::Zero, 0))
    Invoke-JLink "启动 RTT" ([RttJLink]::JLINK_RTTERMINAL_Control(0, [IntPtr]::Zero))

    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    Add-Type -ReferencedAssemblies ([System.Drawing.Bitmap].Assembly.Location) -TypeDefinition @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class RttImage {
  public static Bitmap FromRgb565(byte[] source, int width, int height) {
    Bitmap bitmap = new Bitmap(width, height, PixelFormat.Format24bppRgb);
    Rectangle rect = new Rectangle(0, 0, width, height);
    BitmapData data = bitmap.LockBits(rect, ImageLockMode.WriteOnly, bitmap.PixelFormat);
    try {
      byte[] bgr = new byte[data.Stride * height];
      for (int y = 0; y < height; y++) {
        int srcRow = y * width * 2;
        int dstRow = y * data.Stride;
        for (int x = 0; x < width; x++) {
          int pixel = source[srcRow + x * 2] | (source[srcRow + x * 2 + 1] << 8);
          int dst = dstRow + x * 3;
          bgr[dst]     = (byte)((pixel & 0x1F) * 255 / 31);
          bgr[dst + 1] = (byte)(((pixel >> 5) & 0x3F) * 255 / 63);
          bgr[dst + 2] = (byte)(((pixel >> 11) & 0x1F) * 255 / 31);
        }
      }
      Marshal.Copy(bgr, 0, data.Scan0, bgr.Length);
    } finally {
      bitmap.UnlockBits(data);
    }
    return bitmap;
  }
}
"@

    $form = New-Object System.Windows.Forms.Form
    $form.Text = "RA8P1 OV7725 RTT Camera Preview"
    $form.ClientSize = New-Object System.Drawing.Size(640, 500)
    $form.StartPosition = "CenterScreen"
    $form.TopMost = $true
    $form.ShowInTaskbar = $true

    $picture = New-Object System.Windows.Forms.PictureBox
    $picture.Dock = "Fill"
    $picture.SizeMode = "Zoom"
    $picture.BackColor = [System.Drawing.Color]::Black
    $form.Controls.Add($picture)
    $form.Show()
    $form.Activate()

    $width = 320
    $height = 240
    $pixels = New-Object byte[] ($width * $height * 2)
    $frames = 0
    $lastStat = [Environment]::TickCount

    while ($form.Visible) {
        $readOk = $true
        $partSize = 38400
        for ($offset = 0; $offset -lt $pixels.Length; $offset += $partSize) {
            $part = [Math]::Min($partSize, $pixels.Length - $offset)
            $piece = New-Object byte[] $part
            if (0 -ne [RttJLink]::JLINKARM_ReadMem($FrameAddress + [UInt32]$offset, [UInt32]$part, $piece)) {
                $readOk = $false
                break
            }
            [Array]::Copy($piece, 0, $pixels, $offset, $part)
        }
        if ($readOk) {
            $bitmap = [RttImage]::FromRgb565($pixels, $width, $height)
            $old = $picture.Image
            $picture.Image = $bitmap
            if ($null -ne $old) { $old.Dispose() }
            $frames++
        }

        $now = [Environment]::TickCount
        if (($now - $lastStat) -ge 1000) {
            $form.Text = "RA8P1 OV7725 RTT Camera Preview - $frames FPS"
            $frames = 0
            $lastStat = $now
        }
        [System.Windows.Forms.Application]::DoEvents()
        Start-Sleep -Milliseconds 30
    }
}
finally {
    [RttJLink]::JLINKARM_Close() | Out-Null
}
