# Build script for ChameleonRT with proper paths to SDL2 and Slang
# PowerShell script
# Usage: 
#   .\clean_rebuild.ps1 -Clean     # Clean rebuild (default)
#   .\clean_rebuild.ps1 -Build     # Simple build only

param(
    [switch]$Clean,
    [switch]$Build
)

# Default to clean rebuild if no parameter is specified
if (-not $Clean -and -not $Build) {
    $Clean = $true
}

if ($Clean) {
    Write-Host "Starting clean rebuild of ChameleonRT..." -ForegroundColor Green
} else {
    Write-Host "Starting build of ChameleonRT..." -ForegroundColor Green
}

# Stop on errors
$ErrorActionPreference = "Stop"

# Set proxy environment variables
$env:https_proxy = "http://proxy-dmz.intel.com:912"
$env:http_proxy = "http://proxy-dmz.intel.com:912"
Write-Host "Set proxy environment variables" -ForegroundColor Cyan

# Define paths
$SLANG_PATH = "C:\dev\slang\build\RelWithDebInfo"
$SDL2_PATH = "C:\dev\SDL2\cmake"
$PROJECT_ROOT = "C:\dev\ChameleonRT"
$BUILD_DIR = "$PROJECT_ROOT\build"

Write-Host "Using Slang path: $SLANG_PATH" -ForegroundColor Cyan
Write-Host "Using SDL2 path: $SDL2_PATH" -ForegroundColor Cyan

if ($Clean) {
    # Remove old build directory if it exists
    if (Test-Path $BUILD_DIR) {
        Write-Host "Removing old build directory..." -ForegroundColor Yellow
        Remove-Item -Recurse -Force $BUILD_DIR
    }

    # Create new build directory
    Write-Host "Creating new build directory..." -ForegroundColor Yellow
    New-Item -Path $BUILD_DIR -ItemType Directory | Out-Null

    # Configure CMake with proper paths
    Write-Host "Configuring CMake..." -ForegroundColor Yellow
    Set-Location $PROJECT_ROOT
    Write-Host "Running CMake configure command..." -ForegroundColor Yellow
    $cmakeCommand = "cmake -B build `"-DSlang_ROOT=$SLANG_PATH`" `"-DENABLE_SLANG=ON`" `"-DSDL2_DIR=$SDL2_PATH`" -DCMAKE_BUILD_TYPE=Debug -DENABLE_DXR=ON ."
    Write-Host $cmakeCommand -ForegroundColor Gray
    Invoke-Expression $cmakeCommand
} else {
    # Just verify build directory exists for simple build
    if (-not (Test-Path $BUILD_DIR)) {
        Write-Host "Build directory not found. Please run clean rebuild first: .\clean_rebuild.ps1 -Clean" -ForegroundColor Red
        exit 1
    }
    Write-Host "Using existing build configuration..." -ForegroundColor Cyan
}

# Build the project
Write-Host "Building project..." -ForegroundColor Yellow
cd build
cmake --build . --config Debug

Pop-Location
if ($Clean) {
    Write-Host "Clean rebuild completed!" -ForegroundColor Green
} else {
    Write-Host "Build completed!" -ForegroundColor Green
}
