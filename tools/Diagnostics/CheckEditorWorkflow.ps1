param([string]$ProjectFile='')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path (Split-Path $PSScriptRoot -Parent) '..'))
if(!$ProjectFile){$ProjectFile=Join-Path $root 'Projects/Test/Test.idkp'}
$ProjectFile=(Resolve-Path -LiteralPath $ProjectFile).Path
$text=[IO.File]::ReadAllText($ProjectFile)
if($text -notmatch '(?m)^editorScene\s+"([^"]+)"'){throw 'Missing editorScene in project'}
$scene=Join-Path (Split-Path $ProjectFile) $Matches[1]
$before=if(Test-Path -LiteralPath $scene){(Get-FileHash -LiteralPath $scene).Hash}else{''}
try {
    & (Join-Path (Split-Path $PSScriptRoot -Parent) 'Diagnostics/RunBenchmark.ps1') -Name editor_workflow_integration -ProjectFile $ProjectFile -Arguments '--benchmark 120 --benchmark-native --benchmark-no-vsync --benchmark-clouds 0 --benchmark-editor-ui --benchmark-editor-workflow'
    $directory=Join-Path $root 'Diagnostics/Benchmarks/EditorBuild/editor_workflow_integration'
    $log=[IO.File]::ReadAllText((Join-Path $directory 'run.log'))
    if($log -notmatch '\[Editor workflow\] PASS:' -or $log -match '\[Editor workflow\] FAIL:|\[FATAL\]|Present failed'){throw 'Editor workflow integration failed'}
    $state=[IO.File]::ReadAllText((Join-Path $directory 'benchmark_frame.bmp.txt'))
    if($state -notmatch 'editor_texture_cache entries [1-9]\d* previews [1-9]\d*'){throw 'Texture preview did not obtain a GPU descriptor'}
    if($state -notmatch '(?m)^gpu_pass_ms (.+)$'){throw 'GPU pass timings missing'}
    $times=@($Matches[1].Trim() -split '\s+' | ForEach-Object {[double]::Parse($_,[Globalization.CultureInfo]::InvariantCulture)})
    if($times.Count -ne 10 -or ($times | Where-Object {[double]::IsNaN($_) -or [double]::IsInfinity($_) -or $_ -lt 0}).Count -or ($times|Measure-Object -Sum).Sum -le 0){throw 'Invalid GPU pass timings'}
    Write-Output 'PASS: editor integration and 10 GPU pass timings.'
} finally {
    $after=if(Test-Path -LiteralPath $scene){(Get-FileHash -LiteralPath $scene).Hash}else{''}
    if($before -ne $after){throw 'Editor workflow check changed the source scene'}
    Write-Output 'Original editor scene preserved.'
}
