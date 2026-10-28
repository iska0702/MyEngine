$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path (Split-Path $PSScriptRoot -Parent) '..'))
$directory=Join-Path $root ('Diagnostics/ProjectBuilder-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Path $directory -Force | Out-Null
$build=[IO.File]::ReadAllText((Join-Path $root 'build.bat'));$start=$build.IndexOf(':: Check if vcvarsall.bat exists');$end=$build.IndexOf(':: Determine Build Target');$bootstrap=$build.Substring($start,$end-$start)
$commands=@"
@echo off
setlocal enabledelayedexpansion
set VSCMD_SKIP_SENDTELEMETRY=1
$bootstrap
cd /D "$directory"
cl.exe /nologo /MP3 /EHsc /utf-8 /MD /DENGINE_EDITOR=1 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /std:c++20 /I"$root/src" "$root/tests/ProjectBuilderRegression.cpp" "$root/src/Core/ProjectBuilder.cpp" "$root/src/Storage/PakCompression.cpp" /Fe"$directory/ProjectBuilderRegression.exe" /link user32.lib
exit /b %errorlevel%
"@
$batch=Join-Path $directory 'compile.bat';[IO.File]::WriteAllText($batch,$commands,[Text.Encoding]::Default)
try{
    & $batch *> "$directory/build.log"
    if($LASTEXITCODE-ne 0){Get-Content "$directory/build.log" -Tail 20;throw 'Project builder compilation failed'}
    & "$directory/ProjectBuilderRegression.exe" "$root/Projects/Test/Build" "$root/Diagnostics/Tools/StreamingCooker/CookStreamingTexture.exe" "$directory/Cases" *> "$directory/test.log"
    $code=$LASTEXITCODE;Get-Content "$directory/test.log" -Tail 18
    if($code-ne 0){throw 'Project builder regression failed'}
    Write-Output "Reports: $directory"
}finally{Get-ChildItem -LiteralPath $directory -File -Filter '*.obj' | ForEach-Object {Remove-Item -LiteralPath $_.FullName -Force}}
