param([string]$BuildDir = 'build/windows')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$env:PATH = "$root/.deps/Qt/6.8.3/msvc2022_64/bin;" + $env:PATH
$exe = Join-Path $root "$BuildDir/langlern.exe"
if (!(Test-Path -LiteralPath $exe)) { throw "Build the application first: ./tools/build-windows.ps1 -BuildDir $BuildDir -Test" }
& $exe
