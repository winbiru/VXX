param(
    [string]$InstallDir = "$env:LOCALAPPDATA\Programs\VPP",
    [switch]$NoPathUpdate,
    [int]$WaitForProcessId = 0,
    [switch]$RemoveSelf
)

$ErrorActionPreference = "Stop"
$VppExecutableName = 'vpp.exe'
$StdlibDirectoryName = 'gói'
$TemplatesDirectoryName = 'templates'
$ExamplesDirectoryName = 'examples'
$UninstallScriptName = 'uninstall-vpp.ps1'
$UninstallLauncherName = 'vpp-uninstall.cmd'
$ManagedEntries = @(
    $VppExecutableName,
    $StdlibDirectoryName,
    $TemplatesDirectoryName,
    $ExamplesDirectoryName,
    $UninstallLauncherName,
    $UninstallScriptName
)
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
$global:OutputEncoding = [Console]::OutputEncoding
$InstallDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($InstallDir)
if ($WaitForProcessId -gt 0) {
    Wait-Process -Id $WaitForProcessId -ErrorAction SilentlyContinue
}
if ($RemoveSelf) {
    Remove-Item -LiteralPath $PSCommandPath -Force
}

function Normalize-VppPath([string]$Value) {
    return [Environment]::ExpandEnvironmentVariables($Value.Trim().Trim('"')).TrimEnd([char[]]'\/')
}

function Remove-VppFromPath([string]$Value) {
    return (($Value -split ';' | Where-Object {
        $_ -and (Normalize-VppPath $_) -ine (Normalize-VppPath $InstallDir)
    }) -join ';')
}

# Only remove files/directories managed by the installer; keep user projects.
# Refuse unrelated directories when an installation is not identifiable.
if (Test-Path -LiteralPath $InstallDir) {
    if (-not (Test-Path -LiteralPath (Join-Path $InstallDir $VppExecutableName)) -and
        -not (Test-Path -LiteralPath (Join-Path $InstallDir $UninstallScriptName))) {
        throw "Không tìm thấy bản cài V++ tại: $InstallDir"
    }
    foreach ($name in $ManagedEntries) {
        $target = Join-Path $InstallDir $name
        if (Test-Path -LiteralPath $target) {
            Remove-Item -LiteralPath $target -Recurse -Force
        }
    }
    if (@(Get-ChildItem -LiteralPath $InstallDir -Force).Count -eq 0) {
        Remove-Item -LiteralPath $InstallDir -Force
    }
}

if (-not $NoPathUpdate) {
    $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
    [Environment]::SetEnvironmentVariable('Path', (Remove-VppFromPath $userPath), 'User')
    $userHome = [Environment]::GetEnvironmentVariable('VPP_HOME', 'User')
    if ($userHome -and (Normalize-VppPath $userHome) -ieq (Normalize-VppPath $InstallDir)) {
        [Environment]::SetEnvironmentVariable('VPP_HOME', $null, 'User')
    }
}
$env:Path = Remove-VppFromPath $env:Path
if ($env:VPP_HOME -and (Normalize-VppPath $env:VPP_HOME) -ieq (Normalize-VppPath $InstallDir)) {
    Remove-Item Env:\VPP_HOME
}
Write-Host "Đã gỡ V++ tại: $InstallDir"
Write-Host "Hãy đóng và mở lại terminal/VS Code để nhận môi trường mới."
