param(
    [Parameter(Mandatory = $true)][string]$SeedCompiler,
    [Parameter(Mandatory = $true)][string]$RuntimeObject,
    [Parameter(Mandatory = $true)][string]$OutputDirectory
)
# Run this gate on Windows itself. The seed PE and ASM boundary can be
# cross-created once; all subsequent compiler generations link using V++.
$ErrorActionPreference = 'Stop'
# Expected nonzero native exit codes are checked explicitly below.
$PSNativeCommandUseErrorActionPreference = $false
if (-not $IsWindows -or (Test-Path -LiteralPath $OutputDirectory)) {
    throw 'Windows and a new output directory are required.'
}
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$seed = (Resolve-Path -LiteralPath $SeedCompiler).Path
$runtime = (Resolve-Path -LiteralPath $RuntimeObject).Path
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$out = (Resolve-Path -LiteralPath $OutputDirectory).Path
Copy-Item -LiteralPath $runtime -Destination (Join-Path $out 'windows-runtime.obj')
$names = @('VPP_HOME', 'VPP_NATIVE_NO_CACHE', 'VPP_NATIVE_TARGET', 'VPP_WINDOWS_RUNTIME_OBJECT')
$saved = @{}
foreach ($name in $names) { $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
try {
    $env:VPP_HOME = $root.Replace('\', '/')
    $env:VPP_NATIVE_NO_CACHE = '1'
    $env:VPP_NATIVE_TARGET = 'windows-x86_64'
    $env:VPP_WINDOWS_RUNTIME_OBJECT = (Join-Path $out 'windows-runtime.obj').Replace('\', '/')
    $source = (Join-Path $root 'gói/biên dịch/bản địa/bộ biên dịch.vi').Replace('\', '/')
    $compiler = $seed
    foreach ($stage in 1..3) {
        $exe = Join-Path $out "stage$stage.exe"
        & $compiler build $source -o $exe 2>&1 | Out-File -LiteralPath (Join-Path $out "stage$stage.log")
        if ($LASTEXITCODE -ne 0) { throw "Windows native PE stage $stage exited $LASTEXITCODE" }
        # Check the actual binary produced by V++ before bootstrapping from it.
        $image = [IO.File]::ReadAllBytes($exe)
        if ($image.Length -lt 256 -or $image[0] -ne 0x4d -or $image[1] -ne 0x5a) {
            throw "Stage $stage did not emit a DOS/PE executable."
        }
        $peOffset = [BitConverter]::ToInt32($image, 0x3c)
        if ($peOffset -lt 0x40 -or $peOffset -gt $image.Length - 26 -or
            [BitConverter]::ToUInt32($image, $peOffset) -ne 0x00004550 -or
            [BitConverter]::ToUInt16($image, $peOffset + 4) -ne 0x8664 -or
            [BitConverter]::ToUInt16($image, $peOffset + 24) -ne 0x020b) {
            throw "Stage $stage is not a PE32+ AMD64 executable."
        }
        $compiler = $exe
    }
    $hashes = @(1..3 | ForEach-Object { (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $out "stage$_.exe")).Hash })
    if (($hashes | Select-Object -Unique).Count -ne 1) { throw 'Windows PE compiler stages diverged.' }
    $hashes | Out-File -LiteralPath (Join-Path $out 'sha256.txt')
    $probe = Join-Path $out 'gói thử nghiệm 𠮷.vi'
    [IO.File]::WriteAllText($probe, 'hàm chính() { trả về 52; };' + "`n", [Text.UTF8Encoding]::new($false))
    $executable = Join-Path $out 'probe.exe'
    & $compiler build $probe -o $executable
    if ($LASTEXITCODE -ne 0) { throw 'Windows native PE Unicode probe build failed.' }
    & $executable
    if ($LASTEXITCODE -ne 52) { throw 'Windows native PE Unicode probe returned the wrong value.' }
    Write-Output 'WINDOWS_NATIVE_PE_UNICODE_PROBE=PASS'

    $runtimeProbe = Join-Path $out 'runtime.vi'
    [IO.File]::WriteAllText($runtimeProbe,
        'hàm chính() { trả về máy đặt quyền tệp mở(-1,448); };' + "`n",
        [Text.UTF8Encoding]::new($false))
    $runtimeExe = Join-Path $out 'runtime-probe.exe'
    & $compiler build $runtimeProbe -o $runtimeExe
    if ($LASTEXITCODE -ne 0) { throw 'Windows native PE runtime probe build failed.' }
    & $runtimeExe
    if ($LASTEXITCODE -ne 255) { throw 'Windows native PE runtime probe returned the wrong value.' }
    Write-Output 'WINDOWS_NATIVE_PE_RUNTIME_PROBE=PASS'

    # A failed boundary link must not replace an existing executable.
    $preserved = Join-Path $out 'preserved.exe'
    $sentinel = [Text.Encoding]::UTF8.GetBytes('previous output must survive')
    [IO.File]::WriteAllBytes($preserved, $sentinel)
    $malformed = Join-Path $out 'malformed.obj'
    [IO.File]::WriteAllBytes($malformed, [Text.Encoding]::ASCII.GetBytes('bad COFF object'))
    $env:VPP_WINDOWS_RUNTIME_OBJECT = $malformed.Replace('\', '/')
    try {
        & $compiler build $runtimeProbe -o $preserved 2>&1 |
            Out-File -LiteralPath (Join-Path $out 'rejected.log')
        $rejectedExit = $LASTEXITCODE
    } finally {
        $env:VPP_WINDOWS_RUNTIME_OBJECT = (Join-Path $out 'windows-runtime.obj').Replace('\', '/')
    }
    if ($rejectedExit -ne 17) { throw "Malformed COFF boundary returned $rejectedExit instead of 17." }
    if ([Convert]::ToBase64String([IO.File]::ReadAllBytes($preserved)) -ne
        [Convert]::ToBase64String($sentinel)) {
        throw 'The rejected PE link overwrote an existing executable.'
    }
    $tempFiles = @(Get-ChildItem -LiteralPath $out -Recurse -File | Where-Object {
        $_.Name -like '*.vpp-exe-*' -or $_.Name -like '*.vpp-module-*'
    })
    if ($tempFiles.Count -gt 0) { throw 'Native PE linker left temporary build files.' }
    Write-Output 'WINDOWS_NATIVE_PE_ATOMIC_FAILURE=PASS'
    Write-Output 'WINDOWS_NATIVE_PE_SELFHOST_EXACT_CMP=PASS'
    Write-Output 'WINDOWS_NATIVE_PE_SELFHOST=PASS (native Windows)'
} finally {
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $saved[$name], 'Process') }
}
