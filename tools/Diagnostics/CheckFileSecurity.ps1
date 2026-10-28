param()
$ErrorActionPreference='Stop'
if($PSVersionTable.PSVersion.Major-lt 7){throw 'Run with PowerShell 7 (pwsh)'}
$root=[IO.Path]::GetFullPath((Join-Path (Split-Path $PSScriptRoot -Parent) '..'))
$directory=Join-Path $root ('Diagnostics/FileSecurity-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $directory -Force | Out-Null
& (Join-Path $root 'tests/CreateFileSecurityFixtures.ps1') -Directory $directory
$build=[IO.File]::ReadAllText((Join-Path $root 'build.bat'))
$start=$build.IndexOf(':: Check if vcvarsall.bat exists');$end=$build.IndexOf(':: Determine Build Target')
if($start-lt 0-or $end-le $start){throw 'Missing compiler bootstrap'}
$bootstrap=$build.Substring($start,$end-$start)
$batch=Join-Path $directory 'compile.bat'
$commands=@"
@echo off
setlocal enabledelayedexpansion
set VSCMD_SKIP_SENDTELEMETRY=1
$bootstrap
cd /D "$directory"
cl.exe /nologo /MP4 /EHsc /utf-8 /MD /DNDEBUG /DENGINE_EDITOR=1 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /std:c++20 /I"$root/src" "$root/tests/FileSecurityRegression.cpp" "$root/src/Storage/SaveSystem.cpp" "$root/src/Storage/PakCompression.cpp" "$root/src/Platform/PlatformUtils.cpp" /Fe"$directory/FileSecurityRegression.exe" /link shell32.lib advapi32.lib user32.lib ole32.lib
exit /b %errorlevel%
"@
[IO.File]::WriteAllText($batch,$commands,[Text.Encoding]::Default)
try {
    & $batch *> (Join-Path $directory 'build.log')
    if($LASTEXITCODE-ne 0){Get-Content (Join-Path $directory 'build.log') -Tail 15;throw 'File security regression compilation failed'}
    & (Join-Path $directory 'FileSecurityRegression.exe') $directory *> (Join-Path $directory 'test.log')
    $code=$LASTEXITCODE;Get-Content (Join-Path $directory 'test.log')
    if($code-ne 0){throw "File security regression failed: $directory"}
    Write-Output "Reports: $directory"
} finally {
    Get-ChildItem -LiteralPath $directory -File -Filter '*.obj' | ForEach-Object {Remove-Item -LiteralPath $_.FullName -Force}
}
