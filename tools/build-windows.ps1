param([string]$BuildDir = 'build/windows', [switch]$Test, [switch]$Phase0)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    $vswhere = 'C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe'
    $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (!$vs) { throw 'MSVC x64 toolchain not found' }
    $dev = Join-Path $vs 'Common7/Tools/Launch-VsDevShell.ps1'
    & $dev -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
    if ($LASTEXITCODE) { throw 'DevShell failed' }
    $env:PATH = "$root/.tools/venv/Scripts;$root/.deps/Qt/6.8.3/msvc2022_64/bin;" + $env:PATH
    $options = @('-DLANGLERN_BUILD_PHASE0=OFF', "-DPDFium_DIR=$root/.deps/pdfium-win")
    if ($Phase0) { $options = @('-DLANGLERN_BUILD_PHASE0=ON', "-DPDFium_DIR=$root/.deps/pdfium-win") }
    if ($Test) { $options += '-DBUILD_TESTING=ON' }
    & "$root/.deps/Qt/6.8.3/msvc2022_64/bin/qt-cmake.bat" -S . -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=Release @options
    if ($LASTEXITCODE) { throw 'Configure failed' }
    cmake --build $BuildDir --parallel 6
    if ($LASTEXITCODE) { throw 'Build failed' }
    if ($Test) {
        ctest --test-dir $BuildDir --output-on-failure
        if ($LASTEXITCODE) { throw 'CTest failed' }
    }
} finally { Pop-Location }
