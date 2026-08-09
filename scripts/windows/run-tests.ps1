param(
    [Parameter(Mandatory = $true)]
    [string]$VppExecutable
)

# Full Windows counterpart to run_tests.sh.  Keep the explicit list below in
# sync with that script: it deliberately excludes the fixture, a legacy test
# without an expected output, and the manual test that calls the public web.
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$script:Vpp = (Resolve-Path -LiteralPath $VppExecutable).Path
$script:PassCount = 0
$script:FailCount = 0
$script:LastHttpProbeError = ""
$sessionDir = Join-Path (Join-Path $repoRoot "src\tests\.tmp") ("windows-" + [guid]::NewGuid().ToString())
$externalVppHome = $null
$fixtureProcess = $null
$backendProcess = $null
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
        [int]$Attempts = 50
    )

    $script:LastHttpProbeError = ""
    for ($attempt = 0; $attempt -lt $Attempts; $attempt++) {
        try {
            # Local fixture checks must not inherit a corporate/runner proxy.
            $response = Invoke-WebRequest -Uri $Uri -UseBasicParsing -NoProxy -TimeoutSec 1
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

try {
    if (-not (Test-Path -LiteralPath $script:Vpp -PathType Leaf)) {
        throw "Khong tim thay vpp-cli: $VppExecutable"
    }

    New-Item -ItemType Directory -Path $sessionDir -Force | Out-Null
    $env:VPP_HOME = $repoRoot

    # Keep the Windows UTF-8 filesystem regression independent from the HTTP
    # assertion. The source is outside the repository, so imports must use
    # VPP_HOME/gói/thư viện.
    $probeDir = Join-Path $sessionDir "import-utf8"
    New-Item -ItemType Directory -Path $probeDir -Force | Out-Null
    $probeFile = Join-Path $probeDir "import-utf8.vi"
    [System.IO.File]::WriteAllText(
        $probeFile,
        "nhập mạng;`nnhập `"vào ra`";`n",
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

    # This helper also checks the native Winsock adapter and the UTF-8 import
    # path used by an installed project.
    Write-Host "== Running examples/api_project/application.vi [example] =="
    try {
        & (Join-Path $PSScriptRoot "test-http-server.ps1") -VppExecutable $script:Vpp
        Add-Pass "examples/api_project/application.vi [example]"
    } catch {
        Add-Fail "examples/api_project/application.vi [example]"
        Write-Host $_.Exception.Message
    }

    Write-Host "== Running backend scaffold smoke test =="
    $scaffoldRoot = Join-Path $sessionDir "backend"
    New-Item -ItemType Directory -Path $scaffoldRoot -Force | Out-Null
    $scaffoldStdOut = Join-Path $sessionDir "backend-init.output"
    $scaffoldStdErr = Join-Path $sessionDir "backend-init.stderr"
    $backendStdOut = $null
    $backendStdErr = $null
    $backendProbeError = ""
    $backendFailureReason = ""
    $scaffoldExit = Invoke-Vpp -Arguments @("init", "backend", "demo-api") -StdOut $scaffoldStdOut -StdErr $scaffoldStdErr -WorkingDirectory $scaffoldRoot
    $scaffoldProject = Join-Path $scaffoldRoot "demo-api"
    $properties = Join-Path $scaffoldProject "application.properties"
    $scaffoldOk = $scaffoldExit -eq 0
    foreach ($required in @("application.vi", "application.properties", "README.md", ".gitignore")) {
        $scaffoldOk = $scaffoldOk -and (Test-Path -LiteralPath (Join-Path $scaffoldProject $required))
    }
    if ($scaffoldOk) {
        $propertiesText = [System.IO.File]::ReadAllText($properties, [System.Text.UTF8Encoding]::new($false))
        # `$` in multiline regex sits before `\n`, not before a preceding
        # CRLF `\r`; accept both checkout line endings before changing the port.
        $updatedPropertiesText = [regex]::Replace($propertiesText, "(?m)^port=8080\r?$", "port=18081")
        if ($updatedPropertiesText -notmatch "(?m)^port=18081\r?$") {
            $scaffoldOk = $false
            $backendFailureReason = "Could not change the scaffold server port to 18081."
        } else {
            [System.IO.File]::WriteAllText($properties, $updatedPropertiesText, [System.Text.UTF8Encoding]::new($false))
            $backendStdOut = Join-Path $sessionDir "backend-server.output"
            $backendStdErr = Join-Path $sessionDir "backend-server.stderr"
            $backendProcess = Start-VppServer -Source (Join-Path $scaffoldProject "application.vi") -StdOut $backendStdOut -StdErr $backendStdErr -WorkingDirectory $scaffoldProject
            $scaffoldOk = Wait-ForHttp -Uri "http://127.0.0.1:18081/health" -ExpectedContent "true"
            $backendProbeError = $script:LastHttpProbeError
            Stop-VppServer $backendProcess
            $backendProcess = $null
        }
    }
    if ($scaffoldOk) {
        Add-Pass "backend scaffold"
    } else {
        Add-Fail "backend scaffold"
        if ($scaffoldExit -ne 0) {
            Show-FailureDetails -StdErr $scaffoldStdErr -ExitCode $scaffoldExit
        } elseif (-not [string]::IsNullOrWhiteSpace($backendFailureReason)) {
            Write-Host "  $backendFailureReason"
        } else {
            Write-Host "  health probe: $backendProbeError"
            $backendOutput = Get-NormalizedUtf8Text $backendStdOut
            $backendError = Get-NormalizedUtf8Text $backendStdErr
            if (-not [string]::IsNullOrWhiteSpace($backendOutput)) {
                Write-Host "  stdout: $backendOutput"
            }
            if (-not [string]::IsNullOrWhiteSpace($backendError)) {
                Write-Host "  stderr: $backendError"
            }
        }
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

    $tests = @(
        "src/tests/import_main.vi",
        "src/tests/kiem_tra_boolean.vi",
        "src/tests/kiem_tra_bo_qua.vi",
        "src/tests/kiem_tra_chon_ca.vi",
        "src/tests/kiem_tra_de_quy.vi",
        "src/tests/kiem_tra_ngoai_le.vi",
        "src/tests/kiem_tra_dieu_kien_long_nhieu_cap.vi",
        "src/tests/kiem_tra_dieu_kien_phu_dinh.vi",
        "src/tests/kiem_tra_ham_4_tham_so.vi",
        "src/tests/kiem_tra_ham_tham_so.vi",
        "src/tests/kiem_tra_ham_da_tu_khong_nhay.vi",
        "src/tests/kiem_tra_mang_3_chieu.vi",
        "src/tests/kiem_tra_noi_chuoi.vi",
        "src/tests/kiem_tra_so_chan_1-20.vi",
        "src/tests/kiem_tra_so_chia_het_cho_3_va_4.vi",
        "src/tests/kiem_tra_so_le_chia_het_cho_5.vi",
        "src/tests/kiem_tra_so_nguyen.vi",
        "src/tests/kiem_tra_so_thuc.vi",
        "src/tests/kiem_tra_tong_hop_khong_xung_dot.vi",
        "src/tests/kiem_tra_rong_va_map.vi",
        "src/tests/kiem_tra_namespace_module.vi",
        "src/tests/kiem_tra_package_modules.vi",
        "src/tests/kiem_tra_package_tieng_viet.vi",
        "src/tests/kiem_tra_stdlib.vi",
        "src/tests/kiem_tra_stdlib_starter.vi",
        "src/tests/kiem_tra_stdlib_http.vi",
        "src/tests/kiem_tra_stdlib_http_post_put.vi",
        "src/tests/kiem_tra_application_server.vi",
        "src/tests/kiem_tra_rest_json_jwt.vi",
        "src/tests/kiem_tra_stdlib_tinh_toan.vi",
        "src/tests/kiem_tra_stdlib_mo_rong.vi",
        "src/tests/kiem_tra_thu_vien_kiem_thu.vi",
        "src/tests/kiem_tra_json_an_toan.vi",
        "src/tests/kiem_tra_stdlib_io_config_time.vi",
        "src/tests/kiem_tra_api_thuc_thu.vi",
        "src/tests/kiem_tra_api_db_project.vi",
        "src/tests/kiem_tra_thu_vien_spring.vi",
        "src/tests/kiem_tra_thu_vien_lop.vi",
        "src/tests/kiem_tra_lambda_hof_mac_dinh.vi",
        "src/tests/kiem_tra_toan_tu_moi.vi",
        "src/tests/kiem_tra_lop_truy_cap.vi",
        "src/tests/kiem_tra_cu_phap_modifier_cu.vi",
        "src/tests/kiem_tra_tra_ve.vi",
        "src/tests/program.vi"
    )
    foreach ($relativeTest in $tests) {
        Invoke-ExpectedTest -TestFile (Join-Path $repoRoot $relativeTest)
    }

    Write-Host "== Running src/tests/kiem_tra_jit_mvp.vi [JIT] =="
    Invoke-ExpectedTest -TestFile (Join-Path $repoRoot "src\tests\kiem_tra_jit_mvp.vi") -Environment @{ VPP_ENABLE_JIT = "1" }

    Write-Host "== Running src/tests/kiem_tra_gc_mvp.vi [GC] =="
    Invoke-ExpectedTest -TestFile (Join-Path $repoRoot "src\tests\kiem_tra_gc_mvp.vi") -Environment @{ VPP_ENABLE_GC = "1"; VPP_GC_INTERVAL = "1" }

    Write-Host ""
    Write-Host "=== PASS: $script:PassCount, FAIL: $script:FailCount ==="
    if ($script:FailCount -ne 0) {
        throw "Windows V++ regression suite failed with $script:FailCount failure(s)."
    }
} finally {
    Stop-VppServer $backendProcess
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
