$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path (Split-Path $PSScriptRoot -Parent) '..'))
$directory=Join-Path $root ('Diagnostics/StreamingSecurity-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path "$directory/Contents","$directory/ThirdParty/Microsoft/DirectStorage" | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'Projects/Test/Build/Contents/content10.pak') -Destination "$directory/Contents/content10.pak"
Copy-Item -Path (Join-Path $root 'Projects/Test/Build/ThirdParty/Microsoft/DirectStorage/*') -Destination "$directory/ThirdParty/Microsoft/DirectStorage"
$build=[IO.File]::ReadAllText((Join-Path $root 'build.bat'))
$start=$build.IndexOf(':: Check if vcvarsall.bat exists');$end=$build.IndexOf(':: Determine Build Target');$bootstrap=$build.Substring($start,$end-$start)
$sdk=Join-Path $root 'ThirdParty/Microsoft/DirectStorage/sdk-package/native'
$batch=Join-Path $directory 'compile.bat'
$commands=@"
@echo off
setlocal enabledelayedexpansion
set VSCMD_SKIP_SENDTELEMETRY=1
$bootstrap
cd /D "$directory"
cl.exe /nologo /MP2 /EHsc /utf-8 /MD /DNOMINMAX /DWIN32_LEAN_AND_MEAN /std:c++20 /I"$root/src" /I"$sdk/include" "$root/tests/StreamingSecurityRegression.cpp" "$root/src/Storage/DirectStorage/DirectStorageSystem.cpp" /Fe"$directory/StreamingSecurityRegression.exe" /link /LIBPATH:"$sdk/lib/x64" dstorage.lib d3d12.lib dxgi.lib delayimp.lib /DELAYLOAD:dstorage.dll
exit /b %errorlevel%
"@
[IO.File]::WriteAllText($batch,$commands,[Text.Encoding]::Default)
try{
    & $batch *> "$directory/build.log"
    if($LASTEXITCODE-ne 0){Get-Content "$directory/build.log" -Tail 15;throw 'Streaming security compilation failed'}
    & "$directory/StreamingSecurityRegression.exe" *> "$directory/test.log"
    $code=$LASTEXITCODE;Get-Content "$directory/test.log"
    if($code-ne 0){throw 'Streaming security test failed'}
    Write-Output "Reports: $directory"
}finally{Get-ChildItem -LiteralPath $directory -File -Filter '*.obj' | ForEach-Object {Remove-Item -LiteralPath $_.FullName -Force}}
