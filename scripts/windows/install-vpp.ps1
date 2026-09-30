param(
    [string]$InstallDir = "$env:LOCALAPPDATA\Programs\VPP",
    [switch]$NoPathUpdate
)

$ErrorActionPreference = "Stop"

# Configure UTF-8 before invoking V++ or printing Vietnamese installer messages.
# A headless host may have no console for chcp; redirected output still uses UTF-8.
try {
    & "$env:SystemRoot\System32\chcp.com" 65001 2>$null | Out-Null
} catch {
    Write-Verbose "No console code page available; configuring UTF-8 streams only."
}
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
# Script-local assignment would be lost when returning to the calling shell.
$global:OutputEncoding = [Console]::OutputEncoding

# Persist an absolute path even when -InstallDir is relative.
$InstallDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($InstallDir)

function Add-VppToPath([string]$ExistingPath, [string]$Directory) {
    # Prefer this installation and remove equivalent entries on repeated installs.
    $entries = @($ExistingPath -split ';' | Where-Object {
        $entry = [Environment]::ExpandEnvironmentVariables($_.Trim().Trim('"')).TrimEnd([char[]]'\/')
        $entry -and $entry -ine $Directory.TrimEnd([char[]]'\/')
    })
    return (@($Directory) + $entries) -join ';'
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$sourceExe = Join-Path $scriptDir "vpp.exe"
if (-not (Test-Path $sourceExe)) {
    throw "Không tìm thấy vpp.exe trong thư mục giải nén: $scriptDir"
}

# Fail before replacing an existing installation when the executable cannot load.
& $sourceExe "phiên" "bản"
if ($LASTEXITCODE -ne 0) {
    throw "Không thể chạy vpp.exe (mã lỗi: $LASTEXITCODE). Hãy tải bản Windows mới nhất. Với bản cũ bị lỗi 0xC0000135, cài Visual C++ Runtime x64: https://aka.ms/vc14/vc_redist.x64.exe"
}

$sourceStdlib = Join-Path $scriptDir "gói"
if (-not (Test-Path $sourceStdlib)) {
    throw "Không tìm thấy thư viện chuẩn tại: $sourceStdlib"
}
$sourceTemplates = Join-Path $scriptDir "templates"
if (-not (Test-Path $sourceTemplates)) {
    throw "Không tìm thấy templates tại: $sourceTemplates"
}
$sourceExamples = Join-Path $scriptDir "examples"
if (-not (Test-Path $sourceExamples)) {
    throw "Không tìm thấy examples tại: $sourceExamples"
}

New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
$stagingDir = Join-Path $InstallDir ".vpp-install-$PID"
$targetExe = Join-Path $InstallDir "vpp.exe"
$targetStdlib = Join-Path $InstallDir "gói"
$targetTemplates = Join-Path $InstallDir "templates"
$targetExamples = Join-Path $InstallDir "examples"

try {
    Remove-Item $stagingDir -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Path $stagingDir -Force | Out-Null
    Copy-Item $sourceExe (Join-Path $stagingDir "vpp.exe") -Force
    Copy-Item $sourceStdlib (Join-Path $stagingDir "gói") -Recurse
    Copy-Item $sourceTemplates (Join-Path $stagingDir "templates") -Recurse
    Copy-Item $sourceExamples (Join-Path $stagingDir "examples") -Recurse

    Copy-Item (Join-Path $stagingDir "vpp.exe") $targetExe -Force
    foreach ($name in @('uninstall-vpp.ps1', 'vpp-uninstall.cmd')) {
        Copy-Item (Join-Path $scriptDir $name) (Join-Path $InstallDir $name) -Force
    }
    foreach ($managedDir in @("gói", "templates", "examples")) {
        $target = Join-Path $InstallDir $managedDir
        Remove-Item $target -Recurse -Force -ErrorAction SilentlyContinue
        Move-Item (Join-Path $stagingDir $managedDir) $target
    }
} finally {
    Remove-Item $stagingDir -Recurse -Force -ErrorAction SilentlyContinue
}

if (-not $NoPathUpdate) {
    $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
    $newPath = Add-VppToPath $userPath $InstallDir
    [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
    [Environment]::SetEnvironmentVariable("VPP_HOME", $InstallDir, "User")
}

$env:Path = Add-VppToPath $env:Path $InstallDir
$env:VPP_HOME = $InstallDir

Write-Host "Đã cài đặt/cập nhật V++ tại: $targetExe"
Write-Host "Đã cài thư viện chuẩn tại: $targetStdlib"
Write-Host "Đã cài templates tại: $targetTemplates"
Write-Host "Đã cài examples tại: $targetExamples"
if ($NoPathUpdate) {
    Write-Host "Đã bỏ qua cập nhật PATH/VPP_HOME của User theo -NoPathUpdate."
} else {
    Write-Host "Đã lưu PATH/VPP_HOME (User) và cập nhật môi trường của tiến trình PowerShell đang chạy bộ cài."
    Write-Host "Nếu gọi trực tiếp install-vpp.ps1 trong PowerShell, bạn có thể dùng ngay: vpp"
    Write-Host "Nếu cài qua .cmd hoặc tiến trình con, hãy đóng và mở lại ứng dụng terminal/VS Code để nhận môi trường mới."
}
