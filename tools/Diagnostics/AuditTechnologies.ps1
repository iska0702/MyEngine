param([switch]$IncludeExtended,[string[]]$CaseNames=@())
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path (Split-Path $PSScriptRoot -Parent) '..'))
$cases=@(
    @{Name='audit_raster'; Args='--benchmark-native --benchmark-no-raytracing'; Pattern='technique 0.*taa 0'; Extra='dxr_enabled 0'},
    @{Name='audit_dxr'; Args='--benchmark-native'; Pattern='technique 0.*taa 0'; Extra='dxr_enabled 1'},
    @{Name='audit_taa'; Args='--benchmark-aa 1'; Pattern='technique 0.*taa 1'; Extra='render 1280x720 display 1280x720'},
    @{Name='audit_dlaa'; Args='--benchmark-aa 2'; Pattern='technique 1 quality 0.*dlss_active 1'; Extra='render 1280x720 display 1280x720'},
    @{Name='audit_dlss_quality'; Args='--benchmark-upscale 1 --benchmark-quality 1'; Pattern='technique 1 quality 1.*dlss_active 1'; Extra='render (?!1280x720)\d+x\d+ display 1280x720'},
    @{Name='audit_fsr_temporal'; Args='--benchmark-upscale 2 --benchmark-quality 1'; Pattern='technique 2 quality 1'; Extra='fsr_temporal_active 1'},
    @{Name='audit_fsr_native'; Args='--benchmark-aa 4'; Pattern='technique 2 quality 0'; Extra='fsr_temporal_active 1'},
    @{Name='audit_xess_aa'; Args='--benchmark-aa 3'; Pattern='technique 3 quality 0.*xess_active 1'; Extra='render 1280x720 display 1280x720'},
    @{Name='audit_xess_quality'; Args='--benchmark-upscale 3 --benchmark-quality 1'; Pattern='technique 3 quality 1.*xess_active 1'; Extra='render (?!1280x720)\d+x\d+ display 1280x720'}
)
if ($IncludeExtended) {
    $cases+=@(
        @{Name='audit_path_tracing'; Args='--benchmark-native --benchmark-pathtracing --benchmark-editor-lighting'; Pattern='technique 0'; Extra='path_tracing 1'},
        @{Name='audit_rain'; Args='--benchmark-native --benchmark-rain'; Pattern='technique 0'; Extra='dxr_enabled 1'},
        @{Name='audit_fsr_fg'; Args='--benchmark-aa 4 --benchmark-fsr-fg'; Pattern='fsr_frame_generation enabled 1 active 1 generated [1-9]\d*'; Extra='fsr_temporal_active 1'},
        @{Name='audit_xefg'; Args='--benchmark-aa 3 --benchmark-xess-fg'; Pattern='xess_frame_generation enabled 1 active 1 generated [1-9]\d*'; Extra='xell_active 1'},
        @{Name='audit_rr'; Args='--benchmark-rr --benchmark-pathtracing'; Pattern='ray_reconstruction supported 1 active 1'; Extra='dlss_active 1'},
        @{Name='audit_reflex'; Args='--benchmark-native --benchmark-latency 1'; Pattern='reflex_active 1'; Extra='xell_enabled 0'},
        @{Name='audit_xell'; Args='--benchmark-native --benchmark-latency 2'; Pattern='xell_active 1'; Extra='reflex_mode 0'},
        @{Name='audit_fg_cycle'; Args='--benchmark-aa 4 --benchmark-fsr-fg --benchmark-xess-fg'; Frames=180; Pattern='fsr_frame_generation enabled 1 active 1 generated [1-9]\d*'; Extra='render 1024x576 display 1024x576'}
    )
}
$results=@()
if($CaseNames.Count){
    foreach($name in $CaseNames){if($name -notin $cases.Name){throw "Unknown audit case: $name"}}
    $cases=@($cases | Where-Object {$_.Name -in $CaseNames})
    $previous=Join-Path $root 'Diagnostics/technology-audit.csv'
    if(Test-Path $previous){$results=@(Import-Csv $previous | Where-Object {$_.Case -notin $CaseNames} | ForEach-Object {[pscustomobject]@{Case=$_.Case;Passed=[bool]::Parse($_.Passed);Details=$_.Details}})}
}
foreach ($case in $cases) {
    $passed=$false; $detail=''
    try {
        $frames=if($case.Frames){$case.Frames}else{90}
        $arguments="--benchmark $frames --benchmark-no-vsync --benchmark-clouds 0 --benchmark-editor-ui $($case.Args)"
        & (Join-Path (Split-Path $PSScriptRoot -Parent) 'Diagnostics/RunBenchmark.ps1') -Build EditorBuild -Name $case.Name -Arguments $arguments
        $report=Join-Path $root "Diagnostics\Benchmarks\EditorBuild\$($case.Name)\benchmark_frame.bmp.txt"
        $text=[IO.File]::ReadAllText($report)
        if ($text -notmatch $case.Pattern -or $text -notmatch $case.Extra) { throw "Unexpected runtime status: $text" }
        if($case.Name -eq 'audit_fg_cycle' -and $text -notmatch 'xess_frame_generation .* generated [1-9]\d*'){throw 'XeFG did not generate frames during the provider switch test.'}
        if ($text -notmatch 'frame_generation_integrated 1' -or $text -notmatch 'ray_reconstruction_integrated 1') {
            throw 'SDK integration status is missing.'
        }
        $log=[IO.File]::ReadAllText((Join-Path (Split-Path $report) 'run.log'))
        if($log -match '\[FATAL\]|tagging failed|Present failed|presentCommon\(\) was not observed'){throw 'Runtime SDK or presentation failure; inspect run.log'}
        $passed=$true; $detail=($text -split '\r?\n')[0]
    } catch { $detail=$_.Exception.Message }
    $results += [pscustomobject]@{Case=$case.Name;Passed=$passed;Details=$detail}
    $results | Export-Csv -LiteralPath (Join-Path $root 'Diagnostics\technology-audit.csv') -NoTypeInformation -Encoding UTF8
    Write-Output "$($case.Name): passed=$passed"
}
if (@($results | Where-Object {!$_.Passed}).Count) { throw 'Some technology checks failed; see Diagnostics/technology-audit.csv.' }
Write-Output "All $($results.Count) runtime technology checks passed."
