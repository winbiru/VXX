param(
    [Parameter(Mandatory=$true)]
    [ValidateSet('install','check','remove')]
    [string]$Mode
)
$ErrorActionPreference = 'Stop'
# Windows PowerShell 5.1 needs BOM for Vietnamese script/profile source.
$utf8 = [System.Text.UTF8Encoding]::new($false)
$utf8Bom = [System.Text.UTF8Encoding]::new($true)
[Console]::InputEncoding = $utf8
[Console]::OutputEncoding = $utf8
$global:OutputEncoding = $utf8
$start = '# >>> VDK UTF-8 >>>'
$end = '# <<< VDK UTF-8 <<<'
# Override used by isolated packaging tests; otherwise use the actual Documents folder.
$documents = $env:VDK_DISPLAY_PROFILE_ROOT
if (-not $documents) { $documents = [Environment]::GetFolderPath('MyDocuments') }
if (-not $documents) { throw 'Không tìm thấy thư mục Documents của người dùng.' }
$profiles = @(
    (Join-Path $documents 'WindowsPowerShell\profile.ps1'),
    (Join-Path $documents 'PowerShell\profile.ps1')
)
$block = @'
# >>> VDK UTF-8 >>>
try {
    $VdkUtf8 = [System.Text.UTF8Encoding]::new($false)
    [Console]::InputEncoding = $VdkUtf8
    [Console]::OutputEncoding = $VdkUtf8
    $global:OutputEncoding = $VdkUtf8
    Remove-Variable VdkUtf8 -ErrorAction SilentlyContinue
} catch { }
# <<< VDK UTF-8 <<<
'@
function Read-ProfileText([string]$Path) {
    # BOM-aware, strict UTF-8. Fall back to the Windows ANSI code page for legacy profiles.
    try { return [IO.File]::ReadAllText($Path, [Text.UTF8Encoding]::new($false,$true)) }
    catch [Text.DecoderFallbackException] {
        return [IO.File]::ReadAllText($Path, [Text.Encoding]::GetEncoding([Globalization.CultureInfo]::CurrentCulture.TextInfo.ANSICodePage))
    }
}
$pattern = '(?ms)^' + [regex]::Escape($start) + '\r?$.*?^' + [regex]::Escape($end) + '\r?$(?:\r?\n)?'
try {
    foreach ($profilePath in $profiles) {
        $exists = Test-Path -LiteralPath $profilePath -PathType Leaf
        $content = ''
        if ($exists) {
            $item = Get-Item -LiteralPath $profilePath -Force
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Không sửa profile là liên kết: $profilePath" }
            $content = Read-ProfileText $profilePath
        }
        $starts = [regex]::Matches($content, '(?m)^' + [regex]::Escape($start) + '\r?$').Count
        $ends = [regex]::Matches($content, '(?m)^' + [regex]::Escape($end) + '\r?$').Count
        $matches = [regex]::Matches($content, $pattern).Count
        if ($starts -ne $ends -or $starts -ne $matches -or $starts -gt 1) { throw "Khối cấu hình không hợp lệ: $profilePath" }
        if ($Mode -eq 'check') {
            if ($matches -eq 1) { Write-Output "Đã cài: $profilePath" }
            else { Write-Output "Chưa cài: $profilePath" }
            continue
        }
        if ($Mode -eq 'remove' -and $matches -eq 0) { continue }
        $updated = [regex]::Replace($content, $pattern, '')
        if ($Mode -eq 'install') {
            if ($updated.Length -gt 0 -and -not $updated.EndsWith("`n")) { $updated += [Environment]::NewLine }
            $updated += $block + [Environment]::NewLine
        }
        if ($updated -ne $content -or -not $exists) {
            [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($profilePath)) | Out-Null
            [IO.File]::WriteAllText($profilePath, $updated, $utf8Bom)
            Write-Output "Đã cập nhật: $profilePath"
        }
    }
    if ($Mode -eq 'check') {
        Write-Output "Bảng mã nhập/xuất: $([Console]::InputEncoding.CodePage)/$([Console]::OutputEncoding.CodePage)"
        Write-Output 'Tiếng Việt: Trường đại học — Nguyễn, Đắk Lắk, chữ ă â ê ô ơ ư đ.'
    } elseif ($Mode -eq 'install') {
        Write-Output 'Đã cài UTF-8 cho PowerShell 5.1 và PowerShell 7. Mở terminal mới để áp dụng.'
    } else {
        Write-Output 'Đã gỡ cấu hình UTF-8 do VDK quản lý. Mở terminal mới để áp dụng.'
    }
} catch {
    [Console]::Error.WriteLine("VDK: " + $_.Exception.Message)
    exit 1
}
