param(
    [Parameter(Mandatory = $true)]
    [string]$VppExecutable
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$vpp = (Resolve-Path $VppExecutable).Path
$example = Join-Path $repoRoot "examples\api_project\application.vi"
$stdoutLog = Join-Path ([System.IO.Path]::GetTempPath()) ("vpp-http-" + [guid]::NewGuid().ToString() + ".out.log")
$stderrLog = Join-Path ([System.IO.Path]::GetTempPath()) ("vpp-http-" + [guid]::NewGuid().ToString() + ".err.log")
$previousVppHome = $env:VPP_HOME
$server = $null

try {
    # The example is a V++ HTTP server, so this validates the native Winsock
    # adapter through the same public module path used by an installed project.
    $env:VPP_HOME = $repoRoot
    $startArgs = @{
        FilePath = $vpp
        ArgumentList = @($example)
        PassThru = $true
        NoNewWindow = $true
        RedirectStandardOutput = $stdoutLog
        RedirectStandardError = $stderrLog
    }
    $server = Start-Process @startArgs

    $healthy = $false
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        try {
            $response = Invoke-WebRequest -Uri "http://127.0.0.1:8080/health" -UseBasicParsing -TimeoutSec 1
            if ($response.StatusCode -eq 200 -and $response.Content.Trim() -eq "true") {
                $healthy = $true
                break
            }
        } catch {
            Start-Sleep -Milliseconds 100
        }
    }

    if (-not $healthy) {
        $stdout = if (Test-Path $stdoutLog) { Get-Content -Raw $stdoutLog } else { "" }
        $stderr = if (Test-Path $stderrLog) { Get-Content -Raw $stderrLog } else { "" }
        throw "V++ HTTP server did not answer /health. stdout: $stdout stderr: $stderr"
    }

    Write-Host "Windows native HTTP server smoke test passed."
} finally {
    if ($null -ne $server -and -not $server.HasExited) {
        Stop-Process -Id $server.Id -Force
        $server.WaitForExit()
    }
    if ($null -eq $previousVppHome) {
        Remove-Item Env:VPP_HOME -ErrorAction SilentlyContinue
    } else {
        $env:VPP_HOME = $previousVppHome
    }
    Remove-Item $stdoutLog, $stderrLog -Force -ErrorAction SilentlyContinue
}
