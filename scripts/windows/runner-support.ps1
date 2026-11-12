Set-StrictMode -Version Latest

function Get-VppTestPython {
    # Application lookup can return both setup-python and the WindowsApps
    # alias. Invoking their .Source array joins two paths into one command.
    $command = Get-Command python -CommandType Application -ErrorAction Stop |
        Select-Object -First 1
    return $command.Source
}

function Stop-VppTestProcess {
    param([System.Diagnostics.Process]$Process)

    if ($null -eq $Process) { return }
    # vpp-cli -> vpp-native -> generated HTTP executable. Killing only the
    # CLI leaves the server and inherited handles alive until CTest times out.
    if (-not $Process.HasExited) { $Process.Kill($true) }
    if (-not $Process.WaitForExit(5000)) {
        throw "Test process $($Process.Id) did not exit after tree termination"
    }
}

function Wait-VppTestProcess {
    param(
        [System.Diagnostics.Process]$Process,
        [int]$TimeoutSeconds = 600,
        [string]$Description = "V++ test"
    )

    if (-not $Process.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-VppTestProcess $Process
        throw "$Description exceeded ${TimeoutSeconds}s; stopped its process tree"
    }
    return $Process.ExitCode
}
