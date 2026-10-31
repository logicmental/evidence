param(
    [string]$Version = "0.1.0",
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$projectPath = Join-Path $root "app\Evidence.Avalonia\Evidence.csproj"
$isccCommand = Get-Command "ISCC.exe" -ErrorAction SilentlyContinue
$isccPath = if ($isccCommand) { $isccCommand.Source }
if (-not $isccPath) {
    $standardIsccPath = Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6\ISCC.exe"
    if (Test-Path $standardIsccPath) {
        $isccPath = $standardIsccPath
    }
}
if (-not $isccPath) {
    throw "Inno Setup 6 was not found. Install it and make sure ISCC.exe is on PATH."
}
if (-not (Test-Path $projectPath)) {
    throw "Avalonia project not found: $projectPath"
}

$distDir = Join-Path $root "dist"
$payloadDir = Join-Path $distDir "payload"
New-Item -ItemType Directory -Force -Path $distDir | Out-Null
if (Test-Path $payloadDir) {
    Remove-Item -Recurse -Force $payloadDir
}
New-Item -ItemType Directory -Force -Path $payloadDir | Out-Null

& dotnet publish $projectPath --configuration $Configuration --runtime win-x64 --self-contained true `
    -p:PublishSingleFile=false --output $payloadDir
if ($LASTEXITCODE -ne 0) { throw "Avalonia application publish failed." }

$appExe = Join-Path $payloadDir "evidence.exe"
if (-not (Test-Path $appExe)) {
    throw "Application payload is missing: $appExe"
}

$pandocCandidates = @()
if (-not [string]::IsNullOrWhiteSpace($env:PANDOC_EXE)) {
    $pandocCandidates += $env:PANDOC_EXE
}
if (-not [string]::IsNullOrWhiteSpace($env:ProgramFiles)) {
    $pandocCandidates += Join-Path $env:ProgramFiles "Pandoc\pandoc.exe"
}
if (-not [string]::IsNullOrWhiteSpace($env:ChocolateyInstall)) {
    $pandocCandidates += Join-Path $env:ChocolateyInstall "lib\pandoc\tools\pandoc.exe"
}
$pandocExe = $pandocCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $pandocExe) {
    throw "Pandoc was not found. Install Pandoc or set PANDOC_EXE to pandoc.exe."
}

$libreOfficeCandidates = @()
if (-not [string]::IsNullOrWhiteSpace($env:LIBREOFFICE_ROOT)) {
    $libreOfficeCandidates += $env:LIBREOFFICE_ROOT
}
if (-not [string]::IsNullOrWhiteSpace($env:ProgramFiles)) {
    $libreOfficeCandidates += Join-Path $env:ProgramFiles "LibreOffice"
}
if (-not [string]::IsNullOrWhiteSpace(${env:ProgramFiles(x86)})) {
    $libreOfficeCandidates += Join-Path ${env:ProgramFiles(x86)} "LibreOffice"
}
if (-not [string]::IsNullOrWhiteSpace($env:ChocolateyInstall)) {
    $libreOfficeCandidates += Join-Path $env:ChocolateyInstall "lib\libreoffice-fresh\tools\LibreOffice"
}
$libreOfficeRoot = $libreOfficeCandidates |
    Where-Object { Test-Path (Join-Path $_ "program\soffice.exe") } |
    Select-Object -First 1
if (-not $libreOfficeRoot) {
    $sofficeCommand = Get-Command "soffice.exe" -ErrorAction SilentlyContinue
    if ($sofficeCommand -and (Test-Path $sofficeCommand.Source)) {
        $libreOfficeRoot = Split-Path (Split-Path $sofficeCommand.Source -Parent) -Parent
    }
}
if (-not $libreOfficeRoot -or -not (Test-Path (Join-Path $libreOfficeRoot "program\soffice.exe"))) {
    throw "LibreOffice was not found. Install LibreOffice or set LIBREOFFICE_ROOT."
}

$toolsDir = Join-Path $payloadDir "tools"
$bundledLibreOffice = Join-Path $toolsDir "LibreOffice"
New-Item -ItemType Directory -Force -Path $toolsDir | Out-Null
Copy-Item $pandocExe (Join-Path $toolsDir "pandoc.exe") -Force
New-Item -ItemType Directory -Force -Path $bundledLibreOffice | Out-Null
Copy-Item (Join-Path $libreOfficeRoot "*") $bundledLibreOffice -Recurse -Force

& $isccPath "/DAppVersion=$Version" (Join-Path $PSScriptRoot "evidence.iss")
if ($LASTEXITCODE -ne 0) { throw "Inno Setup compilation failed." }

Write-Host "Installer created in $distDir"