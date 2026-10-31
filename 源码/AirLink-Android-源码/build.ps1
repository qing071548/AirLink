param(
    [switch]$Release
)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot                                  # ...\AirLink\android
$ws = Split-Path -Parent (Split-Path -Parent $here)    # workspace root
$sdk = Join-Path $ws 'gongj\android\sdk'
$bt = Join-Path $sdk 'build-tools\35.0.0'
$platform = Join-Path $sdk 'platforms\android-35\android.jar'
$env:JAVA_HOME = 'D:\ruanjianAZ\jdk24'
$javaBin = Join-Path $env:JAVA_HOME 'bin'

foreach ($p in $bt, $platform, (Join-Path $javaBin 'javac.exe')) {
    if (-not (Test-Path $p)) { throw "missing: $p" }
}
$env:PATH = "$javaBin;$bt;$env:PATH"

$build = Join-Path $here 'build'
$out = Join-Path $here 'out'
$gen = Join-Path $build 'gen'
$classes = Join-Path $build 'classes'
$dexDir = Join-Path $build 'dex'
Remove-Item $build -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $build, $out, $gen, $classes, $dexDir | Out-Null

Push-Location $here
try {
    # ---------------------------------------------------------------- 1. resources
    Write-Host "[1/6] aapt2 compile" -ForegroundColor Cyan
    $resZip = Join-Path $build 'res.zip'
    & aapt2 compile --dir res -o $resZip
    if ($LASTEXITCODE -ne 0) { throw "aapt2 compile failed" }

    Write-Host "[2/6] aapt2 link" -ForegroundColor Cyan
    $baseApk = Join-Path $build 'base.apk'
    & aapt2 link -o $baseApk `
        -I $platform `
        --manifest AndroidManifest.xml `
        --java $gen `
        --min-sdk-version 21 `
        --target-sdk-version 35 `
        --version-code 1 `
        --version-name 1.0 `
        $resZip
    if ($LASTEXITCODE -ne 0) { throw "aapt2 link failed" }

    # ---------------------------------------------------------------- 2. java
    Write-Host "[3/6] javac" -ForegroundColor Cyan
    $sources = Get-ChildItem -Path (Join-Path $here 'src'), $gen -Recurse -Filter *.java |
        ForEach-Object { $_.FullName }
    $libs = (Get-ChildItem -Path (Join-Path $here 'libs') -Filter *.jar |
        ForEach-Object { $_.FullName }) -join ';'

    $javacArgs = @(
        '-source', '8', '-target', '8',
        '-encoding', 'UTF-8',
        '-classpath', "$platform;$libs",
        '-d', $classes,
        '-nowarn', '-Xlint:none'
    ) + $sources
    & javac @javacArgs
    if ($LASTEXITCODE -ne 0) { throw "javac failed" }

    # ---------------------------------------------------------------- 3. dex
    Write-Host "[4/6] d8" -ForegroundColor Cyan
    $classFiles = (Get-ChildItem -Path $classes -Recurse -Filter *.class |
        ForEach-Object { $_.FullName })
    $jarList = Get-ChildItem -Path (Join-Path $here 'libs') -Filter *.jar |
        ForEach-Object { $_.FullName }
    $d8Args = @(
        '--release',
        '--min-api', '21',
        '--lib', $platform,
        '--output', $dexDir
    ) + $classFiles + $jarList
    & d8 @d8Args
    if ($LASTEXITCODE -ne 0) { throw "d8 failed" }

    # ---------------------------------------------------------------- 4. package
    Write-Host "[5/6] package" -ForegroundColor Cyan
    $unaligned = Join-Path $build 'unaligned.apk'
    Copy-Item $baseApk $unaligned
    Push-Location $dexDir
    try {
        & "$bt\aapt.exe" add -f $unaligned "classes.dex"
        if ($LASTEXITCODE -ne 0) { throw "aapt add dex failed" }
    } finally { Pop-Location }

    $aligned = Join-Path $build 'aligned.apk'
    & zipalign -f -p 4 $unaligned $aligned
    if ($LASTEXITCODE -ne 0) { throw "zipalign failed" }

    # ---------------------------------------------------------------- 5. sign
    Write-Host "[6/6] sign" -ForegroundColor Cyan
    $ks = Join-Path $build 'airlink.jks'
    if (-not (Test-Path $ks)) {
        # keytool writes its progress to stderr; that is not a failure here.
        $prevEap = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        & keytool -genkeypair -keystore $ks -alias airlink `
            -keyalg RSA -keysize 2048 -validity 10950 `
            -storepass airlink123 -keypass airlink123 `
            -dname "CN=AirLink, OU=AirLink, O=AirLink, L=NA, ST=NA, C=CN" 2>$null
        $rc = $LASTEXITCODE
        $ErrorActionPreference = $prevEap
        if ($rc -ne 0 -or -not (Test-Path $ks)) { throw "keystore creation failed" }
    }

    $final = Join-Path $out 'AirLink.apk'
    if (Test-Path $final) { Remove-Item $final -Force }
    & apksigner sign --ks $ks --ks-pass pass:airlink123 --key-pass pass:airlink123 `
        --ks-key-alias airlink --out $final $aligned
    if ($LASTEXITCODE -ne 0) { throw "apksigner failed" }

    & apksigner verify --print-certs $final | Select-Object -First 3

    $kb = [math]::Round((Get-Item $final).Length / 1KB, 1)
    Write-Host ""
    Write-Host "done -> $final ($kb KB)" -ForegroundColor Green
} finally {
    Pop-Location
}
