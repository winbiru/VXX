param(
    [string]$BundleDir = "dist",
    [string]$WorkRoot = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($WorkRoot)) {
    $WorkRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("vpp-release-smoke-" + [guid]::NewGuid().ToString("N"))
}
$bundle = (Resolve-Path $BundleDir).Path
$installDir = Join-Path $WorkRoot "install"
$projectRoot = Join-Path $WorkRoot "project"

function Invoke-Vpp {
    param(
        [string]$WorkingDirectory,
        [string[]]$Arguments
    )
    Push-Location $WorkingDirectory
    try {
        & $script:VppExe @Arguments
        if ($LASTEXITCODE -ne 0) {
            throw "vpp $($Arguments -join ' ') thoát với mã $LASTEXITCODE"
        }
    } finally {
        Pop-Location
    }
}

try {
    New-Item -ItemType Directory -Path $projectRoot -Force | Out-Null
    & (Join-Path $bundle "install-vpp.ps1") -InstallDir $installDir -NoPathUpdate

    $script:VppExe = Join-Path $installDir "vpp.exe"
    $env:VPP_HOME = $installDir
    Invoke-Vpp -WorkingDirectory $projectRoot -Arguments @("phiên", "bản")

    $stale = Join-Path $installDir "templates/.stale-from-old-release"
    Set-Content -Path $stale -Value "stale"
    & (Join-Path $bundle "install-vpp.ps1") -InstallDir $installDir -NoPathUpdate
    if (Test-Path $stale) {
        throw "Update vẫn để lại file stale từ release cũ."
    }

    Invoke-Vpp -WorkingDirectory $projectRoot -Arguments @("khởi", "tạo", "ứng", "dụng", "smoke-app")

    $appRoot = Join-Path $projectRoot "smoke-app"
    Invoke-Vpp -WorkingDirectory $appRoot -Arguments @("dựng", "src/main.vi")
    Invoke-Vpp -WorkingDirectory $appRoot -Arguments @("chạy", "src/main.vi")
    Invoke-Vpp -WorkingDirectory $appRoot -Arguments @("kiểm", "thử", "tests")

    $sampleRoot = Join-Path $installDir "examples/hoa-don-cua-hang"
    Invoke-Vpp -WorkingDirectory $sampleRoot -Arguments @("dựng", "src/main.vi")
    Invoke-Vpp -WorkingDirectory $sampleRoot -Arguments @("kiểm", "thử", "tests")
    Write-Host "Release install smoke passed."
} finally {
    Remove-Item $WorkRoot -Recurse -Force -ErrorAction SilentlyContinue
}
