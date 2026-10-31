# Capture two frames of the running AirLink window, DPI-aware.
# Usage: powershell -File live_frames.ps1 <outDir> <msBetween>
param(
    [string]$OutDir = "$env:TEMP\alv",
    [int]$GapMs = 450
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class DpiNative {
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr ctx);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

# -4 = DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
[void][DpiNative]::SetProcessDpiAwarenessContext([IntPtr](-4))

Add-Type -AssemblyName System.Drawing

$proc = Get-Process AirLink -ErrorAction Stop | Select-Object -First 1
$h = $proc.MainWindowHandle
if ($h -eq [IntPtr]::Zero) { throw "AirLink has no main window" }

[void][DpiNative]::ShowWindow($h, 9)      # SW_RESTORE
[void][DpiNative]::SetForegroundWindow($h)
Start-Sleep -Milliseconds 700

$r = New-Object DpiNative+RECT
[void][DpiNative]::GetWindowRect($h, [ref]$r)
$w = $r.Right - $r.Left
$hh = $r.Bottom - $r.Top
Write-Output "window rect: $($r.Left),$($r.Top) size ${w}x${hh}"

New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

function Grab([string]$path) {
    $bmp = New-Object System.Drawing.Bitmap $w, $hh
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.Left, $r.Top, 0, 0, (New-Object System.Drawing.Size $w, $hh))
    $g.Dispose()
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}

$f1 = Join-Path $OutDir 'live1.png'
$f2 = Join-Path $OutDir 'live2.png'
Grab $f1
Start-Sleep -Milliseconds $GapMs
Grab $f2
Write-Output "saved $f1"
Write-Output "saved $f2"

$a = [System.Drawing.Bitmap]::FromFile($f1)
$b = [System.Drawing.Bitmap]::FromFile($f2)
$minX = 999999; $maxX = -1; $minY = 999999; $maxY = -1; $n = 0
for ($y = 0; $y -lt $a.Height; $y++) {
    for ($x = 0; $x -lt $a.Width; $x++) {
        $p1 = $a.GetPixel($x, $y); $p2 = $b.GetPixel($x, $y)
        if ([math]::Abs($p1.R - $p2.R) -gt 8 -or [math]::Abs($p1.G - $p2.G) -gt 8 -or [math]::Abs($p1.B - $p2.B) -gt 8) {
            $n++
            if ($x -lt $minX) { $minX = $x }; if ($x -gt $maxX) { $maxX = $x }
            if ($y -lt $minY) { $minY = $y }; if ($y -gt $maxY) { $maxY = $y }
        }
    }
}
Write-Output "changed pixels: $n"
if ($n -gt 0) { Write-Output "changed bbox: x $minX..$maxX  y $minY..$maxY" }
$a.Dispose(); $b.Dispose()
