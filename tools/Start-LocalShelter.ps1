param(
    [string]$ServerRoot = 'D:\css\CS2\steamcmd\steamapps\common\Counter-Strike Source Dedicated Server\昼夜求生本地测试服务器',
    [int]$Port = 27169,
    [string]$ListenAddress = '0.0.0.0',
    [string]$RconPassword = '',
    [ValidateSet('zsh_desert_han_dn', 'zsh_deadcity_han_dn')]
    [string]$MapName = 'zsh_desert_han_dn'
)
$ErrorActionPreference = 'Stop'
$expectedRoot = 'D:\css\CS2\steamcmd\steamapps\common\Counter-Strike Source Dedicated Server\昼夜求生本地测试服务器'
$serverPath = [IO.Path]::GetFullPath($ServerRoot).TrimEnd('\')
if (-not $serverPath.Equals($expectedRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Local server tests may use only: $expectedRoot"
}
if ($Port -ne 27169) { throw 'Local server tests may use only port 27169' }
if ($ListenAddress -ne '0.0.0.0') { throw 'The confirmed local test listener must be 0.0.0.0' }
if ($PSBoundParameters.ContainsKey('RconPassword')) {
    throw 'The fixed controller manages private RCON credentials; do not supply a password'
}
$controller = Join-Path $serverPath 'tools\server_control.py'
if (-not (Test-Path -LiteralPath $controller -PathType Leaf)) {
    throw "The fixed local test environment is not prepared: $controller"
}
$pythonPath = Join-Path $serverPath 'tools\python\python.exe'
if (-not (Test-Path -LiteralPath $pythonPath -PathType Leaf)) {
    $pythonPath = 'D:\python\python.exe'
}
if (-not (Test-Path -LiteralPath $pythonPath -PathType Leaf)) {
    throw 'The bundled Python runtime is missing; finish preparing the fixed local test environment first'
}
& $pythonPath -B -X utf8 $controller --start --map $MapName
if ($LASTEXITCODE -ne 0) { throw 'The fixed local test server could not start; inspect its control/session.json' }
