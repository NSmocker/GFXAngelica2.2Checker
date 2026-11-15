param(
    [ValidateSet('x64', 'Win32')][string]$Platform = 'x64',
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
    [string]$ClientSourceRoot = 'B:\Git\PW_ClientSource'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswherePath)) { throw 'Visual Studio Installer (vswhere.exe) was not found.' }

# Готові LTCG-бібліотеки зібрані VS 2022; обираємо саме v143, не новіший VS 18.
$vsPath = & $vswherePath -latest -products '*' -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'Visual Studio 2022 with the v143 C++ toolset was not found.' }
$msbuildPath = Join-Path $vsPath 'MSBuild\Current\Bin\MSBuild.exe'
$buildLogDirectory = Join-Path $projectRoot 'build\logs'
New-Item -ItemType Directory -Path $buildLogDirectory -Force | Out-Null
$buildArguments = @(
    (Join-Path $projectRoot 'gfxchecker.sln'), '/m', '/nologo', '/verbosity:minimal',
    "/p:Configuration=$Configuration", "/p:Platform=$Platform", "/p:ClientSourceRoot=$ClientSourceRoot",
    "/flp:logfile=$buildLogDirectory\$Platform-$Configuration.log;verbosity=normal"
)
& $msbuildPath @buildArguments
if ($LASTEXITCODE -ne 0) { throw "MSBuild failed with exit code $LASTEXITCODE. See build/logs." }
