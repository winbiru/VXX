param(
    [string]$InstallDir = "$env:LOCALAPPDATA\Programs\VPP",
    [switch]$NoPathUpdate
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$sourceExe = Join-Path $scriptDir "vpp.exe"
if (-not (Test-Path $sourceExe)) {
    throw "Không tìm thấy vpp.exe trong thư mục giải nén: $scriptDir"
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
    if ([string]::IsNullOrWhiteSpace($userPath)) {
        $newPath = $InstallDir
    } elseif (($userPath -split ';' | ForEach-Object { $_.Trim() }) -contains $InstallDir) {
        $newPath = $userPath
    } else {
        $newPath = "$userPath;$InstallDir"
    }
    [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
    [Environment]::SetEnvironmentVariable("VPP_HOME", $InstallDir, "User")
}

if (($env:Path -split ';') -notcontains $InstallDir) {
    $env:Path = "$env:Path;$InstallDir"
}
$env:VPP_HOME = $InstallDir

Write-Host "Đã cài đặt/cập nhật V++ tại: $targetExe"
Write-Host "Đã cài thư viện chuẩn tại: $targetStdlib"
Write-Host "Đã cài templates tại: $targetTemplates"
Write-Host "Đã cài examples tại: $targetExamples"
if ($NoPathUpdate) {
    Write-Host "Đã bỏ qua cập nhật PATH/VPP_HOME của User theo -NoPathUpdate."
} else {
    Write-Host "Đã thêm vào PATH và cấu hình VPP_HOME (User). Hãy mở PowerShell/CMD mới để dùng lệnh: vpp"
}
