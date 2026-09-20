[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$ProjectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$QtRoot = "D:\Qt\6.11.2\mingw_64"
$QtBin = Join-Path $QtRoot "bin"
$WinDeployQt = Join-Path $QtBin "windeployqt.exe"
$MingwBin = "D:\Qt\Tools\mingw1310_64\bin"
$CMakeDir = "D:\Qt\Tools\CMake_64\bin"
$CMake = Join-Path $CMakeDir "cmake.exe"
$NinjaDir = "D:\Qt\Tools\Ninja"
$BuildDir = Join-Path $ProjectRoot "build\script-release"
$DistDir = Join-Path $ProjectRoot "dist"
$TargetName = "Mu-Monitor"
$ExecutableName = "$TargetName.exe"

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

Write-Host "Project root : $ProjectRoot"
Write-Host "Build dir    : $BuildDir"
Write-Host "Dist dir     : $DistDir"

$running = Get-Process -Name $TargetName -ErrorAction SilentlyContinue
if ($running) {
    $paths = ($running | Select-Object -ExpandProperty Path -ErrorAction SilentlyContinue) -join ", "
    throw "Mu-Monitor is running. Close it before building. Running path(s): $paths"
}

Assert-ToolExists -Path $CMake
Assert-ToolExists -Path $WinDeployQt
Assert-PathInsideProject -Path $BuildDir
Assert-PathInsideProject -Path $DistDir

$env:PATH = "$QtBin;$MingwBin;$CMakeDir;$NinjaDir;$env:PATH"

if (Test-Path -LiteralPath $BuildDir) {
    Write-Host "Cleaning previous script build..."
    [System.IO.Directory]::Delete($BuildDir, $true)
}

Write-Host "Configuring fresh Release build..."
& $CMake -S $ProjectRoot -B $BuildDir -G Ninja "-DCMAKE_BUILD_TYPE=Release" "-DCMAKE_PREFIX_PATH=$QtRoot"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }

Write-Host "Building $TargetName..."
& $CMake --build $BuildDir --target $TargetName
if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE" }

$builtExecutable = Join-Path $BuildDir $ExecutableName
if (-not (Test-Path -LiteralPath $builtExecutable -PathType Leaf)) {
    throw "Built executable not found: $builtExecutable"
}

if (Test-Path -LiteralPath $DistDir) {
    Write-Host "Cleaning previous dist..."
    [System.IO.Directory]::Delete($DistDir, $true)
}
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null

$distExecutable = Join-Path $DistDir $ExecutableName
Copy-Item -LiteralPath $builtExecutable -Destination $distExecutable -Force

$readme = Join-Path $ProjectRoot "README.md"
if (Test-Path -LiteralPath $readme -PathType Leaf) {
    Copy-Item -LiteralPath $readme -Destination (Join-Path $DistDir "README.md") -Force
}

Write-Host "Running windeployqt..."
& $WinDeployQt --release --compiler-runtime --no-translations $distExecutable
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE" }

$distSize = (Get-ChildItem -LiteralPath $DistDir -Recurse -File | Measure-Object Length -Sum).Sum
Write-Host ""
Write-Host "Build and deployment completed successfully."
Write-Host "Executable : $distExecutable"
Write-Host ("Dist size  : {0:N2} MB" -f ($distSize / 1MB))
