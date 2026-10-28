param(
    [ValidateSet('EditorBuild')][string]$Build='EditorBuild',
    [string]$Name='benchmark',
    [string]$Arguments='--benchmark 180 --benchmark-native --benchmark-no-vsync',
    [string]$ProjectFile=''
)
$ErrorActionPreference='Stop'
if ($Name -notmatch '^[a-zA-Z0-9_-]+$') { throw 'Use letters, numbers, underscores or hyphens for Name.' }
$root=[IO.Path]::GetFullPath((Join-Path (Split-Path $PSScriptRoot -Parent) '..'))
if(!$ProjectFile){$ProjectFile=Join-Path $root 'Projects/Test/Test.idkp'}
$ProjectFile=(Resolve-Path -LiteralPath $ProjectFile).Path
$folder=Join-Path $root $Build
$kind=if ($Build -eq 'EditorBuild') {'Editor'} else {'Release'}
$source=Join-Path $env:LOCALAPPDATA "Idontknow\Diagnostics\$kind"
$output=Join-Path $root "Diagnostics\Benchmarks\$Build\$Name"
New-Item -ItemType Directory -Path $output -Force | Out-Null
$info=[Diagnostics.ProcessStartInfo]::new()
$info.FileName=Join-Path $folder 'Idontknow.exe'
$info.WorkingDirectory=$folder
$info.Arguments=$Arguments+' --project "'+$ProjectFile+'"'
$info.UseShellExecute=$false
$info.CreateNoWindow=$true
$info.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
$info.RedirectStandardOutput=$true
$info.RedirectStandardError=$true
$process=[Diagnostics.Process]::Start($info)
$stdout=$process.StandardOutput.ReadToEndAsync()
$stderr=$process.StandardError.ReadToEndAsync()
$timedOut=!$process.WaitForExit(90000)
if($timedOut){$process.Kill();$process.WaitForExit()}
$session=Get-ChildItem -LiteralPath $source -Filter "editor-*-$($process.Id).log" | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
if($session){
    # WebView2 children can keep inherited pipe handles open after the engine
    # exits. The editor session log is already flushed and closed at this point.
    $file=[IO.File]::Open($session.FullName,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
    $reader=[IO.StreamReader]::new($file)
    try{[IO.File]::WriteAllText((Join-Path $output 'run.log'),$reader.ReadToEnd())}finally{$reader.Dispose();$file.Dispose()}
}elseif($stdout.Wait(1500)-and $stderr.Wait(1500)){
    [IO.File]::WriteAllText((Join-Path $output 'run.log'),$stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult())
}else{throw 'Engine exited without a complete console session log.'}
$process.StandardOutput.Close();$process.StandardError.Close()
if($timedOut){throw "Benchmark exceeded 90 seconds. Runtime log preserved: $output"}
if ($process.ExitCode -ne 0) { throw "Engine exit code: $($process.ExitCode). See $output" }
foreach ($file in @('benchmark.csv','benchmark_frame.bmp','benchmark_frame.bmp.txt','benchmark_world.txt')) {
    Copy-Item -LiteralPath (Join-Path $source $file) -Destination $output
}
$data=Import-Csv (Join-Path $output 'benchmark.csv') | Select-Object -Skip 30
foreach ($column in @('frame_ms','gpu_ms','render_ms')) {
    $values=@($data | ForEach-Object {[double]$_.$column} | Sort-Object)
    if ($values.Count) {
        $average=($values | Measure-Object -Average).Average
        '{0}: mean {1:F3} ms, median {2:F3} ms, p95 {3:F3} ms' -f $column,$average,$values[[int]($values.Count/2)],$values[[Math]::Min($values.Count-1,[int]($values.Count*.95))]
    }
}
Write-Output "Exit 0; reports: $output"
