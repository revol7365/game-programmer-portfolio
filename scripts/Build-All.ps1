$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) { throw 'Visual Studio 2022 MSBuild was not found.' }

foreach ($configuration in @('Debug', 'Release')) {
    & $msbuild "$root\GameServerPortfolio.sln" /m /t:Rebuild "/p:Configuration=$configuration" /p:Platform=x64 /nologo /v:minimal
    if ($LASTEXITCODE -ne 0) { throw "$configuration x64 build failed." }
}
