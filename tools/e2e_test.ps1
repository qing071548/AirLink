# E2E protocol test: pushes one file into the local AirLink receiver
# Note: the save directory is read from the app's own settings.ini so this
# script never has to guess (and never mangles) a non-ASCII path.
$ErrorActionPreference = 'Stop'
$port = 45678

$roaming = [Environment]::GetFolderPath('ApplicationData')
if (-not $roaming) { $roaming = Join-Path $env:USERPROFILE 'AppData\Roaming' }
$ini = Join-Path $roaming 'AirLink\settings.ini'
$saveRoot = $null
if (Test-Path -LiteralPath $ini) {
    foreach ($line in [System.IO.File]::ReadAllLines($ini, [System.Text.Encoding]::UTF8)) {
        if ($line -match '^savedir=(.*)$') { $saveRoot = $Matches[1].Trim(); break }
        if ($line -match '^port=(\d+)$') { $port = [int]$Matches[1] }
    }
}
if (-not $saveRoot) { $saveRoot = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'AirLink 接收' }

$payload = New-Object byte[] (2 * 1024 * 1024)
for ($i = 0; $i -lt $payload.Length; $i++) { $payload[$i] = ($i % 251) }
$name = "e2e-probe-$(Get-Random).bin"
$temp = Join-Path $env:TEMP $name
[System.IO.File]::WriteAllBytes($temp, $payload)

$client = New-Object System.Net.Sockets.TcpClient('127.0.0.1', $port)
$client.NoDelay = $true
$stream = $client.GetStream()
$bw = New-Object System.IO.BinaryWriter $stream

# header
$magic = [System.Text.Encoding]::ASCII.GetBytes('ALFT')
$bw.Write($magic)                      # 4 magic
$bw.Write([byte]1)                     # version
$bw.Write([byte]1)                     # type = batch
$bw.Write([uint32]1)                   # file count
$nb = [System.Text.Encoding]::UTF8.GetBytes($name)
$bw.Write([uint32]$nb.Length)
$bw.Write($nb)
$bw.Write([uint64]$payload.Length)
$bw.Flush()

$reply = $stream.ReadByte()
"offer reply byte = $reply"

$stream.Write($payload, 0, $payload.Length)
$stream.Flush()
Start-Sleep -Milliseconds 500
$client.Close()
Start-Sleep -Milliseconds 800

$dest = Join-Path $saveRoot $name
if (Test-Path -LiteralPath $dest) {
    $got = [System.IO.File]::ReadAllBytes($dest)
    if ($got.Length -eq $payload.Length -and ((Compare-Object $got $payload).Count -eq 0)) {
        Write-Output "RESULT: OK  file ($(($got.Length/1MB).ToString('0.0')) MB) received intact in $saveRoot"
        Remove-Item -LiteralPath $dest -Force
        "(probe file cleaned up)"
    } else {
        Write-Output "RESULT: MISMATCH size=$($got.Length)"
    }
} else {
    Write-Output "RESULT: FILE NOT FOUND at $dest"
}
Remove-Item -LiteralPath $temp -Force
