param([Parameter(Mandatory=$true)][string]$Root)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
function Get-VppSha256([string]$Path) {
    # Windows PowerShell may run without the Get-FileHash cmdlet available.
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $stream = [System.IO.File]::OpenRead($Path)
        try {
            return [System.BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '')
        } finally {
            $stream.Dispose()
        }
    } finally {
        $sha.Dispose()
    }
}
$Root = (Get-Item -LiteralPath $Root).FullName.TrimEnd([char[]]'\/')
$Manifest = Join-Path $Root 'vpp-install-manifest.tsv'
if (-not (Test-Path -LiteralPath $Manifest -PathType Leaf) -or ((Get-Item -LiteralPath $Manifest -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Missing V++ installation manifest' }
$Entries = @()
$Seen = @{}
foreach ($line in [IO.File]::ReadAllLines($Manifest, [Text.Encoding]::UTF8)) {
    $fields = $line.Split("`t")
    if ($fields.Count -ne 2 -or $fields[0] -cnotmatch '^[0-9a-f]{64}$' -or $fields[1] -match '(^/|\\|:|(^|/)\.\.?(/|$)|//|/$)' -or -not $fields[1] -or $fields[1] -ieq 'vpp-install-manifest.tsv') { throw 'Invalid V++ installation manifest' }
    if ($Seen.ContainsKey($fields[1])) { throw "Duplicate V++ installation path" }
    $Seen[$fields[1]] = $true
    $target = $Root
    foreach ($component in $fields[1].Split('/')) {
        if ($component -match '[. ]$|[<>:"|?*]' -or $component -match '^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\.|$)') { throw "Invalid Windows installation path" }
        $target = Join-Path $target $component
        if (($item = Get-Item -LiteralPath $target -Force -ErrorAction SilentlyContinue) -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Refusing to remove through a link: $target" }
    }
    $Entries += [pscustomobject]@{ Path = $target; Hash = $fields[0] }
}
if (-not $Entries.Count) { throw 'Empty V++ installation manifest' }
foreach ($entry in $Entries) {
    if (-not (Test-Path -LiteralPath $entry.Path)) { continue }
    if (-not (Test-Path -LiteralPath $entry.Path -PathType Leaf) -or (Get-VppSha256 $entry.Path) -ine $entry.Hash) {
        Write-Output "Keep modified file: $($entry.Path)"; continue
    }
    Remove-Item -LiteralPath $entry.Path -Force
}
foreach ($entry in $Entries) {
    $parent = Split-Path -Parent $entry.Path
    while ($parent -ine $Root) {
        if (-not (Test-Path -LiteralPath $parent -PathType Container)) { break }
        if (@(Get-ChildItem -LiteralPath $parent -Force).Count) { break }
        Remove-Item -LiteralPath $parent -Force
        $parent = Split-Path -Parent $parent
    }
}
Remove-Item -LiteralPath $Manifest -Force
Write-Output "Removed installed V++ files: $Root"
