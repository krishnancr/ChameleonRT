# Build script for ChameleonRT with Slang support
# Simplified version for Migration package
# Usage:
#   .\clean_rebuild.ps1                           # Clean rebuild Debug
#   .\clean_rebuild.ps1 -Config Release           # Clean rebuild Release
#   .\clean_rebuild.ps1 -Build                    # Build only (no clean)

param(
    [switch]$Build,
    [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"

Write-Host "=== ChameleonRT Build Script ===" -ForegroundColor Cyan
Write-Host "Configuration: $Config" -ForegroundColor Yellow

# Define paths - adjust these for your setup
$SLANG_PATH = if ($env:SLANG_PATH) { $env:SLANG_PATH } else { "C:\dev\slang\build\$Config" }
$PROJECT_ROOT = $PSScriptRoot
$BUILD_DIR = "$PROJECT_ROOT\build"

Write-Host "Slang path: $SLANG_PATH" -ForegroundColor Cyan
Write-Host "Build directory: $BUILD_DIR" -ForegroundColor Cyan

# Clean if requested
if (-not $Build) {
    Write-Host "`nCleaning build directory..." -ForegroundColor Yellow
    if (Test-Path $BUILD_DIR) {
        Remove-Item -Recurse -Force $BUILD_DIR
    }
    New-Item -ItemType Directory -Force -Path $BUILD_DIR | Out-Null
}

# Configure CMake
Write-Host "`nConfiguring CMake..." -ForegroundColor Cyan
$cmakeArgs = @(
    "-B", $BUILD_DIR,
    "-S", $PROJECT_ROOT,
    "-DSlang_ROOT=$SLANG_PATH",
    "-DENABLE_SLANG=ON"
)

Write-Host "Running: cmake $($cmakeArgs -join ' ')" -ForegroundColor Gray
& cmake @cmakeArgs

if ($LASTEXITCODE -ne 0) {
    Write-Host "`nCMake configuration failed!" -ForegroundColor Red
    exit $LASTEXITCODE
}

# Build
Write-Host "`nBuilding..." -ForegroundColor Cyan
$buildArgs = @(
    "--build", $BUILD_DIR,
    "--config", $Config,
    "--parallel"
)

Write-Host "Running: cmake $($buildArgs -join ' ')" -ForegroundColor Gray
& cmake @buildArgs

if ($LASTEXITCODE -ne 0) {
    Write-Host "`nBuild failed!" -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host "`n=== Build Complete ===" -ForegroundColor Green
Write-Host "Executable: $BUILD_DIR\$Config\chameleonrt.exe" -ForegroundColor Cyan
