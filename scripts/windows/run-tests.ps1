param(
    [string]$VppExecutable = $env:VPP_EXEC,
    [string]$NativeCompiler = $env:VPP_NATIVE_COMPILER,
    [string]$LogDirectory = $env:VPP_TEST_LOG_DIR,
    [ValidateSet('full', 'i32', 'smoke')]
    [string]$Suite = 'full',
    [string[]]$Case = @(),
    [ValidateRange(1, 2147483647)]
    [int]$Jobs = 2,
    [ValidateRange(1, 2147483647)]
    [int]$ShardCount = 1,
    [ValidateRange(0, 2147483647)]
    [int]$ShardIndex = 0,
    [ValidateRange(1, 4)]
    [int]$BatchSize = 1,
    [ValidateRange(1, 2147483647)]
    [int]$Timeout = 900,
    [string]$Clang = $env:VPP_AOT_ASSEMBLER,
    [switch]$ModuleSuites,
    [switch]$List,
    [switch]$Inventory
)

# Use the same manifest, platform gates, oracles and logs as Unix and CI.
# The old host CLI, Bash arrays and interpreter/JIT modes have been retired.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'runner-support.ps1')
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$python = Get-VppTestPython
$options = @('--suite', $Suite)
foreach ($id in $Case) { $options += @('--case', $id) }

# Inventory and selection do not require a compiler or a Windows toolchain.
if ($List -or $Inventory) {
    if ($List) { $options += '--list' }
    if ($Inventory) { $options += '--inventory' }
    & $python (Join-Path $repoRoot 'scripts/quality/run_native_regression.py') @options
    exit $LASTEXITCODE
}

if ($BatchSize -gt 1 -and $Suite -ne 'full') {
    throw 'BatchSize requires Suite full'
}
if ($ModuleSuites -and $Suite -ne 'full') {
    throw 'ModuleSuites requires Suite full'
}
# An explicit compiler path wins over an inherited VPP_NATIVE_COMPILER. An
# explicit -NativeCompiler still supports callers passing the old host launcher.
if (-not [string]::IsNullOrWhiteSpace($NativeCompiler) -and
    ($PSBoundParameters.ContainsKey('NativeCompiler') -or
     [string]::IsNullOrWhiteSpace($VppExecutable))) {
    $VppExecutable = $NativeCompiler
}
if ([string]::IsNullOrWhiteSpace($VppExecutable)) {
    $name = if ($Suite -eq 'i32') { 'vpp-i32.exe' } else { 'vpp-native.exe' }
    $VppExecutable = Join-Path $repoRoot "build-pure-vpp/bin/$name"
}
$compiler = (Resolve-Path -LiteralPath $VppExecutable).Path
$package = Split-Path (Split-Path $compiler -Parent) -Parent
if ([string]::IsNullOrWhiteSpace($LogDirectory)) {
    $LogDirectory = Join-Path $repoRoot 'build-pure-vpp/regression-logs'
}
if ([string]::IsNullOrWhiteSpace($Clang)) {
    $Clang = (Get-Command clang -CommandType Application -ErrorAction Stop |
        Select-Object -First 1).Source
}
$options += @('--compiler', $compiler, '--jobs', "$Jobs", '--batch-size', "$BatchSize",
    '--timeout', "$Timeout", '--logs', $LogDirectory,
    '--shard-count', "$ShardCount", '--shard-index', "$ShardIndex")
if ($ModuleSuites) { $options += '--module-suites' }
# The package helper establishes the Windows runtime/linker environment and
# invokes run_native_regression.py. Native argument arrays preserve UTF-8
# paths and spaces without Start-Process command-line quoting.
& $python (Join-Path $repoRoot 'scripts/quality/check_windows_full_bootstrap.py') `
    --clang $Clang regression $package @options
exit $LASTEXITCODE
