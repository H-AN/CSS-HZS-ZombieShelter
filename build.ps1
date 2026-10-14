param(
    [string]$Compiler = 'D:\css\sp\大灾变1.12\addons\sourcemod\scripting\spcomp64.exe',
    [string]$SourceModInclude = 'D:\css\sp\大灾变1.12\addons\sourcemod\scripting\include',
    [switch]$IntegrationProbe
)
$ErrorActionPreference = 'Stop'
$sourcePath = Join-Path $PSScriptRoot 'addons\sourcemod\scripting\HanZombieShelter.sp'
$includePath = Join-Path $PSScriptRoot 'addons\sourcemod\scripting\include'
$outputPath = Join-Path $PSScriptRoot 'addons\sourcemod\plugins\HanZombieShelter.smx'
if ($IntegrationProbe) {
    $sourcePath = Join-Path $PSScriptRoot 'tests\ShelterIntegrationProbe.sp'
    $testOutput = Join-Path $PSScriptRoot 'tests\compiled'
    New-Item -ItemType Directory -Path $testOutput -Force | Out-Null
    $outputPath = Join-Path $testOutput 'ShelterIntegrationProbe.smx'
}
if (-not (Test-Path -LiteralPath $Compiler)) { throw "Compiler missing: $Compiler" }
if (-not (Test-Path -LiteralPath $SourceModInclude)) { throw "SourceMod include missing: $SourceModInclude" }
& $Compiler $sourcePath ('-i' + $SourceModInclude) ('-i' + $includePath) ('-o' + $outputPath)
if ($LASTEXITCODE -ne 0) { throw "SourcePawn compilation failed with exit code $LASTEXITCODE" }
Write-Output "Built $outputPath"
