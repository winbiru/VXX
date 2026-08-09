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
$importProbeDir = Join-Path ([System.IO.Path]::GetTempPath()) ("vpp-import-" + [guid]::NewGuid().ToString())
$importProbe = Join-Path $importProbeDir "import-utf8.vi"
$previousVppHome = $env:VPP_HOME
$server = $null

try {
    # The example is a V++ HTTP server, so this validates the native Winsock
    # adapter through the same public module path used by an installed project.
    $env:VPP_HOME = $repoRoot

    # Keep UTF-8 bundled module lookup separate from the server assertion. The
    # source runs outside the repository, so imports must resolve through
    # VPP_HOME/gói/thư viện rather than a relative project path.
    [System.IO.Directory]::CreateDirectory($importProbeDir) | Out-Null
    $probeSource = @'
nhập mạng;
nhập "vào ra";
'@
    [System.IO.File]::WriteAllText(
        $importProbe,
        $probeSource,
        [System.Text.UTF8Encoding]::new($false)
    )
    $probeOutput = & $vpp $importProbe 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "V++ could not resolve UTF-8 bundled imports through VPP_HOME. output: $probeOutput"
    }

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
    $lastProbeError = ""
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        try {
            $response = Invoke-WebRequest -Uri "http://127.0.0.1:8080/health" -UseBasicParsing -NoProxy -TimeoutSec 1
            if ($response.StatusCode -eq 200 -and $response.Content.Trim() -eq "true") {
                $healthy = $true
                break
            }
            $lastProbeError = "unexpected response: status $($response.StatusCode), body '$($response.Content.Trim())'"
        } catch {
            $lastProbeError = $_.Exception.Message
        }
        Start-Sleep -Milliseconds 100
    }

    if (-not $healthy) {
        # Start-Process can retain exclusive handles for redirected logs until
        # the child exits. Release them before collecting diagnostics.
        if ($null -ne $server) {
            try {
                if (-not $server.HasExited) {
                    Stop-Process -Id $server.Id -Force
                }
                $server.WaitForExit()
            } catch {
                # The process can exit between HasExited and Stop-Process.
            }
        }
        $stdout = if (Test-Path $stdoutLog) { Get-Content -Raw $stdoutLog } else { "" }
        $stderr = if (Test-Path $stderrLog) { Get-Content -Raw $stderrLog } else { "" }
        throw "V++ HTTP server did not answer /health. probe: $lastProbeError stdout: $stdout stderr: $stderr"
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
    Remove-Item $importProbeDir -Recurse -Force -ErrorAction SilentlyContinue
}
