[CmdletBinding()]
param()

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

$ProtectedDeleteDirectoryNames = @("data", "backups", "logs")

function Assert-DeletePathSafe {
    param([Parameter(Mandatory = $true)][string]$Path)

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $segments = $fullPath -split '[\\/]+'
    foreach ($segment in $segments) {
        if ($ProtectedDeleteDirectoryNames -contains $segment.ToLowerInvariant()) {
            throw "Refusing to delete protected path '$fullPath': protected directory name '$segment' is part of the path."
        }
    }

    if (-not (Test-Path -LiteralPath $fullPath -PathType Container)) {
        return
    }

    $pending = [System.Collections.Generic.Stack[string]]::new()
    $pending.Push($fullPath)
    while ($pending.Count -gt 0) {
        $current = $pending.Pop()
        foreach ($child in [System.IO.Directory]::EnumerateDirectories($current)) {
            $childName = [System.IO.Path]::GetFileName($child)
            if ($ProtectedDeleteDirectoryNames -contains $childName.ToLowerInvariant()) {
                throw "Refusing to delete '$fullPath': it contains protected directory '$child'."
            }

            $attributes = [System.IO.File]::GetAttributes($child)
            if (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -eq 0) {
                $pending.Push($child)
            }
        }
    }
}

function Remove-DirectoryWithRetry {
    param([Parameter(Mandatory = $true)][string]$Path)

    Assert-PathInsideProject -Path $Path
    Assert-DeletePathSafe -Path $Path
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

function Assert-ToolExists {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required tool not found: $Path"
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

function Remove-DistItem {
    param([Parameter(Mandatory = $true)][string]$RelativePath)
    $path = Join-Path $DistDir $RelativePath
    Assert-PathInsideDist -Path $path
    Assert-DeletePathSafe -Path $path
    if (Test-Path -LiteralPath $path) {
        Remove-Item -LiteralPath $path -Recurse -Force
    }
}

function Remove-DistDeployables {
    if (-not (Test-Path -LiteralPath $DistDir -PathType Container)) {
        return
    }

    foreach ($item in Get-ChildItem -LiteralPath $DistDir -Force) {
        if ($ProtectedDeleteDirectoryNames -contains $item.Name.ToLowerInvariant()) {
            Write-Host "Preserving protected dist item: $($item.FullName)"
            continue
        }

        Remove-DistItem -RelativePath $item.Name
    }
}

Write-Host "Project root : $ProjectRoot"
Write-Host "Build dir    : $BuildDir"
Write-Host "Dist dir     : $DistDir"

function Stop-ProjectProcess {
    $running = Get-Process -Name $TargetName -ErrorAction SilentlyContinue
    foreach ($process in $running) {
        $processPath = $process.Path
        if ($processPath -and $processPath.StartsWith($ProjectRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
            Write-Host "Stopping running project process: $processPath"
            Stop-Process -Id $process.Id -Force
        }
    }
    Start-Sleep -Milliseconds 300
}

Stop-ProjectProcess

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
& $CMake -S $ProjectRoot -B $BuildDir -G Ninja "-DCMAKE_BUILD_TYPE=Release" "-DCMAKE_PREFIX_PATH=$QtRoot"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }

Write-Host "Building $TargetName..."
& $CMake --build $BuildDir --target $TargetName
if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE" }

$builtExecutable = Join-Path $BuildDir $ExecutableName
if (-not (Test-Path -LiteralPath $builtExecutable -PathType Leaf)) {
    throw "Built executable not found: $builtExecutable"
}

Stop-ProjectProcess

if (Test-Path -LiteralPath $DistDir) {
    Write-Host "Cleaning previous deployment files while preserving protected data directories..."
    Remove-DistDeployables
}
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null

$distExecutable = Join-Path $DistDir $ExecutableName
Copy-Item -LiteralPath $builtExecutable -Destination $distExecutable -Force

Write-Host "Stripping release symbols from executable..."
& $Strip --strip-unneeded $distExecutable
if ($LASTEXITCODE -ne 0) { throw "strip failed with exit code $LASTEXITCODE" }

$readme = Join-Path $ProjectRoot "README.md"
if (Test-Path -LiteralPath $readme -PathType Leaf) {
    Copy-Item -LiteralPath $readme -Destination (Join-Path $DistDir "README.md") -Force
}

Write-Host "Running windeployqt..."
& $WinDeployQt --release --compiler-runtime --no-translations --no-opengl-sw --no-system-d3d-compiler --no-system-dxc-compiler $distExecutable
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE" }

Write-Host "Pruning unused runtime components..."
$unusedRuntimeItems = @(
    "opengl32sw.dll",
    "D3Dcompiler_47.dll",
    "dxcompiler.dll",
    "dxil.dll",
    "Qt6Network.dll",
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

$distSize = (Get-ChildItem -LiteralPath $DistDir -Recurse -File | Measure-Object Length -Sum).Sum
Write-Host ""
Write-Host "Build and deployment completed successfully."
Write-Host "Executable : $distExecutable"
Write-Host ("Dist size  : {0:N2} MB" -f ($distSize / 1MB))
