# Drive the 设备文件 page end to end: enter a folder, then click 复制 on a file.
param(
    [string]$Ip = '172.16.215.53',
    [int]$Port = 45678,
    [string]$PeerArgs = 'TestPhone'
)
$ErrorActionPreference = 'Stop'

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Ui2 {
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr c);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x,int y,int cx,int cy, uint f);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left,Top,Right,Bottom; }
}
"@
[void][Ui2]::SetProcessDpiAwarenessContext([IntPtr](-4))

$WM_MOUSEMOVE = 0x0200; $WM_LBUTTONDOWN = 0x0201; $WM_LBUTTONUP = 0x0202
$k = 1.75
function L2P([double]$v) { return [int][math]::Round($v * $k) }

function Click-At([IntPtr]$h, [int]$cx, [int]$cy) {
    $lp = [IntPtr](($cy -shl 16) -bor ($cx -band 0xFFFF))
    [void][Ui2]::SendMessage($h, $WM_MOUSEMOVE, [IntPtr]::Zero, $lp)
    Start-Sleep -Milliseconds 130
    [void][Ui2]::SendMessage($h, $WM_LBUTTONDOWN, [IntPtr]1, $lp)
    Start-Sleep -Milliseconds 90
    [void][Ui2]::SendMessage($h, $WM_LBUTTONUP, [IntPtr]0, $lp)
    Start-Sleep -Milliseconds 500
}

Add-Type -AssemblyName System.Drawing
function Grab([IntPtr]$h, [string]$path) {
    $rr = New-Object Ui2+RECT; [void][Ui2]::GetWindowRect($h, [ref]$rr)
    $w = $rr.Right - $rr.Left; $hh = $rr.Bottom - $rr.Top
    $bmp = New-Object System.Drawing.Bitmap $w, $hh
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($rr.Left, $rr.Top, 0, 0, (New-Object System.Drawing.Size $w, $hh))
    $g.Dispose(); $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
}

$exe = 'E:\AZ\deepseek H\gongzuoqu\AirLink\windows\out\AirLink.exe'
$proc = Start-Process -FilePath $exe -ArgumentList @('--fakepeer', $PeerArgs, $Ip, "$Port") -PassThru
Start-Sleep -Seconds 3
$h = (Get-Process -Id $proc.Id).MainWindowHandle
if ($h -eq [IntPtr]::Zero) { throw "no window" }
[void][Ui2]::SetWindowPos($h, [IntPtr]::Zero, 40, 40, 1855, 1309, 0x0040)
[void][Ui2]::SetForegroundWindow($h)
Start-Sleep -Milliseconds 700

# 1) open 设备文件 (id 340, logical 460,274 104x34 -> centre 512,291)
Click-At $h (L2P 512) (L2P 291)
Start-Sleep -Milliseconds 1200
Grab $h "$env:TEMP\fsflow\s1-roots.png"
Write-Output "step1: opened browser"

# 2) click the first row (a drive folder). rows start at y=170 logical, 46 tall
Click-At $h (L2P 400) (L2P 193)
Start-Sleep -Milliseconds 1500
Grab $h "$env:TEMP\fsflow\s2-inside.png"
Write-Output "step2: entered folder"
Get-Content "$env:TEMP\fsflow\br3.txt" -Encoding UTF8 -ErrorAction SilentlyContinue

Write-Output "pid=$($proc.Id)"
