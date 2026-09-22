param([string]$BuildRoot = "$PSScriptRoot/../build/dependency-modes")
$ErrorActionPreference = 'Stop'
$taskRepo = (Resolve-Path "$PSScriptRoot/..").Path
$taskBuild = [IO.Path]::GetFullPath($BuildRoot)
New-Item -ItemType Directory -Force $taskBuild | Out-Null
function Invoke-CMake([string[]]$Arguments) {
    & cmake @Arguments
    if ($LASTEXITCODE -ne 0) { throw "CMake failed: $Arguments" }
}
$taskDir = "$taskBuild/terrain"
Invoke-CMake @('-S', "$taskRepo/tests/dependencies", '-B', $taskDir,
    '-DFETCHCONTENT_FULLY_DISCONNECTED=ON')
Invoke-CMake @('--build', $taskDir, '--config', 'Release', '--parallel', '4')
& ctest --test-dir "$taskDir/coverage" -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Coverage contracts failed' }
foreach ($taskDependency in @('POLYTREE','ALGO')) {
    $taskOutput = & cmake -S "$taskRepo/tests/dependencies" -B "$taskBuild/conflict-$taskDependency" `
        "-DTC_TEST_CONFLICT_$taskDependency=ON" 2>&1 | Out-String
    if ($LASTEXITCODE -eq 0 -or $taskOutput -notmatch 'Conflicting .*::') {
        throw "Conflicting $taskDependency target was not rejected: $taskOutput"
    }
}
Write-Output 'Passed the pinned dependency build and both conflicting-target rejection checks'
