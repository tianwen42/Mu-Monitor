[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [switch]$Clean
)

& (Join-Path $PSScriptRoot "check.ps1") -Configuration $Configuration -Clean:$Clean
exit $LASTEXITCODE
