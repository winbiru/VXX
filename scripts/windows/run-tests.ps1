param(
    [Parameter(Mandatory = $true)]
    [string]$VppExecutable
)

# Full Windows counterpart to run_tests.sh.  The Unix runner is the canonical
# manifest for golden and expected-runtime-failure tests; parse those arrays
# directly so the Windows matrix cannot silently drift behind it.
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$script:Vpp = (Resolve-Path -LiteralPath $VppExecutable).Path
$script:PassCount = 0
$script:FailCount = 0
$script:FailedTests = @()
$script:LastHttpProbeError = ""
$sessionDir = Join-Path (Join-Path $repoRoot "src\tests\.tmp") ("windows-" + [guid]::NewGuid().ToString())
$externalVppHome = $null
$fixtureProcess = $null
$previousVppHome = $env:VPP_HOME

function Add-Pass {
    param([string]$Name)

    Write-Host "PASS: $Name"
    $script:PassCount++
}

function Add-Fail {
    param([string]$Name)

    Write-Host "FAIL: $Name"
    $script:FailCount++
    $script:FailedTests += $Name
}

function Get-NormalizedUtf8Text {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        return $null
    }

    # Start-Process may release redirected log handles a moment after its
    # child has exited. Retry transient sharing violations so diagnostics do
    # not hide the original server startup failure.
    $lastError = $null
    for ($attempt = 0; $attempt -lt 20; $attempt++) {
        try {
            $bytes = [System.IO.File]::ReadAllBytes($Path)
            $text = [System.Text.UTF8Encoding]::new($false).GetString($bytes)
            return $text.Replace("`r`n", "`n").Replace("`r", "`n")
        } catch [System.IO.IOException] {
            $lastError = $_.Exception.Message
            Start-Sleep -Milliseconds 100
        }
    }
    return "[không thể đọc log sau 2 giây: $lastError]"
}

function Show-FailureDetails {
    param(
        [string]$StdErr,
        [int]$ExitCode
    )

    if ($ExitCode -ne 0) {
        Write-Host "  exit code: $ExitCode"
    }
    $errorText = Get-NormalizedUtf8Text $StdErr
    if (-not [string]::IsNullOrWhiteSpace($errorText)) {
        Write-Host "  stderr:"
        Write-Host $errorText
    }
}

function Test-ExpectedOutput {
    param(
        [string]$Expected,
        [string]$Actual
    )

    $expectedText = Get-NormalizedUtf8Text $Expected
    $actualText = Get-NormalizedUtf8Text $Actual
    if ($expectedText -ceq $actualText) {
        return $true
    }

    Write-Host "  --- expected: $Expected"
    Write-Host $expectedText
    Write-Host "  --- actual: $Actual"
    Write-Host $actualText
    return $false
}

function Invoke-Vpp {
    param(
        [string[]]$Arguments,
        [string]$StdOut,
        [string]$StdErr,
        [string]$WorkingDirectory,
        [hashtable]$Environment = @{}
    )

    Remove-Item -LiteralPath $StdOut, $StdErr -Force -ErrorAction SilentlyContinue
    $previousEnvironment = @{}
    foreach ($key in $Environment.Keys) {
        $previousEnvironment[$key] = [Environment]::GetEnvironmentVariable(
            $key,
            [EnvironmentVariableTarget]::Process
        )
        [Environment]::SetEnvironmentVariable(
            $key,
            [string]$Environment[$key],
            [EnvironmentVariableTarget]::Process
        )
    }

    try {
        $process = Start-Process -FilePath $script:Vpp -ArgumentList $Arguments -WorkingDirectory $WorkingDirectory -RedirectStandardOutput $StdOut -RedirectStandardError $StdErr -PassThru -Wait -NoNewWindow
        return $process.ExitCode
    } finally {
        foreach ($key in $Environment.Keys) {
            [Environment]::SetEnvironmentVariable(
                $key,
                $previousEnvironment[$key],
                [EnvironmentVariableTarget]::Process
            )
        }
    }
}

