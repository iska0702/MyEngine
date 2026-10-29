param()
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path (Split-Path $PSScriptRoot -Parent) '..'))
$diagnostics=Join-Path $root 'Diagnostics'
New-Item -ItemType Directory -Force -Path $diagnostics | Out-Null
$batch=[IO.File]::ReadAllText((Join-Path $root 'build.bat'))
$start=$batch.IndexOf(':: Locate Visual Studio and Windows SDK')
$end=$batch.IndexOf(':: Determine Build Target')
if($start-lt 0 -or $end-le $start){throw 'Compiler bootstrap missing from build.bat'}
$bootstrap=$batch.Substring($start,$end-$start)
$temp=Join-Path ([IO.Path]::GetTempPath()) ('Idontknow-vehicle-check-'+[guid]::NewGuid().ToString('N')+'.bat')
$oldPath=$env:PATH
try {
    $commands=@"
@echo off
setlocal enabledelayedexpansion
set VSCMD_SKIP_SENDTELEMETRY=1
$bootstrap
cd /D "$root"
cl.exe /nologo /EHsc /MD /DNDEBUG /std:c++20 /I"$root\src" /I"$root\ThirdParty\NVIDIA\PhysX\physx\include" tests\VehicleDynamicsRegression.cpp /FoDiagnostics\vehicle-dynamics-regression.obj /FeDiagnostics\vehicle-dynamics-regression.exe /link /LIBPATH:"$root\ThirdParty\NVIDIA\PhysX\Lib\bin\win.x86_64.vc143.md\release" PhysXVehicle_static.lib PhysXExtensions_static.lib PhysXFoundation.lib PhysXCommon.lib PhysX.lib
exit /b %errorlevel%
"@
    [IO.File]::WriteAllText($temp,$commands,[Text.Encoding]::Default)
    & $temp *> (Join-Path $diagnostics 'vehicle-dynamics-regression-build.log')
    if($LASTEXITCODE-ne 0){throw 'Vehicle regression compilation failed; see Diagnostics/vehicle-dynamics-regression-build.log'}
    $env:PATH=(Join-Path $root 'ThirdParty/NVIDIA/PhysX/Lib/bin/win.x86_64.vc143.md/release')+';'+$oldPath
    & (Join-Path $diagnostics 'vehicle-dynamics-regression.exe') (Join-Path $diagnostics 'vehicle-dynamics-measurements.csv') *> (Join-Path $diagnostics 'vehicle-dynamics-regression.log')
    $result=$LASTEXITCODE
    Get-Content -LiteralPath (Join-Path $diagnostics 'vehicle-dynamics-regression.log')
    if($result-ne 0){throw 'Vehicle dynamics regression failed'}
} finally {
    $env:PATH=$oldPath
    Remove-Item -LiteralPath $temp -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath (Join-Path $diagnostics 'vehicle-dynamics-regression.obj') -Force -ErrorAction SilentlyContinue
}
