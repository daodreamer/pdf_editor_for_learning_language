param([string]$BuildDir = 'build/windows', [Parameter(Mandatory)][string]$OutputDir)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$vswhere = 'C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'MSVC toolchain not found' }
& (Join-Path $vs 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
if ($LASTEXITCODE) { throw 'DevShell failed' }
$env:PATH = "$root/.deps/Qt/6.8.3/msvc2022_64/bin;" + $env:PATH
& "$root/.tools/venv/Scripts/python" -X utf8 "$PSScriptRoot/package-windows.py" --build $BuildDir --out $OutputDir --vs $vs
if ($LASTEXITCODE) { throw 'Packaging failed; incomplete directory retained for diagnosis' }
