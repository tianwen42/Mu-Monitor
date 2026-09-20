[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$ProjectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$BuildDir = Join-Path $ProjectRoot ("build\check-" + $Configuration.ToLowerInvariant())
$QtRoot = "D:\Qt\6.11.2\mingw_64"
$QtBin = Join-Path $QtRoot "bin"
$MingwBin = "D:\Qt\Tools\mingw1310_64\bin"
$CMakeDir = "D:\Qt\Tools\CMake_64\bin"
$NinjaDir = "D:\Qt\Tools\Ninja"
$CMake = Join-Path $CMakeDir "cmake.exe"
$CTest = Join-Path $CMakeDir "ctest.exe"

function Assert-PathInsideProject {
    param([Parameter(Mandatory = $true)][string]$Path)

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $fullRoot = [System.IO.Path]::GetFullPath($ProjectRoot)
    $prefix = $fullRoot.TrimEnd('\') + '\'
    if (-not $fullPath.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to modify path outside project: $fullPath"
    }
}

function Assert-ToolExists {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required tool not found: $Path"
    }
}

Assert-ToolExists -Path $CMake
Assert-ToolExists -Path $CTest
Assert-ToolExists -Path (Join-Path $QtBin "qmake.exe")
Assert-ToolExists -Path (Join-Path $MingwBin "g++.exe")
Assert-PathInsideProject -Path $BuildDir

$env:PATH = "$QtBin;$MingwBin;$CMakeDir;$NinjaDir;$env:PATH"

if ($Clean -and (Test-Path -LiteralPath $BuildDir)) {
    Write-Host "Cleaning check build directory: $BuildDir"
    Remove-Item -LiteralPath $BuildDir -Recurse -Force
}

Write-Host "Configuring Mu-Monitor ($Configuration)..."
& $CMake -S $ProjectRoot -B $BuildDir -G Ninja `
    "-DCMAKE_BUILD_TYPE=$Configuration" `
    "-DCMAKE_PREFIX_PATH=$QtRoot" `
    "-DBUILD_TESTING=ON" `
    "-DBUILD_DEVICE_SIMULATOR=ON"
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed with exit code $LASTEXITCODE"
}

Write-Host "Building all targets..."
& $CMake --build $BuildDir
if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE"
}

Write-Host "Running tests..."
& $CTest --test-dir $BuildDir --output-on-failure
if ($LASTEXITCODE -ne 0) {
    throw "Tests failed with exit code $LASTEXITCODE"
}

Write-Host ""
Write-Host "Check completed successfully."
Write-Host "Build directory: $BuildDir"
