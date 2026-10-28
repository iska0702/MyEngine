$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path (Split-Path $PSScriptRoot -Parent) '..'))
$scene=Join-Path $root 'Projects/Test/Saved/Editor.save'
$before=(Get-FileHash -LiteralPath $scene).Hash
$base='--benchmark 90 --benchmark-native --benchmark-no-vsync --benchmark-clouds 0 --benchmark-no-raytracing'
function Run([string]$Name,[string]$Arguments){
    & (Join-Path (Split-Path $PSScriptRoot -Parent) 'Diagnostics/RunBenchmark.ps1') -Name $Name -Arguments $Arguments | Out-Host
    $log=[IO.File]::ReadAllText((Join-Path $root "Diagnostics/Benchmarks/EditorBuild/$Name/run.log"))
    if($log -match '\[FATAL\]|Present failed|Pipeline FAILED:'){throw "Backend failure: $Name"}
    if($log-notmatch 'Render backend: mesh=(\d+) dxr=(\d+) meshDispatches=(\d+) vertexDraws=(\d+) computeDispatches=(\d+)'){throw 'Backend counters missing'}
    $result=@{Log=$log;Mesh=[long]$matches[1];Dxr=[long]$matches[2];MeshDraws=[long]$matches[3];VertexDraws=[long]$matches[4];Compute=[long]$matches[5]}
    if($log-notmatch 'Pipeline cache: hits=(\d+) misses=(\d+) failed=(\d+) storeFailed=(\d+)'){throw 'Pipeline cache counters missing'}
    $result.Hits=[int]$matches[1];$result.Misses=[int]$matches[2]
    if([int]$matches[3]-ne 0-or [int]$matches[4]-ne 0){throw 'PSO creation/storage failure'}
    return $result
}
$cold=Run 'backend_mesh' ($base+' --benchmark-pipeline-depth-pyramid')
if($cold.Mesh-ne 1-or $cold.MeshDraws-le 0-or $cold.Compute-le 0){throw 'Mesh/compute dispatch did not execute'}
if($cold.Log-notmatch 'Mesh GPU: amplification=([1-9]\d*) mesh=([1-9]\d*) primitives=([1-9]\d*)'){throw 'Missing GPU mesh execution proof'}
if($cold.Log-notmatch 'Compute GPU: invocations=([1-9]\d*)'){throw 'Missing GPU compute execution proof'}
$warm=Run 'backend_warm' ($base+' --benchmark-pipeline-depth-pyramid')
if($warm.Hits-le 0-or $warm.Misses-ne 0){throw 'Second startup did not reuse every engine-owned PSO'}
$fallback=Run 'backend_vertex' ($base+' --benchmark-vertex-pipeline --benchmark-no-dxr-hardware --benchmark-pipeline-depth-pyramid')
if($fallback.Mesh-ne 0-or $fallback.Dxr-ne 0-or $fallback.MeshDraws-ne 0-or $fallback.VertexDraws-le 0-or $fallback.Compute-le 0){throw 'Vertex/compute fallback failed'}
$outside=Run 'backend_cull_outside' ($base+' --benchmark-cull-outside')
if($outside.Log-notmatch 'Mesh GPU: amplification=([1-9]\d*) mesh=0 primitives=0'){throw 'AS did not reject out-of-frustum meshlets'}
if($cold.Log-notmatch '(?m)^Pipeline cache file: (.+)$'){throw 'Missing cache path'}
$cache=[IO.Path]::GetFullPath($matches[1].Trim())
$boundary=[IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'Idontknow/ShaderCache'))+'\'
if(!$cache.StartsWith($boundary,[StringComparison]::OrdinalIgnoreCase)-or [IO.Path]::GetExtension($cache)-ne '.pso'){throw 'Unexpected cache target'}
$backup=Join-Path $root 'Diagnostics/backend-library-before-corruption.pso'
Copy-Item -LiteralPath $cache -Destination $backup -Force
try{
    [IO.File]::WriteAllBytes($cache,[byte[]](1,2,3))
    $recovered=Run 'backend_cache_recovery' $base
    if($recovered.Log-notmatch 'incompatible/corrupt library discarded'-or $recovered.Misses-le 0){throw 'Corrupt cache did not recover'}
}catch{Copy-Item -LiteralPath $backup -Destination $cache -Force;throw}
if((Get-FileHash -LiteralPath $scene).Hash-ne $before){throw 'Source scene changed during backend checks'}
Write-Output "PASS: GPU mesh/compute, frustum rejection, vertex fallback, warm PSO cache ($($warm.Hits) hits), corrupt-cache recovery; source scene preserved."
