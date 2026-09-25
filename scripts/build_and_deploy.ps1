[CmdletBinding()]
param(
    [switch]$CleanData
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$ProjectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$QtRoot = "D:\Qt\6.11.2\mingw_64"
$QtBin = Join-Path $QtRoot "bin"
$WinDeployQt = Join-Path $QtBin "windeployqt.exe"
$MingwBin = "D:\Qt\Tools\mingw1310_64\bin"
$Strip = Join-Path $MingwBin "strip.exe"
$CMakeDir = "D:\Qt\Tools\CMake_64\bin"
$CMake = Join-Path $CMakeDir "cmake.exe"
$NinjaDir = "D:\Qt\Tools\Ninja"

$BuildDir = Join-Path $ProjectRoot ("build\script-release-" + $PID)
$DistDir = Join-Path $ProjectRoot "dist"
$DataDir = Join-Path $DistDir "data"
$PortableFlag = Join-Path $DistDir "portable.flag"

$AppTargetName = "Mu-Monitor"
$SimulatorTargetName = "DeviceSimulator"
$TargetNames = @($AppTargetName, $SimulatorTargetName)
$AppExecutableName = "$AppTargetName.exe"
$SimulatorExecutableName = "$SimulatorTargetName.exe"

function Assert-PathInsideProject {
    param([Parameter(Mandatory = $true)][string]$Path)

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $fullRoot = [System.IO.Path]::GetFullPath($ProjectRoot)
    $prefix = $fullRoot.TrimEnd('\') + '\'
    if (-not $fullPath.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to modify path outside project: $fullPath"
    }
}

function Assert-PathInsideDist {
    param([Parameter(Mandatory = $true)][string]$Path)

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $fullDist = [System.IO.Path]::GetFullPath($DistDir)
    $prefix = $fullDist.TrimEnd('\') + '\'
    if (-not $fullPath.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to modify path outside dist: $fullPath"
    }
}

function Remove-DirectoryWithRetry {
    param([Parameter(Mandatory = $true)][string]$Path)

    Assert-PathInsideProject -Path $Path
    for ($attempt = 1; $attempt -le 12; ++$attempt) {
        try {
            if (Test-Path -LiteralPath $Path) {
                [System.IO.Directory]::Delete($Path, $true)
            }
            return
        } catch [System.IO.IOException] {
            if ($attempt -eq 12) {
                throw
            }
            Write-Host "Directory is busy, retrying ($attempt/12): $Path"
            Start-Sleep -Seconds 2
        }
    }
}

function Remove-DistItem {
    param([Parameter(Mandatory = $true)][string]$RelativePath)

    $path = Join-Path $DistDir $RelativePath
    Assert-PathInsideDist -Path $path
    if (Test-Path -LiteralPath $path) {
        Remove-Item -LiteralPath $path -Recurse -Force
    }
}

function Assert-ToolExists {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required tool not found: $Path"
    }
}

function Stop-ProjectProcesses {
    $running = Get-Process -Name $TargetNames -ErrorAction SilentlyContinue
    foreach ($process in $running) {
        $processPath = $process.Path
        if ($processPath -and $processPath.StartsWith(
                $ProjectRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
            Write-Host "Stopping running project process: $processPath"
            Stop-Process -Id $process.Id -Force
        }
    }
    Start-Sleep -Milliseconds 300
}

Write-Host "Project root : $ProjectRoot"
Write-Host "Build dir    : $BuildDir"
Write-Host "Dist dir     : $DistDir"
Write-Host "Clean data   : $CleanData"

Stop-ProjectProcesses

Assert-ToolExists -Path $CMake
Assert-ToolExists -Path $WinDeployQt
Assert-ToolExists -Path $Strip
Assert-PathInsideProject -Path $BuildDir
Assert-PathInsideProject -Path $DistDir

$env:PATH = "$QtBin;$MingwBin;$CMakeDir;$NinjaDir;$env:PATH"

if (Test-Path -LiteralPath $BuildDir) {
    Write-Host "Cleaning previous script build..."
    Remove-DirectoryWithRetry -Path $BuildDir
}

Write-Host "Configuring fresh Release build..."
& $CMake -S $ProjectRoot -B $BuildDir -G Ninja `
    "-DCMAKE_BUILD_TYPE=Release" `
    "-DCMAKE_PREFIX_PATH=$QtRoot" `
    "-DBUILD_DEVICE_SIMULATOR=ON"
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed with exit code $LASTEXITCODE"
}

foreach ($target in $TargetNames) {
    Write-Host "Building $target..."
    & $CMake --build $BuildDir --target $target
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed for $target with exit code $LASTEXITCODE"
    }
}

$builtApp = Join-Path $BuildDir $AppExecutableName
$builtSimulator = Join-Path $BuildDir $SimulatorExecutableName
foreach ($builtExecutable in @($builtApp, $builtSimulator)) {
    if (-not (Test-Path -LiteralPath $builtExecutable -PathType Leaf)) {
        throw "Built executable not found: $builtExecutable"
    }
}

Stop-ProjectProcesses

New-Item -ItemType Directory -Force -Path $DistDir | Out-Null

if ($CleanData) {
    Write-Host "Explicitly cleaning preserved data directory: $DataDir"
    Remove-DistItem -RelativePath "data"
}
New-Item -ItemType Directory -Force -Path $DataDir | Out-Null
if (-not (Test-Path -LiteralPath $PortableFlag -PathType Leaf)) {
    New-Item -ItemType File -Path $PortableFlag | Out-Null
}

$distApp = Join-Path $DistDir $AppExecutableName
$distSimulator = Join-Path $DistDir $SimulatorExecutableName
Copy-Item -LiteralPath $builtApp -Destination $distApp -Force
Copy-Item -LiteralPath $builtSimulator -Destination $distSimulator -Force

Write-Host "Stripping release symbols from executables..."
foreach ($distExecutable in @($distApp, $distSimulator)) {
    & $Strip --strip-unneeded $distExecutable
    if ($LASTEXITCODE -ne 0) {
        throw "strip failed for $distExecutable with exit code $LASTEXITCODE"
    }
}

$readme = Join-Path $ProjectRoot "README.md"
if (Test-Path -LiteralPath $readme -PathType Leaf) {
    Copy-Item -LiteralPath $readme -Destination (Join-Path $DistDir "README.md") -Force
}

Write-Host "Running windeployqt for both executables..."
foreach ($distExecutable in @($distApp, $distSimulator)) {
    & $WinDeployQt --release --compiler-runtime --no-translations `
        --no-opengl-sw --no-system-d3d-compiler --no-system-dxc-compiler `
        $distExecutable
    if ($LASTEXITCODE -ne 0) {
        throw "windeployqt failed for $distExecutable with exit code $LASTEXITCODE"
    }
}

Write-Host "Pruning unused runtime components..."
$unusedRuntimeItems = @(
    "opengl32sw.dll",
    "D3Dcompiler_47.dll",
    "dxcompiler.dll",
    "dxil.dll",
    "Qt6Svg.dll",
    "generic",
    "iconengines",
    "networkinformation",
    "tls",
    "imageformats\qgif.dll",
    "imageformats\qjpeg.dll",
    "imageformats\qsvg.dll",
    "sqldrivers\qsqlibase.dll",
    "sqldrivers\qsqlmimer.dll",
    "sqldrivers\qsqloci.dll",
    "sqldrivers\qsqlodbc.dll",
    "sqldrivers\qsqlpsql.dll"
)

foreach ($item in $unusedRuntimeItems) {
    Remove-DistItem -RelativePath $item
}

$requiredRuntimeItems = @(
    "Qt6Core.dll",
    "Qt6Gui.dll",
    "Qt6Widgets.dll",
    "Qt6Network.dll",
    "Qt6Sql.dll",
    "platforms\qwindows.dll",
    "imageformats\qico.dll",
    "sqldrivers\qsqlite.dll"
)

foreach ($item in $requiredRuntimeItems) {
    $requiredPath = Join-Path $DistDir $item
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required runtime component was removed or missing: $requiredPath"
    }
}

foreach ($distExecutable in @($distApp, $distSimulator)) {
    if (-not (Test-Path -LiteralPath $distExecutable -PathType Leaf)) {
        throw "Deployed executable missing: $distExecutable"
    }
}

$distSize = (Get-ChildItem -LiteralPath $DistDir -Recurse -File |
    Measure-Object Length -Sum).Sum
Write-Host ""
Write-Host "Build and deployment completed successfully."
Write-Host "Application: $distApp"
Write-Host "Simulator  : $distSimulator"
Write-Host "Data dir   : $DataDir"
Write-Host ("Dist size  : {0:N2} MB" -f ($distSize / 1MB))