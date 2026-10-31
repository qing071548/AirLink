Add-Type -AssemblyName System.Drawing

$outDir = Join-Path $PSScriptRoot 'res'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$icoPath = Join-Path $outDir 'app.ico'

function New-Layer([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)

    $pad = [single]($size * 0.02)
    $r = [System.Drawing.RectangleF]::new($pad, $pad, [single]($size - 2 * $pad), [single]($size - 2 * $pad))
    $rad = [single]($size * 0.235)

    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = [single]($rad * 2)
    $path.AddArc($r.X, $r.Y, $d, $d, 180, 90)
    $path.AddArc($r.Right - $d, $r.Y, $d, $d, 270, 90)
    $path.AddArc($r.Right - $d, $r.Bottom - $d, $d, $d, 0, 90)
    $path.AddArc($r.X, $r.Bottom - $d, $d, $d, 90, 90)
    $path.CloseFigure()

    $c1 = [System.Drawing.Color]::FromArgb(255, 92, 144, 255)
    $c2 = [System.Drawing.Color]::FromArgb(255, 132, 88, 255)
    $br = [System.Drawing.Drawing2D.LinearGradientBrush]::new($r, $c1, $c2, [single]45)
    $g.FillPath($br, $path)

    $hl = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
        $r, [System.Drawing.Color]::FromArgb(45, 255, 255, 255), [System.Drawing.Color]::FromArgb(0, 255, 255, 255), [single]90)
    $g.SetClip($path)
    $g.FillRectangle($hl, $r.X, $r.Y, $r.Width, [single]($r.Height * 0.55))
    $g.ResetClip()

    $s = [single]($size / 24.0)
    $pts = @(
        [System.Drawing.PointF]::new([single](5.2 * $s), [single](10.7 * $s)),
        [System.Drawing.PointF]::new([single](20.2 * $s), [single](3.6 * $s)),
        [System.Drawing.PointF]::new([single](14.5 * $s), [single](20.4 * $s)),
        [System.Drawing.PointF]::new([single](11.9 * $s), [single](13.3 * $s))
    )
    $wb = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::White)
    $g.FillPolygon($wb, [System.Drawing.PointF[]]$pts)
    $penW = [single]([Math]::Max(1.0, 1.6 * $s))
    $pen = New-Object System.Drawing.Pen([System.Drawing.Color]::White, $penW)
    $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $g.DrawLine($pen, [single](11.9 * $s), [single](13.3 * $s), [single](20.2 * $s), [single](3.6 * $s))

    $g.Dispose()
    return $bmp
}

$sizes = @(16, 24, 32, 48, 64, 128, 256)
$pngs = @()
foreach ($s in $sizes) {
    $bmp = New-Layer $s
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $pngs += , ($ms.ToArray())
    $bmp.Dispose()
    $ms.Dispose()
}

$fs = [System.IO.File]::Create($icoPath)
$bw = New-Object System.IO.BinaryWriter($fs)
$bw.Write([UInt16]0)
$bw.Write([UInt16]1)
$bw.Write([UInt16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $s = $sizes[$i]
    $dim = 0
    if ($s -lt 256) { $dim = $s }
    $bw.Write([Byte]$dim)
    $bw.Write([Byte]$dim)
    $bw.Write([Byte]0)
    $bw.Write([Byte]0)
    $bw.Write([UInt16]1)
    $bw.Write([UInt16]32)
    $bw.Write([UInt32]$pngs[$i].Length)
    $bw.Write([UInt32]$offset)
    $offset += $pngs[$i].Length
}
foreach ($p in $pngs) { $bw.Write($p) }
$bw.Close()
$fs.Close()
Write-Output ("icon written: " + $icoPath + "  (" + (Get-Item $icoPath).Length + " bytes)")
