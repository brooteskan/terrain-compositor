param([string]$BuildRoot = "$PSScriptRoot/../build/dependency-modes")
$ErrorActionPreference = 'Stop'
$taskRepo = (Resolve-Path "$PSScriptRoot/..").Path
$taskBuild = [IO.Path]::GetFullPath($BuildRoot)
New-Item -ItemType Directory -Force $taskBuild | Out-Null
function Invoke-CMake([string[]]$Arguments) {
    & cmake @Arguments
    if ($LASTEXITCODE -ne 0) { throw "CMake failed: $Arguments" }
}
foreach ($taskOrder in @('alone','terrain-first','scene-first')) {
    $taskDir = "$taskBuild/$taskOrder"
    Invoke-CMake @('-S', "$taskRepo/tests/dependencies", '-B', $taskDir,
        "-DTC_TEST_COMPOSITION=$taskOrder", '-DFETCHCONTENT_FULLY_DISCONNECTED=ON')
    Invoke-CMake @('--build', $taskDir, '--config', 'Release', '--parallel', '4')
    & ctest --test-dir "$taskDir/coverage" -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "Coverage contracts failed in $taskOrder" }
}
foreach ($taskDependency in @('POLYTREE','ALGO')) {
    $taskOutput = & cmake -S "$taskRepo/tests/dependencies" -B "$taskBuild/conflict-$taskDependency" `
        "-DTC_TEST_CONFLICT_$taskDependency=ON" 2>&1 | Out-String
    if ($LASTEXITCODE -eq 0 -or $taskOutput -notmatch 'Conflicting .*::') {
        throw "Conflicting $taskDependency target was not rejected: $taskOutput"
    }
}
Write-Output 'Passed 3 pinned-submodule compositions and both conflicting-target rejection checks'
