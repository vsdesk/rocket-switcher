# Pack the Release GUI: zip for a direct run, then an Inno Setup installer.
# Version comes from the first line of packaging/deb/changelog.
$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $Root

$changelog = Get-Content -Path "packaging/deb/changelog" -TotalCount 1
if ($changelog -notmatch '\(([^)]+)\)') {
    throw "Cannot read version from packaging/deb/changelog"
}
$version = $Matches[1]

$exe = Join-Path $Root "build/src/gui/Release/rocket-switcher-config.exe"
if (-not (Test-Path $exe)) {
    throw "Missing $exe — build the Release configuration first"
}
if (-not $env:QT_ROOT_DIR) {
    throw "QT_ROOT_DIR is not set"
}
$windeploy = Join-Path $env:QT_ROOT_DIR "bin/windeployqt.exe"
if (-not (Test-Path $windeploy)) {
    throw "Missing $windeploy"
}

$stage = Join-Path $Root "dist/windows/stage"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force -Path (Join-Path $stage "dict") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $stage "sounds") | Out-Null
Copy-Item $exe $stage
Copy-Item (Join-Path $Root "data/dict/en.txt"), (Join-Path $Root "data/dict/ru.txt") (Join-Path $stage "dict")
Copy-Item (Join-Path $Root "data/sounds/switch.wav") (Join-Path $stage "sounds")

& $windeploy --release --compiler-runtime (Join-Path $stage "rocket-switcher-config.exe")
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE" }

$dist = Join-Path $Root "dist"
New-Item -ItemType Directory -Force -Path $dist | Out-Null
$zip = Join-Path $dist "rocket-switcher_${version}_windows-x64.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory($stage, $zip)

$iscc = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
if (-not (Test-Path $iscc)) {
    $iscc = "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
}
if (-not (Test-Path $iscc)) { throw "Inno Setup 6 (ISCC.exe) not found" }

$issSrc = Join-Path $Root "packaging/windows/rocket-switcher.iss"
$issBom = Join-Path $Root "packaging/windows/rocket-switcher.bom.iss"
$utf8bom = New-Object System.Text.UTF8Encoding $true
[System.IO.File]::WriteAllText($issBom, [System.IO.File]::ReadAllText($issSrc), $utf8bom)
try {
    & $iscc "/DAppVersion=$version" $issBom
    if ($LASTEXITCODE -ne 0) { throw "ISCC failed with exit code $LASTEXITCODE" }
} finally {
    if (Test-Path $issBom) { Remove-Item -Force $issBom }
}

Write-Host "Wrote $zip"
Get-ChildItem $dist -Filter "rocket-switcher_${version}_windows-x64.*"
