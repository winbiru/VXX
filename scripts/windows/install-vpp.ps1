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
$targetStdlib = Join-Path $InstallDir "gói"
New-Item -ItemType Directory -Path $targetStdlib -Force | Out-Null
Copy-Item (Join-Path $sourceStdlib "*") $targetStdlib -Recurse -Force
$sourceTemplates = Join-Path $scriptDir "templates"
if (-not (Test-Path $sourceTemplates)) {
    throw "Khong tim thay templates tai: $sourceTemplates"
}
$targetTemplates = Join-Path $InstallDir "templates"
New-Item -ItemType Directory -Path $targetTemplates -Force | Out-Null
Copy-Item (Join-Path $sourceTemplates "*") $targetTemplates -Recurse -Force
$sourceExamples = Join-Path $scriptDir "examples"
if (-not (Test-Path $sourceExamples)) {
    throw "Khong tim thay examples tai: $sourceExamples"
}
$targetExamples = Join-Path $InstallDir "examples"
New-Item -ItemType Directory -Path $targetExamples -Force | Out-Null
Copy-Item (Join-Path $sourceExamples "*") $targetExamples -Recurse -Force

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
Write-Host "Da cai dat thu vien chuan tai: $targetStdlib"
Write-Host "Da cai dat templates tai: $targetTemplates"
Write-Host "Da cai dat examples tai: $targetExamples"
Write-Host "Da them vao PATH (User). Hay mo PowerShell/CMD moi de dung lenh: vpp"
