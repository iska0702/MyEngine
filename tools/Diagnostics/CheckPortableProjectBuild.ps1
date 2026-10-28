$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path (Split-Path $PSScriptRoot -Parent) '..'))
$fixture=Join-Path $root 'Diagnostics/NativeUiFixture/Test.idkp'
if(!(Test-Path -LiteralPath $fixture)){throw 'Create the isolated NativeUiFixture project first'}
if(Get-Process Idontknow -ErrorAction SilentlyContinue){throw 'Close the engine before the portable build test'}
$portable=Join-Path $root ('Diagnostics/PortableEditor-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $portable -Force | Out-Null
Get-ChildItem -LiteralPath (Join-Path $root 'EditorBuild') -Force | Copy-Item -Destination $portable -Recurse -Force
foreach($forbidden in @('src','tools','build.bat','.keys')){if(Test-Path -LiteralPath (Join-Path $portable $forbidden)){throw "Portable editor unexpectedly contains $forbidden"}}
function Run([string]$file,[string]$arguments,[string]$directory,[int]$timeout){
    $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=$file;$info.WorkingDirectory=$directory;$info.Arguments=$arguments
    $info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
    $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
    $info.EnvironmentVariables['PATH']=Join-Path $env:SystemRoot 'System32'
    $process=[Diagnostics.Process]::Start($info)
    try{
        $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
        if(!$process.WaitForExit($timeout)){$process.Kill();$process.WaitForExit();throw 'Isolated process timed out'}
        # WebView2 children may retain inherited pipe handles after the game exits.
        $text='';if($stdout.Wait(1500)){$text+=$stdout.GetAwaiter().GetResult()};if($stderr.Wait(1500)){$text+=$stderr.GetAwaiter().GetResult()}
        if($process.ExitCode-ne 0){throw "Isolated process failed ($($process.ExitCode)): $text"};return $text
    }finally{$process.Dispose()}
}
$editor=Join-Path $portable 'Idontknow.exe'
Run $editor '--startup-check' $portable 60000 | Out-Null
Write-Output 'PASS: copied editor integrity check without the engine source tree'
$watch=[Diagnostics.Stopwatch]::StartNew()
$output=Run $editor ('--build-project "'+$fixture+'" --build-platform standalone') $portable 120000
$watch.Stop()
if($output-notmatch 'Build ready:'){throw 'Native build did not report completion'}
Write-Output ("PASS: built .idkp without source tree, Visual Studio or PowerShell in child PATH ({0:N1} seconds)"-f $watch.Elapsed.TotalSeconds)
$game=Join-Path (Split-Path $fixture) 'Build/Idontknow.exe'
$verify=@(& (Join-Path (Split-Path $PSScriptRoot -Parent) 'Diagnostics/CheckProtectedBuild.ps1') -BuildDirectory (Split-Path $game))
$idLine=$verify|Where-Object {$_-match '^Runtime build ID: [a-f0-9]{32}$'}
if(!$idLine){throw 'Missing authenticated project runtime ID'}
$id=$idLine.Substring('Runtime build ID: '.Length)
Run $game '--smoke-test' (Split-Path $game) 90000 | Out-Null
$cacheBase=Join-Path $env:LOCALAPPDATA 'Idontknow/Games'
$empty=@(Get-ChildItem -Path "$cacheBase/*/Cache/$id/Content/Empty.lua" -File)
if($empty.Count-ne 1-or $empty[0].Length-ne 0){throw 'Empty project asset was not extracted as an authenticated file'}
Write-Output 'PASS: packaged standalone game rendered and exited 0; authenticated empty file survived runtime extraction'
Write-Output "Portable editor: $portable"
