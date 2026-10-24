param(
    [Parameter(Mandatory=$true)][string]$BackupRoot
)
$ErrorActionPreference = 'Stop'
throw 'This historical local-server restore entry is retired. Use only the fixed local test environment and review its backups there; old server trees must not be restored as test targets.'
$backupPath = [IO.Path]::GetFullPath($BackupRoot).TrimEnd('\')
$record = Get-Content -LiteralPath (Join-Path $backupPath '备份清单.json') -Encoding UTF8 -Raw | ConvertFrom-Json
$serverPath = [IO.Path]::GetFullPath($record.server_root).TrimEnd('\')
$gamePath = [IO.Path]::GetFullPath((Join-Path $serverPath 'cstrike')).TrimEnd('\')
$backupGamePath = [IO.Path]::GetFullPath((Join-Path $backupPath 'cstrike')).TrimEnd('\')
$running = Get-CimInstance Win32_Process | Where-Object { $_.ExecutablePath -in @((Join-Path $serverPath 'srcds.exe'),(Join-Path $serverPath 'srcds_win64.exe')) }
if ($running) { throw 'Stop this dedicated server before restoring files' }
function Resolve-ContainedFile([string]$root,[string]$relative) {
    $path = [IO.Path]::GetFullPath((Join-Path $root $relative))
    if (-not $path.StartsWith($root + '\',[StringComparison]::OrdinalIgnoreCase)) { throw "Path outside expected directory: $relative" }
    return $path
}
# Validate every path and every existing backup before changing any file.
foreach ($item in $record.files) {
    $targetPath = Resolve-ContainedFile $gamePath $item.path
    if ($item.existed) {
        $sourcePath = Resolve-ContainedFile $backupGamePath $item.path
        if ((Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash -ne $item.sha256) { throw "Backup checksum mismatch: $($item.path)" }
    }
}
foreach ($item in $record.files) {
    $targetPath = Resolve-ContainedFile $gamePath $item.path
    if ($item.existed) {
        $sourcePath = Resolve-ContainedFile $backupGamePath $item.path
        New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($targetPath)) -Force | Out-Null
        Copy-Item -LiteralPath $sourcePath -Destination $targetPath -Force
    } elseif (Test-Path -LiteralPath $targetPath -PathType Leaf) {
        Remove-Item -LiteralPath $targetPath
    }
}
foreach ($item in $record.disabled_active_plugins) {
    $relative = 'addons\sourcemod\plugins\' + $item.to.Replace('/','\')
    $renamedPath = Resolve-ContainedFile $gamePath $relative
    if (Test-Path -LiteralPath $renamedPath -PathType Leaf) { Remove-Item -LiteralPath $renamedPath }
}
Write-Output 'Original plugins, configurations and replaced assets restored; test logs and backup retained.'
