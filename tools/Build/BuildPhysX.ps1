param([int]$Jobs = 6)
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$sdk = Join-Path $root 'ThirdParty/NVIDIA/PhysX'
$source = Join-Path $sdk 'physx'
$cmake = Get-Command cmake -ErrorAction SilentlyContinue
$ninja = Get-Command ninja -ErrorAction SilentlyContinue
if (!$cmake -or !$ninja) {
    if (!$env:VSINSTALLDIR) { throw 'Run from a Visual Studio developer shell (or through build.bat).' }
    $bundled = Join-Path $env:VSINSTALLDIR 'Common7/IDE/CommonExtensions/Microsoft/CMake'
    $cmake = Join-Path $bundled 'CMake/bin/cmake.exe'
    $ninja = Join-Path $bundled 'Ninja/ninja.exe'
}
$build = Join-Path $sdk 'build-myengine'
& $cmake -S "$source/compiler/public" -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" `
    -DCMAKE_BUILD_TYPE=release "-DPHYSX_ROOT_DIR=$source" -DTARGET_BUILD_PLATFORM=windows `
    -DPX_BUILDSNIPPETS=OFF -DPX_BUILDPVDRUNTIME=OFF -DPX_GENERATE_GPU_PROJECTS=OFF `
    -DPX_GENERATE_STATIC_LIBRARIES=OFF -DNV_USE_STATIC_WINCRT=OFF `
    "-DPX_OUTPUT_LIB_DIR=$sdk/Lib" "-DPX_OUTPUT_BIN_DIR=$sdk/bin"
if ($LASTEXITCODE) { throw 'PhysX configuration failed.' }
& $cmake --build $build --parallel $Jobs
if ($LASTEXITCODE) { throw 'PhysX build failed.' }