function Start-VppServer {
    param(
        [string]$Source,
        [string]$StdOut,
        [string]$StdErr,
        [string]$WorkingDirectory
    )

    Remove-Item -LiteralPath $StdOut, $StdErr -Force -ErrorAction SilentlyContinue
    return Start-Process -FilePath $script:Vpp -ArgumentList @($Source) -WorkingDirectory $WorkingDirectory -RedirectStandardOutput $StdOut -RedirectStandardError $StdErr -PassThru -NoNewWindow
}

function Stop-VppServer {
    param($Process)

    if ($null -eq $Process) {
        return
    }

    try {
        if (-not $Process.HasExited) {
            Stop-Process -Id $Process.Id -Force -ErrorAction Stop
        }
        $Process.WaitForExit()
    } catch {
        # Best-effort cleanup: the process may already have exited.
    }
}

function Wait-ForHttp {
    param(
        [string]$Uri,
        [string]$ExpectedContent,
        [int]$Attempts = 20
    )

    $script:LastHttpProbeError = ""
    for ($attempt = 0; $attempt -lt $Attempts; $attempt++) {
        try {
            # Local fixture checks must not inherit a corporate/runner proxy.
            # PowerShell/.NET can spend noticeable time initializing the first
            # localhost request on a fresh Windows runner. Give each probe enough
            # time to complete without turning normal startup into a client abort.
            $response = Invoke-WebRequest -Uri $Uri -UseBasicParsing -NoProxy -TimeoutSec 3
            if ($response.StatusCode -eq 200 -and $response.Content.Trim() -eq $ExpectedContent) {
                return $true
            }
            $script:LastHttpProbeError = "unexpected response: status $($response.StatusCode), body '$($response.Content.Trim())'"
        } catch {
            $script:LastHttpProbeError = $_.Exception.Message
        }
        Start-Sleep -Milliseconds 100
    }
    return $false
}

