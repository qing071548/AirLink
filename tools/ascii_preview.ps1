param(
    [Parameter(Mandatory = $true)][string]$Path,
    [int]$Cols = 120
)
Add-Type -AssemblyName System.Drawing

$bmp = [System.Drawing.Bitmap]::FromFile($Path)
$w = $bmp.Width; $h = $bmp.Height
$rows = [Math]::Max(1, [int]($Cols * $h / $w / 2.1))

$rect = [System.Drawing.Rectangle]::new(0, 0, $w, $h)
$data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$stride = $data.Stride
$bytes = New-Object byte[] ($stride * $h)
[System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
$bmp.UnlockBits($data)

$chars = ' .:-=+*#%@'
$sb = New-Object System.Text.StringBuilder
for ($r = 0; $r -lt $rows; $r++) {
    $y0 = [int]($r * $h / $rows); $y1 = [int](($r + 1) * $h / $rows)
    for ($c = 0; $c -lt $Cols; $c++) {
        $x0 = [int]($c * $w / $Cols); $x1 = [int](($c + 1) * $w / $Cols)
        $maxv = 0
        for ($sy = $y0; $sy -lt $y1; $sy += [Math]::Max(1, [int](($y1 - $y0) / 3))) {
            for ($sx = $x0; $sx -lt $x1; $sx += [Math]::Max(1, [int](($x1 - $x0) / 3))) {
                $i = $sy * $stride + $sx * 4
                $l = [int](0.299 * $bytes[$i + 2] + 0.587 * $bytes[$i + 1] + 0.114 * $bytes[$i])
                if ($l -gt $maxv) { $maxv = $l }
            }
        }
        $idx = [int]($maxv / 26)
        if ($idx -gt 9) { $idx = 9 }
        [void]$sb.Append($chars[$idx])
    }
    [void]$sb.AppendLine()
}
$bmp.Dispose()
Write-Output ("image: {0}x{1}  grid: {2}x{3}" -f $w, $h, $Cols, $rows)
Write-Output $sb.ToString()
