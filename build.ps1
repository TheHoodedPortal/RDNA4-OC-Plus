$ErrorActionPreference = 'Stop'
$source = $PSScriptRoot
$build = Join-Path $source 'build-native'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'

if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Visual Studio Installer was not found. Install Visual Studio Build Tools with the Desktop development with C++ workload.'
}

$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ tools were not found.' }
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninja = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
foreach ($path in @($cmake, $ninja, $vcvars)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Required Visual Studio build tool not found: $path" }
}

$configure = '"{0}" -S "{1}" -B "{2}" -G Ninja -DCMAKE_MAKE_PROGRAM="{3}" -DCMAKE_BUILD_TYPE=Release -DRDNA_ENABLE_HOVER_EXPERIMENT=ON' -f $cmake, $source, $build, $ninja
$compile = '"{0}" --build "{1}" --config Release' -f $cmake, $build
$test = '"{0}" --build "{1}" --target test --config Release' -f $cmake, $build
$command = 'call "{0}" >nul && {1} && {2} && {3}' -f $vcvars, $configure, $compile, $test
& cmd.exe /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw "Native configure/build/test failed ($LASTEXITCODE)." }

Write-Host "Built and tested: $(Join-Path $build 'RDNA4OCPlus.exe')"
