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
$buildClock = [Diagnostics.Stopwatch]::StartNew()
function Write-BuildProgress([int]$Percent, [string]$Message) {
    Write-Host ('[Biên dịch V++ Windows i32 {0,3}% | {1}s] {2}' -f $Percent, [int]$buildClock.Elapsed.TotalSeconds, $Message)
}
if (-not $IsWindows -or (Test-Path -LiteralPath $OutputDirectory)) {
    throw 'Windows and a new output directory are required.'
}
Write-BuildProgress 0 'Chuẩn bị bộ biên dịch khởi tạo và đối tượng COFF'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$seed = (Resolve-Path -LiteralPath $SeedCompiler).Path
$runtime = (Resolve-Path -LiteralPath $RuntimeObject).Path
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$out = (Resolve-Path -LiteralPath $OutputDirectory).Path
Copy-Item -LiteralPath $runtime -Destination (Join-Path $out 'windows-runtime.obj')
$names = @('VPP_HOME', 'VPP_NATIVE_NO_CACHE', 'VPP_NATIVE_TARGET', 'VPP_WINDOWS_RUNTIME_OBJECT', 'VPP_NATIVE_BUILD_PROGRESS')
$saved = @{}
foreach ($name in $names) { $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
try {
    $env:VPP_HOME = $root.Replace([char]92, [char]47)
    $env:VPP_NATIVE_NO_CACHE = '1'
    $env:VPP_NATIVE_TARGET = 'windows-x86_64'
    $env:VPP_WINDOWS_RUNTIME_OBJECT = (Join-Path $out 'windows-runtime.obj').Replace([char]92, [char]47)
    $source = (Join-Path $root 'gói/biên dịch/bản địa/bộ biên dịch.vi').Replace([char]92, [char]47)
    $compiler = $seed
    foreach ($stage in 1..3) {
        Write-BuildProgress (15 + ($stage - 1) * 20) "Tự biên dịch bộ biên dịch V++ giai đoạn $stage/3"
        $exe = Join-Path $out "stage$stage.exe"
        $env:VPP_NATIVE_BUILD_PROGRESS = '1'
        & $compiler build $source -o $exe 2>&1 | Tee-Object -FilePath (Join-Path $out "stage$stage.log") | ForEach-Object { Write-Host ('[{0}s] {1}' -f [int]$buildClock.Elapsed.TotalSeconds, $_) }
        $env:VPP_NATIVE_BUILD_PROGRESS = '0'
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
    Write-BuildProgress 75 'Kiểm tra tính đồng nhất của ba thế hệ bộ biên dịch và đường dẫn mô-đun'
    $hashes = @(1..3 | ForEach-Object { (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $out "stage$_.exe")).Hash })
    if (($hashes | Select-Object -Unique).Count -ne 1) { throw 'Windows PE compiler stages diverged.' }
    $hashes | Out-File -LiteralPath (Join-Path $out 'sha256.txt')
    Push-Location -LiteralPath $root
    try {
        foreach ($relativeSource in @('gói/biên dịch/bản địa/bộ biên dịch.vi', '.\gói\biên dịch\bản địa\bộ biên dịch.vi')) {
            & $compiler check $relativeSource
            if ($LASTEXITCODE -ne 0) { throw "Windows relative module path check failed: $LASTEXITCODE" }
        }
    } finally {
        Pop-Location
    }
    Write-Output 'WINDOWS_NATIVE_PE_RELATIVE_MODULE_PATH=PASS'
    Write-BuildProgress 85 'Kiểm tra Unicode và bộ thực thi Windows'
    $probe = Join-Path $out 'gói thử nghiệm 𠮷.vi'
    [IO.File]::WriteAllText($probe, 'hàm chính() { trả về 52; };' + "`n", [Text.UTF8Encoding]::new($false))
    $executable = Join-Path $out 'probe.exe'
    & $compiler build $probe -o $executable
    if ($LASTEXITCODE -ne 0) { throw 'Windows native PE Unicode probe build failed.' }
    & $executable
    if ($LASTEXITCODE -ne 52) { throw 'Windows native PE Unicode probe returned the wrong value.' }
    Write-Output 'WINDOWS_NATIVE_PE_UNICODE_PROBE=PASS'

    $runtimeProbe = Join-Path $out 'runtime.vi'
    # Check the signed i32 result inside V++. Windows preserves the full
    # 32-bit process exit code, whereas a POSIX shell truncates -1 to 255.
    [IO.File]::WriteAllText($runtimeProbe,
        'hàm chính() { nếu (máy đặt quyền tệp mở(-1,448) == -1) { trả về 73; }; trả về 1; };' + "`n",
        [Text.UTF8Encoding]::new($false))
    $runtimeExe = Join-Path $out 'runtime-probe.exe'
    & $compiler build $runtimeProbe -o $runtimeExe
    if ($LASTEXITCODE -ne 0) { throw 'Windows native PE runtime probe build failed.' }
    & $runtimeExe
    if ($LASTEXITCODE -ne 73) { throw "Windows native PE runtime probe returned $LASTEXITCODE instead of 73 (expected fchmod result -1)." }
    Write-Output 'WINDOWS_NATIVE_PE_RUNTIME_PROBE=PASS'
    Write-BuildProgress 90 'Kiểm tra bảo toàn tệp khi liên kết thất bại'

    # A failed boundary link must not replace an existing executable.
    $preserved = Join-Path $out 'preserved.exe'
    $sentinel = [Text.Encoding]::UTF8.GetBytes('previous output must survive')
    [IO.File]::WriteAllBytes($preserved, $sentinel)
    $malformed = Join-Path $out 'malformed.obj'
    [IO.File]::WriteAllBytes($malformed, [Text.Encoding]::ASCII.GetBytes('bad COFF object'))
    $env:VPP_WINDOWS_RUNTIME_OBJECT = $malformed.Replace([char]92, [char]47)
    try {
        & $compiler build $runtimeProbe -o $preserved 2>&1 |
            Out-File -LiteralPath (Join-Path $out 'rejected.log')
        $rejectedExit = $LASTEXITCODE
    } finally {
        $env:VPP_WINDOWS_RUNTIME_OBJECT = (Join-Path $out 'windows-runtime.obj').Replace([char]92, [char]47)
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
Write-BuildProgress 100 'Tự biên dịch V++ i32 hoàn tất'
# The rejected COFF probe intentionally exits 17. GitHub Actions propagates
# $LASTEXITCODE even after all checks pass, so explicitly report success.
exit 0
