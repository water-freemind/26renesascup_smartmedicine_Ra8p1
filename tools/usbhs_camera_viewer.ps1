param(
    [string] $Port = "COM15",
    [int] $BaudRate = 115200
)

$ErrorActionPreference = "Stop"
trap {
    Add-Type -AssemblyName System.Windows.Forms -ErrorAction SilentlyContinue
    [System.Windows.Forms.MessageBox]::Show($_.Exception.ToString(), "USBHS Viewer Error") | Out-Null
    exit 1
}

# Windows PowerShell 5 may parse a UTF-8 script without BOM as an ANSI file.
# Build visible Chinese text from Unicode code points so the window title stays
# correct regardless of the script's source-file encoding.
function Get-UnicodeText([int[]] $CodePoints) {
    return -join ($CodePoints | ForEach-Object { [char] $_ })
}
$PreviewText = Get-UnicodeText @(0x9884, 0x89C8)
$FpsText = Get-UnicodeText @(0x5E27, 0x7387)
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies ([System.Drawing.Bitmap].Assembly.Location) -TypeDefinition @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class UsbHsImage {
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
          bgr[dst] = (byte)((pixel & 0x1F) * 255 / 31);
          bgr[dst + 1] = (byte)(((pixel >> 5) & 0x3F) * 255 / 63);
          bgr[dst + 2] = (byte)(((pixel >> 11) & 0x1F) * 255 / 31);
        }
      }
      Marshal.Copy(bgr, 0, data.Scan0, bgr.Length);
    } finally { bitmap.UnlockBits(data); }
    return bitmap;
  }
}
"@

$serial = [System.IO.Ports.SerialPort]::new($Port, $BaudRate, 'None', 8, 'One')
$serial.ReadTimeout = 20
$serial.DtrEnable = $true
$serial.RtsEnable = $true
$serial.Open()
$serial.DiscardInBuffer()

$form = [System.Windows.Forms.Form]::new()
$form.Text = "RA8P1 OV7725 USBHS $PreviewText - 0 FPS"
$form.ClientSize = [System.Drawing.Size]::new(800, 600)
$form.StartPosition = "CenterScreen"
$picture = [System.Windows.Forms.PictureBox]::new()
$picture.Dock = 'Fill'
$picture.SizeMode = 'Zoom'
$picture.BackColor = [System.Drawing.Color]::Black
$form.Controls.Add($picture)
$form.Show()
$form.Activate()

$buffer = [System.Collections.Generic.List[byte]]::new()
$frames = 0
$lastStat = [Environment]::TickCount
try {
    while ($form.Visible) {
        $available = $serial.BytesToRead
        if ($available -gt 0) {
            $incoming = New-Object byte[] $available
            [void]$serial.Read($incoming, 0, $incoming.Length)
            $buffer.AddRange($incoming)
        }

        while ($buffer.Count -ge 8) {
            $start = -1
            for ($i = 0; $i -lt ($buffer.Count - 1); $i++) {
                if (($buffer[$i] -eq 0x55) -and ($buffer[$i + 1] -eq 0xAA)) { $start = $i; break }
            }
            if ($start -lt 0) {
                $last = $buffer[$buffer.Count - 1]
                $buffer.Clear(); $buffer.Add($last)
                break
            }
            if ($start -gt 0) { $buffer.RemoveRange(0, $start) }
            if ($buffer.Count -lt 8) { break }

            $w = ($buffer[2] -shl 8) -bor $buffer[3]
            $h = ($buffer[4] -shl 8) -bor $buffer[5]
            $size = $w * $h * 2
            if (($w -le 0) -or ($h -le 0) -or ($w -gt 2048) -or ($h -gt 2048)) {
                $buffer.RemoveAt(0); continue
            }
            if ($buffer.Count -lt (8 + $size)) { break }

            $pixels = New-Object byte[] $size
            $buffer.CopyTo(8, $pixels, 0, $size)
            $buffer.RemoveRange(0, 8 + $size)
            $bitmap = [UsbHsImage]::FromRgb565($pixels, $w, $h)
            $old = $picture.Image; $picture.Image = $bitmap
            if ($null -ne $old) { $old.Dispose() }
            $frames++
        }

        $now = [Environment]::TickCount
        if (($now - $lastStat) -ge 1000) {
            $form.Text = "RA8P1 OV7725 USBHS $PreviewText - $frames $FpsText"
            $frames = 0; $lastStat = $now
        }
        [System.Windows.Forms.Application]::DoEvents()
        Start-Sleep -Milliseconds 1
    }
}
finally {
    if ($null -ne $picture.Image) { $picture.Image.Dispose() }
    if ($serial.IsOpen) { $serial.Close() }
}
