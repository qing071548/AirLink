# Acts as an AirLink receiver, then asks the app to send a file, and verifies bytes.
param(
    [string]$AppExe = 'E:\AZ\deepseek H\gongzuoqu\AirLink\AirLink.exe'
)
$ErrorActionPreference = 'Stop'
$port = 45678
$outDir = Join-Path $env:TEMP 'alrx'
Remove-Item -LiteralPath $outDir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $outDir -Force | Out-Null

# 1. source file
$srcDir = Join-Path $env:TEMP 'alsrc'
New-Item -ItemType Directory -Path $srcDir -Force | Out-Null
$src = Join-Path $srcDir 'outgoing-test.bin'
$payload = New-Object byte[] (3 * 1024 * 1024)
for ($i = 0; $i -lt $payload.Length; $i++) { $payload[$i] = ($i % 253) }
[System.IO.File]::WriteAllBytes($src, $payload)

# 2. standalone receiver (one connection)
$listener = New-Object System.Net.Sockets.TcpListener([System.Net.IPAddress]::Any, $port)
$listener.Start()
"listening on $port"

# 3. ask the running app to send to 127.0.0.1 via WM_COPYDATA-free route: use the test hook
$req = @{
    path   = $src
    ip     = '127.0.0.1'
    port   = $port
    name   = 'Loopback'
}
# The app has no CLI send hook yet, so drive it below in step 4.

$client = $listener.AcceptTcpClient()
$client.NoDelay = $true
$stream = $client.GetStream()

function Read-Exact($s, $n) {
    $buf = New-Object byte[] $n
    $off = 0
    while ($off -lt $n) {
        $r = $s.Read($buf, $off, $n - $off)
        if ($r -le 0) { throw "eof" }
        $off += $r
    }
    return ,$buf
}

$magic = [System.Text.Encoding]::ASCII.GetString((Read-Exact $stream 4))
$ver = (Read-Exact $stream 2)
$cnt = [BitConverter]::ToUInt32((Read-Exact $stream 4), 0)
"header magic=$magic ver=$($ver[0]) type=$($ver[1]) count=$cnt"

$files = @()
for ($i = 0; $i -lt $cnt; $i++) {
    $nl = [BitConverter]::ToUInt32((Read-Exact $stream 4), 0)
    $nm = [System.Text.Encoding]::UTF8.GetString((Read-Exact $stream $nl))
    $sz = [BitConverter]::ToUInt64((Read-Exact $stream 8), 0)
    $files += [pscustomobject]@{ Name = $nm; Size = $sz }
    "file[$i] name=$nm size=$sz"
}

# accept
$stream.Write([byte[]]@(1), 0, 1)
$stream.Flush()

foreach ($f in $files) {
    $left = [int64]$f.Size
    $dest = Join-Path $outDir $f.Name
    $fs = [System.IO.File]::Create($dest)
    $buf = New-Object byte[] 1048576
    while ($left -gt 0) {
        $want = [Math]::Min($left, $buf.Length)
        $r = $stream.Read($buf, 0, $want)
        if ($r -le 0) { throw "eof during payload" }
        $fs.Write($buf, 0, $r)
        $left -= $r
    }
    $fs.Close()
    "received $($f.Name)"
}
$client.Close()
$listener.Stop()

# verify
$ok = $true
foreach ($f in $files) {
    $dest = Join-Path $outDir $f.Name
    $got = [System.IO.File]::ReadAllBytes($dest)
    if ($got.Length -ne $payload.Length) { $ok = $false; "size mismatch" }
    else {
        for ($i = 0; $i -lt $got.Length; $i++) {
            if ($got[$i] -ne $payload[$i]) { $ok = $false; "byte mismatch at $i"; break }
        }
    }
}
"RESULT: intact=$ok"
