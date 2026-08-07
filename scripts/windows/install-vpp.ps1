param(
    [string]$InstallDir = "$env:LOCALAPPDATA\Programs\VPP"
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$sourceExe = Join-Path $scriptDir "vpp.exe"
if (-not (Test-Path $sourceExe)) {
    throw "Khong tim thay vpp.exe trong thu muc giai nen: $scriptDir"
}

New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
$targetExe = Join-Path $InstallDir "vpp.exe"
Copy-Item $sourceExe $targetExe -Force
$sourceStdlib = Join-Path $scriptDir "gói"
if (-not (Test-Path $sourceStdlib)) {
    throw "Khong tim thay thu vien chuan tai: $sourceStdlib"
}
Copy-Item $sourceStdlib (Join-Path $InstallDir "gói") -Recurse -Force

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

$env:Path = "$env:Path;$InstallDir"
$env:VPP_HOME = $InstallDir

Write-Host "Da cai dat VPP tai: $targetExe"
Write-Host "Da cai dat thu vien chuan tai: $(Join-Path $InstallDir 'gói')"
Write-Host "Da them vao PATH (User). Hay mo PowerShell/CMD moi de dung lenh: vpp"
