param([switch]$Verbose)

$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$airlink = Split-Path -Parent $here
$ws = Split-Path -Parent $airlink
$bin = Join-Path $ws 'gongj\w64devkit\bin'
if (-not (Test-Path $bin)) { throw "toolchain not found: $bin" }
$env:PATH = "$bin;$env:PATH"

$build = Join-Path $here 'build'
$out   = Join-Path $here 'out'
New-Item -ItemType Directory -Force -Path $build, $out | Out-Null

Write-Host "[1/3] resources" -ForegroundColor Cyan
Push-Location $here
& windres 'res\app.rc' -O coff -o 'build\app_res.o' --include-dir 'res'
$rc = $LASTEXITCODE
Pop-Location
if ($rc -ne 0) { throw "windres failed" }

Write-Host "[2/3] compile" -ForegroundColor Cyan
$srcs = @('src\main.cpp', 'src\ui.cpp', 'src\net.cpp', 'src\util.cpp', 'src\qr.cpp', 'src\wan.cpp') | ForEach-Object { Join-Path $here $_ }
$args = @(
    '-std=c++17', '-O2', '-ffunction-sections', '-fdata-sections', '-fno-ident',
    '-municode', '-mwindows', '-s',
    '-Wall', '-Wextra', '-Wno-unused-parameter', '-Wno-cast-function-type',
    '-Wl,--gc-sections', '-Wl,--build-id=none',
    '-static', '-static-libgcc', '-static-libstdc++'
) + $srcs + @(
    (Join-Path $build 'app_res.o'),
    '-o', (Join-Path $out 'AirLink.exe'),
    '-lgdiplus', '-lws2_32', '-liphlpapi', '-ldwmapi', '-lshell32', '-lshlwapi',
    '-lcomdlg32', '-lole32', '-luuid', '-lcomctl32', '-luser32', '-lgdi32',
    '-lstdc++', '-lpthread'
)
& g++ @args
if ($LASTEXITCODE -ne 0) { throw "compile failed" }

$exe = Join-Path $out 'AirLink.exe'
$mb = [math]::Round((Get-Item $exe).Length / 1KB, 1)
Write-Host "[3/3] done  ->  $exe  ($mb KB)" -ForegroundColor Green
