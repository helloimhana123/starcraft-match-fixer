<#
.SYNOPSIS
    Builds MatchFixer.dll and packages it with MatchFixer.ini into MatchFixer.zip.

.DESCRIPTION
    Configures and builds the 32-bit StarCraft plugin with CMake into ./build,
    then creates a release archive named MatchFixer.zip that contains
    MatchFixer.dll and MatchFixer.ini at its root.

.PARAMETER Configuration
    CMake build configuration to build. Defaults to Release.

.PARAMETER OutputDirectory
    Directory the MatchFixer.zip archive is written to. Defaults to the repository root.

.EXAMPLE
    ./Build.ps1

.EXAMPLE
    ./Build.ps1 -Configuration Debug -OutputDirectory ./artifacts
#>
#Requires -Version 5.1
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',

    [string]$OutputDirectory = $PSScriptRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = $PSScriptRoot
$buildDir = Join-Path $repoRoot 'build'
$iniPath = Join-Path $repoRoot 'MatchFixer.ini'
$zipPath = Join-Path $OutputDirectory 'MatchFixer.zip'

if (-not (Test-Path -LiteralPath $iniPath -PathType Leaf)) {
    throw "Missing configuration file: $iniPath"
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw 'cmake was not found on PATH. Install CMake and the Visual Studio C++ toolchain.'
}

# The plugin is a 32-bit DLL for StarCraft 1.16.1, so a Visual Studio generator
# targeting Win32 is required.
$cmakeHelp = cmake --help | Out-String
$generator = @(
    'Visual Studio 18 2026',
    'Visual Studio 17 2022',
    'Visual Studio 16 2019',
    'Visual Studio 15 2017'
) | Where-Object { $cmakeHelp -match ([regex]::Escape($_) + '\s*=') } | Select-Object -First 1

if (-not $generator) {
    throw 'No Visual Studio CMake generator found. Install the Visual Studio C++ desktop workload.'
}

# 1. Build the DLL.
Write-Host "==> Configuring $Configuration x86 build in $buildDir" -ForegroundColor Cyan
cmake -S $repoRoot -B $buildDir -G $generator -A Win32
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed with exit code $LASTEXITCODE."
}

Write-Host "==> Building MatchFixer.dll" -ForegroundColor Cyan
cmake --build $buildDir --config $Configuration --target matchfixer
if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed with exit code $LASTEXITCODE."
}

$dllPath = Join-Path $buildDir "$Configuration\MatchFixer.dll"
if (-not (Test-Path -LiteralPath $dllPath -PathType Leaf)) {
    $found = Get-ChildItem -LiteralPath $buildDir -Filter 'MatchFixer.dll' -Recurse -File |
        Select-Object -First 1
    if (-not $found) {
        throw "Build succeeded but MatchFixer.dll was not found under $buildDir."
    }
    $dllPath = $found.FullName
}

Write-Host "    DLL: $dllPath" -ForegroundColor DarkGray

# 2. Package the DLL and the ini file as MatchFixer.zip.
if (-not (Test-Path -LiteralPath $OutputDirectory -PathType Container)) {
    New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
}

if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}

Write-Host "==> Packaging MatchFixer.zip" -ForegroundColor Cyan
Compress-Archive -LiteralPath $dllPath, $iniPath -DestinationPath $zipPath -CompressionLevel Optimal

$zipSize = [math]::Round((Get-Item -LiteralPath $zipPath).Length / 1KB, 1)
Write-Host "==> Done: $zipPath ($zipSize KB)" -ForegroundColor Green
