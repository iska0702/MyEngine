$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path (Split-Path $PSScriptRoot -Parent) '..'))
if(Get-Process Idontknow -ErrorAction SilentlyContinue){throw 'Close the engine before checking the runtime cache'}
$verified=@(& (Join-Path (Split-Path $PSScriptRoot -Parent) 'Diagnostics/CheckProtectedBuild.ps1'))
$idLine=$verified | Where-Object {$_-match '^Runtime build ID: [a-f0-9]{32}$'}
if(!$idLine){throw 'No authenticated runtime build ID found'}
$id=$idLine.Substring('Runtime build ID: '.Length)
$base=Join-Path $env:LOCALAPPDATA 'Idontknow/Games'
$candidates=@(Get-ChildItem -Path "$base/*/Cache/$id/Project.idkp" -File)
if($candidates.Count-ne 1){throw 'Run the packaged game once before checking its unique runtime cache'}
$path=[IO.Path]::GetFullPath($candidates[0].FullName)
if(!$path.StartsWith([IO.Path]::GetFullPath($base).TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Invalid runtime cache path'}
$original=[IO.File]::ReadAllBytes($path)
try{
    $bad=[byte[]]$original.Clone();$bad[0]=$bad[0]-bxor 0x40
    [IO.File]::WriteAllBytes($path,$bad)
    & (Join-Path (Split-Path $PSScriptRoot -Parent) 'Diagnostics/CheckASDLaunch.ps1') -GameOnly | Out-Host
    $restored=[IO.File]::ReadAllBytes($path)
    if([Convert]::ToBase64String($restored)-cne [Convert]::ToBase64String($original)){throw 'Runtime cache was not restored from the authenticated archive'}
    Write-Output 'PASS: modified cached Project.idkp restored from authenticated PAK despite existing .complete marker; packaged game rendered and exited 0'
}finally{[IO.File]::WriteAllBytes($path,$original)}