function Invoke-ExpectedTest {
    param(
        [string]$TestFile,
        [hashtable]$Environment = @{}
    )

    $base = [System.IO.Path]::GetFileNameWithoutExtension($TestFile)
    $expected = Join-Path $repoRoot ("src\tests\expected\$base.expected")
    $stdout = Join-Path $sessionDir ("$base.output")
    $stderr = Join-Path $sessionDir ("$base.stderr")
    $displayName = $TestFile.Substring($repoRoot.Length).TrimStart('\', '/')

    Write-Host "== Running $displayName =="
    if (-not (Test-Path -LiteralPath $expected)) {
        Add-Fail "$displayName (no expected output)"
        return
    }

    $exitCode = Invoke-Vpp -Arguments @($TestFile) -StdOut $stdout -StdErr $stderr -WorkingDirectory $repoRoot -Environment $Environment
    $matches = Test-ExpectedOutput -Expected $expected -Actual $stdout
    if ($exitCode -eq 0 -and $matches) {
        Add-Pass $displayName
    } else {
        Add-Fail $displayName
        Show-FailureDetails -StdErr $stderr -ExitCode $exitCode
    }
}

function Invoke-ExpectedFailureTest {
    param([string]$TestFile)

    $base = [System.IO.Path]::GetFileNameWithoutExtension($TestFile)
    $expected = Join-Path $repoRoot ("src\tests\expected\$base.expected")
    $stdout = Join-Path $sessionDir ("$base.output")
    $stderr = Join-Path $sessionDir ("$base.stderr")
    $displayName = $TestFile.Substring($repoRoot.Length).TrimStart('\', '/')

    Write-Host "== Running $displayName [expected runtime failure] =="
    if (-not (Test-Path -LiteralPath $expected)) {
        Add-Fail "$displayName (no expected output)"
        return
    }

    $exitCode = Invoke-Vpp -Arguments @($TestFile) -StdOut $stdout -StdErr $stderr -WorkingDirectory $repoRoot
    $matches = Test-ExpectedOutput -Expected $expected -Actual $stderr
    if ($exitCode -ne 0 -and $matches) {
        Add-Pass $displayName
    } else {
        if ($exitCode -eq 0) {
            Add-Fail "$displayName (expected non-zero exit code)"
        } else {
            Add-Fail $displayName
        }
        Show-FailureDetails -StdErr $stderr -ExitCode $exitCode
    }
}

function Get-BashTestArray {
    param([string]$ArrayName)

    $runner = Join-Path $repoRoot "run_tests.sh"
    $lines = [System.IO.File]::ReadAllLines($runner, [System.Text.Encoding]::UTF8)
    $inside = $false
    $tests = [System.Collections.Generic.List[string]]::new()
    foreach ($line in $lines) {
        if (-not $inside) {
            if ($line.Trim().StartsWith("$ArrayName=(")) {
                $inside = $true
            }
            continue
        }
        if ($line.Trim() -eq ")") {
            return $tests.ToArray()
        }
        if ($line -match '(src/tests/[^\s\\]+\.vi)') {
            $tests.Add($Matches[1])
        }
    }
    throw "Không tìm thấy mảng $ArrayName hoàn chỉnh trong run_tests.sh"
}

try {
    if (-not (Test-Path -LiteralPath $script:Vpp -PathType Leaf)) {
        throw "Khong tim thay vpp-cli: $VppExecutable"
    }

    New-Item -ItemType Directory -Path $sessionDir -Force | Out-Null
    $env:VPP_HOME = $repoRoot

    # Keep the Windows UTF-8 filesystem regression independent from the HTTP
    # assertion. The source is outside the repository, so imports must use
    # VPP_HOME/gói/chuẩn.
    $probeDir = Join-Path $sessionDir "import-utf8"
    New-Item -ItemType Directory -Path $probeDir -Force | Out-Null
    $probeFile = Join-Path $probeDir "import-utf8.vi"
    [System.IO.File]::WriteAllText(
        $probeFile,
        "nhập mạng;`nnhập vào ra;`n",
        [System.Text.UTF8Encoding]::new($false)
    )
    $probeStdOut = Join-Path $sessionDir "import-utf8.output"
    $probeStdErr = Join-Path $sessionDir "import-utf8.stderr"
    $probeExit = Invoke-Vpp -Arguments @($probeFile) -StdOut $probeStdOut -StdErr $probeStdErr -WorkingDirectory $probeDir
    if ($probeExit -ne 0) {
        $probeError = Get-NormalizedUtf8Text $probeStdErr
        throw "V++ could not resolve UTF-8 bundled imports through VPP_HOME. stderr: $probeError"
    }

    # The full HTTP client tests use a local V++ fixture, never the network.
    $fixtureStdOut = Join-Path $sessionDir "http_fixture.output"
    $fixtureStdErr = Join-Path $sessionDir "http_fixture.stderr"
    $fixtureProcess = Start-VppServer -Source (Join-Path $repoRoot "src\tests\http_fixture.vi") -StdOut $fixtureStdOut -StdErr $fixtureStdErr -WorkingDirectory $repoRoot
    if (-not (Wait-ForHttp -Uri "http://127.0.0.1:18080/health" -ExpectedContent '{"ok":true}')) {
        # Start-Process keeps redirected log handles open on Windows. Stop and
        # wait for the fixture before reading its diagnostics, otherwise the
        # lock itself masks the real startup error.
        $fixturePid = $fixtureProcess.Id
        Stop-VppServer $fixtureProcess
        $fixtureExitCode = "unknown"
        try {
            $fixtureExitCode = $fixtureProcess.ExitCode
        } catch {
            # Keep the timeout diagnostic useful even if process metadata is unavailable.
        }
        $fixtureProcess = $null
        $fixtureOutput = Get-NormalizedUtf8Text $fixtureStdOut
        $fixtureError = Get-NormalizedUtf8Text $fixtureStdErr
        throw "Local HTTP test fixture did not start (pid: $fixturePid, exit code: $fixtureExitCode, probe: $script:LastHttpProbeError). stdout: $fixtureOutput stderr: $fixtureError"
    }

    Write-Host "== Running Vietnamese package import via VPP_HOME =="
    $externalVppHome = Join-Path ([System.IO.Path]::GetTempPath()) ("vpp-package-import-" + [guid]::NewGuid().ToString())
    New-Item -ItemType Directory -Path $externalVppHome -Force | Out-Null
    $externalTest = Join-Path $externalVppHome "kiem_tra_package_tieng_viet.vi"
    Copy-Item -LiteralPath (Join-Path $repoRoot "src\tests\kiem_tra_package_tieng_viet.vi") -Destination $externalTest
    Copy-Item -LiteralPath (Join-Path $repoRoot "gói") -Destination (Join-Path $externalVppHome "gói") -Recurse
    $externalStdOut = Join-Path $sessionDir "kiem_tra_package_tieng_viet_vpp_home.output"
    $externalStdErr = Join-Path $sessionDir "kiem_tra_package_tieng_viet_vpp_home.stderr"
    $externalExit = Invoke-Vpp -Arguments @($externalTest) -StdOut $externalStdOut -StdErr $externalStdErr -WorkingDirectory $externalVppHome -Environment @{ VPP_HOME = $externalVppHome }
    $externalExpected = Join-Path $repoRoot "src\tests\expected\kiem_tra_package_tieng_viet.expected"
    if ($externalExit -eq 0 -and (Test-ExpectedOutput -Expected $externalExpected -Actual $externalStdOut)) {
        Add-Pass "Vietnamese package import via VPP_HOME"
    } else {
        Add-Fail "Vietnamese package import via VPP_HOME"
        Show-FailureDetails -StdErr $externalStdErr -ExitCode $externalExit
    }

    $tests = Get-BashTestArray -ArrayName "TESTS"
    foreach ($relativeTest in $tests) {
        Invoke-ExpectedTest -TestFile (Join-Path $repoRoot $relativeTest)
    }

    $expectedFailureTests = Get-BashTestArray -ArrayName "EXPECTED_FAILURE_TESTS"
    foreach ($relativeTest in $expectedFailureTests) {
        Invoke-ExpectedFailureTest -TestFile (Join-Path $repoRoot $relativeTest)
    }

    Write-Host "== Running src/tests/kiem_tra_jit_mvp.vi [JIT] =="
    Invoke-ExpectedTest -TestFile (Join-Path $repoRoot "src\tests\kiem_tra_jit_mvp.vi") -Environment @{ VPP_ENABLE_JIT = "1" }

    Write-Host "== Running src/tests/kiem_tra_gc_mvp.vi [GC] =="
    Invoke-ExpectedTest -TestFile (Join-Path $repoRoot "src\tests\kiem_tra_gc_mvp.vi") -Environment @{ VPP_ENABLE_GC = "1"; VPP_GC_INTERVAL = "1" }

    Write-Host ""
    Write-Host "=== PASS: $script:PassCount, FAIL: $script:FailCount ==="
    if ($script:FailCount -ne 0) {
        Write-Host ("Failed tests: " + ($script:FailedTests -join ', '))
        throw "Windows V++ regression suite failed with $script:FailCount failure(s)."
    }
} finally {
    Stop-VppServer $fixtureProcess
    if ($null -eq $previousVppHome) {
        Remove-Item Env:VPP_HOME -ErrorAction SilentlyContinue
    } else {
        $env:VPP_HOME = $previousVppHome
    }
    Remove-Item -LiteralPath $sessionDir -Recurse -Force -ErrorAction SilentlyContinue
    if ($null -ne $externalVppHome) {
        Remove-Item -LiteralPath $externalVppHome -Recurse -Force -ErrorAction SilentlyContinue
    }
}
