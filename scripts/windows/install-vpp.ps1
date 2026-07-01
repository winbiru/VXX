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

$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ([string]::IsNullOrWhiteSpace($userPath)) {
    $newPath = $InstallDir
} elseif (($userPath -split ';' | ForEach-Object { $_.Trim() }) -contains $InstallDir) {
    $newPath = $userPath
} else {
    $newPath = "$userPath;$InstallDir"
}
[Environment]::SetEnvironmentVariable("Path", $newPath, "User")

$env:Path = "$env:Path;$InstallDir"

Write-Host "Da cai dat VPP tai: $targetExe"
Write-Host "Da them vao PATH (User). Hay mo PowerShell/CMD moi de dung lenh: vpp"
