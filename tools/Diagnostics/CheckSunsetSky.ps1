param([switch]$UseExistingRaster)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path (Split-Path $PSScriptRoot -Parent) '..'))
$scene=Join-Path $root 'Projects/Test/Saved/Editor.save'
$before=(Get-FileHash -LiteralPath $scene).Hash
Add-Type -AssemblyName System.Drawing
function SkyMean([string]$path){
    $bmp=[Drawing.Bitmap]::new($path)
    try {
        $r=0.;$g=0.;$b=0.;$count=0
        for($y=120;$y-lt 300;$y+=8){for($x=640;$x-lt 1200;$x+=8){$p=$bmp.GetPixel($x,$y);$r+=$p.R;$g+=$p.G;$b+=$p.B;$count++}}
        return [pscustomobject]@{Luma=(.2126*$r+.7152*$g+.0722*$b)/$count;Warmth=$r/[Math]::Max(1,$b)}
    }finally{$bmp.Dispose()}
}
try {
    foreach($mode in @('raster','dxr')){
        $values=@{}
        foreach($side in @('toward','away')){
            $name=if($mode-eq 'raster'){'sunset_'+$side}else{'sunset_dxr_'+$side}
            if(!$UseExistingRaster -or $mode-ne 'raster'){
                $yaw=if($side-eq 'toward'){0}else{180}
                $arguments="--benchmark 100 --benchmark-native --benchmark-no-vsync --benchmark-clouds 0.9 --benchmark-sky 5 180 $yaw"
                if($mode-eq 'raster'){$arguments+=' --benchmark-no-raytracing'}
                & (Join-Path (Split-Path $PSScriptRoot -Parent) 'Diagnostics/RunBenchmark.ps1') -Name $name -Arguments $arguments
            }
            $path=Join-Path $root "Diagnostics/Benchmarks/EditorBuild/$name/benchmark_frame.bmp"
            $values[$side]=SkyMean $path
        }
        $ratio=$values.toward.Luma/[Math]::Max(1,$values.away.Luma)
        $warm=$values.toward.Warmth/$values.away.Warmth
        if($ratio-lt 1.35 -or $warm-lt 1.15){throw "Sunset hemispheres insufficiently distinct: $mode brightness=$ratio warmth=$warm"}
        Write-Output "$mode PASS: solar/opposite sky brightness ratio=$([Math]::Round($ratio,2)), warmth ratio=$([Math]::Round($warm,2))"
    }
} finally {if((Get-FileHash -LiteralPath $scene).Hash-ne $before){throw 'Sunset test changed the saved scene'}}
