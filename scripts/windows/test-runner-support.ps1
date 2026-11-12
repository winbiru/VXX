Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'runner-support.ps1')

$temporary = Join-Path ([IO.Path]::GetTempPath()) ("vpp-runner-test-" + [guid]::NewGuid())
$previousPath = $env:PATH
$parent = $null
$child = $null
try {
    # Reproduce the hosted runner's two python applications on PATH without
    # requiring either the WindowsApps alias or a second Python installation.
    $first = New-Item -ItemType Directory -Path (Join-Path $temporary 'first python') -Force
    $second = New-Item -ItemType Directory -Path (Join-Path $temporary 'second python') -Force
    $pythonName = if ($IsWindows) { 'python.exe' } else { 'python' }
    $pwsh = (Get-Process -Id $PID).Path
    foreach ($directory in @($first.FullName, $second.FullName)) {
        Copy-Item -LiteralPath $pwsh -Destination (Join-Path $directory $pythonName)
    }
    $env:PATH = $first.FullName + [IO.Path]::PathSeparator + $second.FullName + [IO.Path]::PathSeparator + $previousPath
    $matches = @(Get-Command python -CommandType Application)
    if ($matches.Count -lt 2) { throw 'Duplicate Python regression did not create multiple applications' }
    $selected = @(Get-VppTestPython)
    if ($selected.Count -ne 1 -or $selected[0] -cne (Join-Path $first.FullName $pythonName)) {
        throw "Python resolution must select exactly the first application: $selected"
    }
    $env:PATH = $previousPath
    Write-Host 'Python lookup with multiple applications: PASS'

    $finished = Start-Process -FilePath $pwsh -ArgumentList @('-NoProfile', '-NonInteractive', '-Command', 'exit 7') -PassThru -NoNewWindow
    $exitCode = Wait-VppTestProcess $finished -TimeoutSeconds 10
    if ($exitCode -ne 7) { throw "Bounded wait lost child exit code: $exitCode" }
    Write-Host 'Bounded wait preserves child exit code: PASS'

    $parentScript = Join-Path $temporary 'parent.ps1'
    $childPidFile = Join-Path $temporary 'child.pid'
    @'
param([string]$Pwsh, [string]$PidFile)
$child = Start-Process -FilePath $Pwsh -ArgumentList @('-NoProfile', '-NonInteractive', '-Command', 'Start-Sleep -Seconds 60') -PassThru -NoNewWindow
[IO.File]::WriteAllText($PidFile, [string]$child.Id)
Start-Sleep -Seconds 60
'@ | Set-Content -LiteralPath $parentScript -Encoding utf8
    $parent = Start-Process -FilePath $pwsh -ArgumentList @(
        '-NoProfile', '-NonInteractive', '-File', ('"' + $parentScript + '"'),
        '-Pwsh', ('"' + $pwsh + '"'), '-PidFile', ('"' + $childPidFile + '"')
    ) -PassThru -NoNewWindow -RedirectStandardOutput (Join-Path $temporary 'stdout') -RedirectStandardError (Join-Path $temporary 'stderr')
    $clock = [Diagnostics.Stopwatch]::StartNew()
    while (-not (Test-Path -LiteralPath $childPidFile)) {
        if ($parent.HasExited -or $clock.Elapsed.TotalSeconds -gt 10) {
            throw 'Parent did not start its server descendant'
        }
        Start-Sleep -Milliseconds 50
    }
    $child = Get-Process -Id ([int][IO.File]::ReadAllText($childPidFile))
    $timedOut = $false
    try {
        Wait-VppTestProcess $parent -TimeoutSeconds 1 -Description 'Intentional hung server'
    } catch {
        if ($_.Exception.Message -notlike '*exceeded 1s*') { throw }
        $timedOut = $true
    }
    if (-not $timedOut -or -not $parent.HasExited -or -not $child.WaitForExit(5000)) {
        throw 'Timed out worker left a live parent or server descendant'
    }
    # Cleanup must also accept a null handle and an already-exited process.
    Stop-VppTestProcess $null
    Stop-VppTestProcess $parent
    Write-Host 'Bounded timeout and server descendant cleanup: PASS'
} finally {
    $env:PATH = $previousPath
    foreach ($process in @($child, $parent)) {
        if ($null -ne $process) { Stop-VppTestProcess $process }
    }
    Remove-Item -LiteralPath $temporary -Recurse -Force -ErrorAction SilentlyContinue
}
