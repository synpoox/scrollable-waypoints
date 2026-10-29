# Configures and builds the scrollable-waypoints plugin (Visual Studio generator, x64, Release).
#   powershell -ExecutionPolicy Bypass -File build.ps1 -Sdk <d2rAI-toolkit>\third_party\PluginSDK [-Build <dir>]
# Output: <Build>\Release\scrollable-waypoints.dll (mod.json "plugins" points at it; `modkit install` copies it).
param(
    [Parameter(Mandatory = $true)][string]$Sdk,    # PluginSDK directory (d2rAI-toolkit\third_party\PluginSDK)
    [string]$Build = ""                            # build directory (default <this folder>\build)
)
$ErrorActionPreference = 'Stop'
$Source = $PSScriptRoot
if (-not $Build) { $Build = Join-Path $Source "build" }
cmake -S $Source -B $Build -A x64 "-DMODKIT_SDK_DIR=$((Resolve-Path $Sdk).Path)"
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed ($LASTEXITCODE)" }
cmake --build $Build --config Release
if ($LASTEXITCODE -ne 0) { throw "cmake build failed ($LASTEXITCODE)" }
$dll = Join-Path $Build "Release\scrollable-waypoints.dll"
if (-not (Test-Path $dll)) { throw "build finished but $dll is missing" }
Write-Output "built: $dll"
