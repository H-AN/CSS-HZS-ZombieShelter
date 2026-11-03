[CmdletBinding()]
param(
    [string]$Compiler,
    [string]$SourceModInclude,
    [string]$SourceModRoot = $env:SOURCEMOD_ROOT,
    [ValidateSet('Main', 'Environment', 'NPCFollow')]
    [string]$Target = 'Main'
)
$ErrorActionPreference = 'Stop'

$sources = @{
    Main = 'src/HanZombieShelter.sp'
    Environment = 'src/HanZombieShelterEnvironment.sp'
    NPCFollow = 'optional/npc-follow-range/addons/sourcemod/scripting/HanZombieShelterNPCFollow.sp'
}
$sourcePath = Join-Path $PSScriptRoot $sources[$Target]
$includePath = Join-Path $PSScriptRoot 'src/include'
if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
    throw "Target source missing: $sourcePath. Install the matching source package or use the complete repository."
}

if (-not $Compiler -and $SourceModRoot) {
    foreach ($name in @('spcomp64.exe', 'spcomp.exe', 'spcomp64', 'spcomp')) {
        $candidate = Join-Path $SourceModRoot ('scripting/' + $name)
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { $Compiler = $candidate; break }
    }
}
if (-not $Compiler) {
    foreach ($name in @('spcomp64', 'spcomp')) {
        $command = Get-Command $name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($command) { $Compiler = $command.Source; break }
    }
}
if (-not $Compiler -or -not (Test-Path -LiteralPath $Compiler -PathType Leaf)) {
    throw 'SourcePawn compiler missing. Supply -Compiler, -SourceModRoot (addons/sourcemod), or SOURCEMOD_ROOT.'
}
$Compiler = (Resolve-Path -LiteralPath $Compiler).Path
if (-not $SourceModInclude) {
    $SourceModInclude = Join-Path (Split-Path -Parent $Compiler) 'include'
}
if (-not (Test-Path -LiteralPath (Join-Path $SourceModInclude 'sourcemod.inc') -PathType Leaf)) {
    throw "SourceMod include missing: $SourceModInclude"
}
$SourceModInclude = (Resolve-Path -LiteralPath $SourceModInclude).Path

$outputDirectory = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$outputPath = Join-Path $outputDirectory ([IO.Path]::GetFileNameWithoutExtension($sourcePath) + '.smx')

# Build outputs are separate from installation files.
& $Compiler $sourcePath ('-i' + $SourceModInclude) ('-i' + $includePath) ('-o' + $outputPath)
if ($LASTEXITCODE -ne 0) { throw "SourcePawn compilation failed with exit code $LASTEXITCODE" }
Write-Output "Built $outputPath"
