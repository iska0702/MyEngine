param()
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path (Split-Path $PSScriptRoot -Parent) '..'))
$editor=Join-Path $root 'EditorBuild'
if(Get-Process Idontknow -ErrorAction SilentlyContinue){throw 'Close the engine before testing archive tamper detection'}
$output=Join-Path $root 'Diagnostics/FileProtection';New-Item -ItemType Directory -Path $output -Force | Out-Null
$report=Join-Path $env:LOCALAPPDATA 'Idontknow/Diagnostics/Editor/startup_check.txt'
function Run-Check {
    $info=[Diagnostics.ProcessStartInfo]::new()
    $info.FileName=Join-Path $editor 'Idontknow.exe';$info.WorkingDirectory=$editor;$info.Arguments='--startup-check'
    $info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
    $process=[Diagnostics.Process]::Start($info)
    try{if(!$process.WaitForExit(30000)){$process.Kill();$process.WaitForExit();throw 'Startup check timed out'};return $process.ExitCode}finally{$process.Dispose()}
}
if((Run-Check)-ne 0){throw "Baseline startup integrity check failed: $report"}
Copy-Item -LiteralPath $report -Destination (Join-Path $output 'baseline.log') -Force
foreach($volume in @('content02.pak','content10.pak')){
    if(Get-Process Idontknow -ErrorAction SilentlyContinue){throw 'Engine opened during tamper test'}
    $path=Join-Path $editor ('Contents/'+$volume)
    $original=[IO.File]::ReadAllBytes($path)
    try{
        $changed=[byte[]]$original.Clone();$changed[32]=$changed[32]-bxor 1
        [IO.File]::WriteAllBytes($path,$changed)
        $code=Run-Check
        $text=[IO.File]::ReadAllText($report)
        Copy-Item -LiteralPath $report -Destination (Join-Path $output ($volume+'.tampered.log')) -Force
        if($code-ne 2-or $text-notmatch 'Content archive.*authentication failed'){throw "Tampered $volume was not rejected"}
    }finally{[IO.File]::WriteAllBytes($path,$original)}
}
if((Run-Check)-ne 0){throw 'Restored archives failed startup verification'}
Write-Output 'PASS: startup integrity, EPK3 and SPK3 index tampering rejected, original archives restored and reverified'
