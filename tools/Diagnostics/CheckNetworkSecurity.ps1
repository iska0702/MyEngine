param()
$ErrorActionPreference='Stop'
if($PSVersionTable.PSVersion.Major-lt 7){throw 'Run this regression script with PowerShell 7 (pwsh)'}
$root=[IO.Path]::GetFullPath((Join-Path (Split-Path $PSScriptRoot -Parent) '..'))
& (Join-Path (Split-Path $PSScriptRoot -Parent) 'Dependencies/FetchMsQuic.ps1')
$diagnostics=Join-Path $root 'Diagnostics';New-Item -ItemType Directory -Force -Path $diagnostics | Out-Null
& (Join-Path $root 'tests/CreateTlsFixtures.ps1')
$runtime=Join-Path $diagnostics 'ThirdParty/Microsoft/MsQuic';New-Item -ItemType Directory -Force -Path $runtime | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'ThirdParty/Microsoft/MsQuic-2.6.3-OpenSSL/build/native/bin/x64/msquic.dll') -Destination $runtime -Force
$build=[IO.File]::ReadAllText((Join-Path $root 'build.bat'))
$start=$build.IndexOf(':: Locate Visual Studio and Windows SDK');$end=$build.IndexOf(':: Determine Build Target')
if($start-lt 0 -or $end-le $start){throw 'Missing compiler bootstrap'}
$bootstrap=$build.Substring($start,$end-$start)
$sources=(Get-ChildItem -LiteralPath (Join-Path $root 'src/Networking') -Filter '*.cpp' | Where-Object {$_.BaseName-ne 'MultiplayerManager'} | ForEach-Object {'"'+$_.FullName+'"'}) -join ' '
$objects=Join-Path $diagnostics ('NetworkObjects-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Path $objects | Out-Null
$batch=Join-Path ([IO.Path]::GetTempPath()) ('Idontknow-network-'+[guid]::NewGuid().ToString('N')+'.bat')
try {
    foreach($test in @('NetworkRegression','TlsRegression')){
        $exe=Join-Path $diagnostics ($test+'.exe')
        $objectArgument=$objects.Replace('\','/')+'/'
        $commands=@"
@echo off
setlocal enabledelayedexpansion
set VSCMD_SKIP_SENDTELEMETRY=1
$bootstrap
cd /D "$root"
cl.exe /nologo /MP4 /EHsc /utf-8 /MD /DNDEBUG /DENGINE_SERVER=1 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /std:c++20 /I"$root/src" "$root/tests/$test.cpp" $sources /Fo"$objectArgument" /Fe"$exe" /link ws2_32.lib advapi32.lib user32.lib bcrypt.lib
exit /b %errorlevel%
"@
        [IO.File]::WriteAllText($batch,$commands,[Text.Encoding]::Default)
        & $batch *> (Join-Path $diagnostics ($test+'-build.log'))
        if($LASTEXITCODE-ne 0){throw "$test compilation failed"}
        Push-Location $root
        try {& $exe *> (Join-Path $diagnostics ($test+'.log'));$code=$LASTEXITCODE} finally {Pop-Location}
        Get-Content -LiteralPath (Join-Path $diagnostics ($test+'.log')) | Select-Object -Last 2
        if($code-ne 0){throw "$test failed; inspect Diagnostics/$test.log"}
    }
} finally {
    Remove-Item -LiteralPath $batch -Force -ErrorAction SilentlyContinue
    Get-ChildItem -LiteralPath $objects -File -Filter '*.obj' | ForEach-Object {Remove-Item -LiteralPath $_.FullName -Force}
    if(!(Get-ChildItem -LiteralPath $objects)){Remove-Item -LiteralPath $objects}
}
