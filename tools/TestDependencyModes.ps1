param(
    [Parameter(Mandatory=$true)][string]$PolytreeSource,
    [Parameter(Mandatory=$true)][string]$AlgoSource,
    [Parameter(Mandatory=$true)][string]$SceneSource,
    [string]$BuildRoot = "$PSScriptRoot/../build/dependency-modes",
    [switch]$Fetch
)
$ErrorActionPreference = 'Stop'
$taskRepo = (Resolve-Path "$PSScriptRoot/..").Path
$taskBuild = [IO.Path]::GetFullPath($BuildRoot)
New-Item -ItemType Directory -Force $taskBuild | Out-Null
function Invoke-CMake([string[]]$Arguments) {
    & cmake @Arguments
    if ($LASTEXITCODE -ne 0) { throw "CMake failed: $Arguments" }
}
$taskPrefix = "$taskBuild/install"
foreach ($taskDependency in @(@('algo', $AlgoSource), @('polytree', $PolytreeSource))) {
    $taskName = $taskDependency[0]
    Invoke-CMake @('-S', $taskDependency[1], '-B', "$taskBuild/install-$taskName",
        "-DCMAKE_INSTALL_PREFIX=$taskPrefix", "-DCMAKE_PREFIX_PATH=$taskPrefix",
        '-DALGO_BUILD_TESTS=OFF', '-DPOLYTREE_BUILD_TESTS=OFF', '-DPOLYTREE_BUILD_BENCHMARKS=OFF')
    Invoke-CMake @('--install', "$taskBuild/install-$taskName", '--config', 'Release')
}
$taskModes = @('source','package')
if ($Fetch) { $taskModes += 'fetch' }
foreach ($taskMode in $taskModes) {
    foreach ($taskOrder in @('alone','terrain-first','scene-first')) {
        $taskDir = "$taskBuild/$taskMode-$taskOrder"
        $taskArgs = @('-S', "$taskRepo/tests/dependencies", '-B', $taskDir)
        if ($taskOrder -ne 'alone') { $taskArgs += "-DSCENE_SOURCE=$SceneSource" }
        if ($taskOrder -eq 'scene-first') { $taskArgs += '-DSCENE_FIRST=ON' }
        if ($taskMode -eq 'source') {
            $taskArgs += @("-DTERRAIN_COMPOSITOR_POLYTREE_SOURCE_DIR=$PolytreeSource",
                "-DTERRAIN_COMPOSITOR_ALGO_SOURCE_DIR=$AlgoSource",
                "-DSCENE_POLYTREE_POLYTREE_SOURCE_DIR=$PolytreeSource",
                "-DSCENE_POLYTREE_ALGO_SOURCE_DIR=$AlgoSource", '-DTERRAIN_COMPOSITOR_FETCH_DEPENDENCIES=OFF')
        } elseif ($taskMode -eq 'package') {
            $taskArgs += @("-DCMAKE_PREFIX_PATH=$taskPrefix", '-DTERRAIN_COMPOSITOR_FETCH_DEPENDENCIES=OFF')
        }
        Invoke-CMake $taskArgs
        Invoke-CMake @('--build', $taskDir, '--config', 'Release', '--target', 'terrain_coverage_tests', '--parallel', '8')
        & "$taskDir/coverage/Release/terrain_coverage_tests.exe"
        if ($LASTEXITCODE -ne 0) { throw "Coverage contracts failed in $taskMode-$taskOrder" }
    }
}

# Deterministic negative configuration checks; no network should be attempted.
$taskNegative = @('-S', "$taskRepo/tests/dependencies", '-B', "$taskBuild/missing",
    '-DTERRAIN_COMPOSITOR_FETCH_DEPENDENCIES=OFF', '-DCMAKE_DISABLE_FIND_PACKAGE_algo=TRUE')
$taskOutput = & cmake @taskNegative 2>&1 | Out-String
if ($LASTEXITCODE -eq 0 -or $taskOutput -notmatch 'requires algo::algo') { throw 'Missing dependency did not fail clearly' }
$taskNegative = @('-S', "$taskRepo/tests/dependencies", '-B', "$taskBuild/conflict",
    "-DTERRAIN_COMPOSITOR_ALGO_SOURCE_DIR=$AlgoSource", "-DSCENE_POLYTREE_ALGO_SOURCE_DIR=$PolytreeSource")
$taskOutput = & cmake @taskNegative 2>&1 | Out-String
if ($LASTEXITCODE -eq 0 -or $taskOutput -notmatch 'Conflicting algo source overrides') { throw 'Conflicting overrides were not rejected' }
Write-Output "Passed $($taskModes.Count * 3) dependency compositions and missing/conflicting dependency rejection checks"
exit 0
