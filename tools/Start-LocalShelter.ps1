param(
    [string]$ServerRoot = 'D:\css\CS2\steamcmd\steamapps\common\Counter-Strike Source Dedicated Server',
    [int]$Port = 27129,
    [string]$RconPassword = ''
)
$ErrorActionPreference = 'Stop'
$serverPath = [IO.Path]::GetFullPath($ServerRoot)
$executable = Join-Path $serverPath 'srcds_win64.exe'
if (-not (Test-Path -LiteralPath $executable)) { throw "Server missing: $executable" }
if (-not (Test-Path -LiteralPath (Join-Path $serverPath 'cstrike\cfg\zsh_codex_test.cfg'))) { throw 'Local test configuration missing' }
if ($Port -lt 1024 -or $Port -gt 65535) { throw 'Invalid port' }
if (-not $RconPassword) { $RconPassword = [Guid]::NewGuid().ToString('N') }
if ($RconPassword -notmatch '^[A-Za-z0-9_-]{8,128}$') { throw 'Use 8-128 letters, numbers, underscore or hyphen for RCON' }
$running = Get-CimInstance Win32_Process | Where-Object { $_.ExecutablePath -eq $executable }
if ($running) { throw 'This dedicated server is already running' }
$secretPath = Join-Path $serverPath '.zsh-local-rcon.txt'
[IO.File]::WriteAllText($secretPath,$RconPassword,[Text.Encoding]::ASCII)
$arguments = @('-console','-game','cstrike','-insecure','-nohltv','-ip','127.0.0.1','-port',$Port,
    '+maxplayers','32','+servercfgfile','zsh_codex_test.cfg','+rcon_password',$RconPassword,
    '+con_logfile','zsh_local_runtime.log','+map','zsh_desertplant_cso_v1_d')
$process = Start-Process -FilePath $executable -ArgumentList $arguments -WorkingDirectory $serverPath -WindowStyle Hidden -PassThru
Write-Output "Started local server PID $($process.Id); connect 127.0.0.1:$Port"
Write-Output "RCON password is in $secretPath"
