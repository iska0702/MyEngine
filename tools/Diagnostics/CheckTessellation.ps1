$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path (Split-Path $PSScriptRoot -Parent) '..'))
$scene=Join-Path $root 'Projects/Test/Saved/Editor.save'
$before=(Get-FileHash -LiteralPath $scene).Hash
foreach($mode in @('raster','dxr')){
    $arguments='--benchmark 90 --benchmark-native --benchmark-no-vsync --benchmark-clouds 0 --benchmark-editor-ui --benchmark-tessellation'
    if($mode-eq 'raster'){$arguments+=' --benchmark-no-raytracing'}
    & (Join-Path (Split-Path $PSScriptRoot -Parent) 'Diagnostics/RunBenchmark.ps1') -Name "tessellation_$mode" -Arguments $arguments
    $log=Join-Path $root "Diagnostics/Benchmarks/EditorBuild/tessellation_$mode/run.log"
    $text=[IO.File]::ReadAllText($log)
    if($text-notmatch 'Tessellation draws: (\d+)'){throw 'Missing tessellation draw statistics'}
    $draws=[long]$matches[1]
    if($mode-eq 'raster'){
        if($draws-le 0){throw 'Raster tessellation did not draw'}
        if($text-notmatch 'Tessellation GPU: hull=(\d+) domain=(\d+) primitives=(\d+)'){throw 'Missing GPU tessellation statistics'}
        if([long]$matches[1]-le 0-or [long]$matches[2]-le 0){throw 'The GPU did not invoke hull/domain shaders'}
    }elseif($draws-ne 0){throw 'DXR should retain the original mesh path'}
    Select-String -LiteralPath $log -Pattern 'Tessellation GPU:|Tessellation draws:' | ForEach-Object {$_.Line}
}
& (Join-Path (Split-Path $PSScriptRoot -Parent) 'Diagnostics/CheckEditorWorkflow.ps1')
if((Get-FileHash -LiteralPath $scene).Hash-ne $before){throw 'Source scene changed during validation'}
Write-Output 'Tessellation: raster GPU stages, DXR fallback, material/scene roundtrip and editor workflow passed; source scene preserved.'
