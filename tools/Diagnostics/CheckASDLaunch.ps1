param([switch]$GameOnly,[switch]$SkipGame)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path (Split-Path $PSScriptRoot -Parent) '..'))
if(Get-Process Idontknow -ErrorAction SilentlyContinue){throw 'Close the engine before checking ASD launches'}
& (Join-Path (Split-Path $PSScriptRoot -Parent) 'Graphics/InstallAdvancedShaderDelivery.ps1') -Action Status | Out-Host
$record=Get-Content -LiteralPath (Join-Path $root 'Diagnostics/asd-local-install.json') -Raw | ConvertFrom-Json
$scene=Join-Path (Split-Path $record.Project) 'Saved/Editor.save'
$sceneHash=if(Test-Path -LiteralPath $scene){(Get-FileHash -LiteralPath $scene).Hash}else{''}
$previous=$env:IDK_ASD_VERIFICATION
$env:IDK_ASD_VERIFICATION='1'
try{
    if(!$GameOnly){
        foreach($mode in @('raster','dxr','vertex','tessellation')){
            $arguments='--benchmark 90 --benchmark-native --benchmark-no-vsync --benchmark-editor-ui'
            if($mode-ne 'dxr'){$arguments+=' --benchmark-no-raytracing'}
            if($mode-eq 'vertex'){$arguments+=' --benchmark-vertex-pipeline'}
            if($mode-eq 'tessellation'){$arguments+=' --benchmark-tessellation'}
            & (Join-Path (Split-Path $PSScriptRoot -Parent) 'Diagnostics/RunBenchmark.ps1') -Name "asd_installed_$mode" -ProjectFile $record.Project -Arguments $arguments | Out-Host
            $log=Get-Content -LiteralPath (Join-Path $root "Diagnostics/Benchmarks/EditorBuild/asd_installed_$mode/run.log") -Raw
            if($log-notmatch 'Pipeline cache: bypassed for ASD verification' -or $log-notmatch 'ASD statistics: defaultPSDB=1' -or $log-notmatch 'project identity set'){throw "Persistent ASD binding did not activate: $mode"}
            $matchesFound=[regex]::Matches($log,'ASD statistics: defaultPSDB=1; PSO created=(\d+) psdbMiss=(\d+) totalMiss=(\d+) unknown=(\d+)')
            $stats=$matchesFound | Select-Object -Last 1
            if(!$stats){throw 'Missing final ASD counters'}
            $created=[int]$stats.Groups[1].Value;$missed=[int]$stats.Groups[2].Value;$unknown=[int]$stats.Groups[4].Value
            if($created-le 0 -or $unknown-ne 0 -or (1.0*$missed/$created)-gt .1){throw "ASD known PSO coverage below 90%: $mode"}
            Write-Output "$mode : $($created-$missed)/$created PSO hits; ordinary persisted binding, no temporary d3dconfig test override."
        }
    }
    if(!$SkipGame){
        # Observe the game's own OutputDebugString messages without attaching a
        # debugger, injecting code, or disabling its anti-cheat. Do not take over
        # an existing DebugView/DBWIN reader or retain other processes' messages.
        if(!('IdontknowASDLogListener' -as [type])){
            Add-Type -TypeDefinition @'
using System;
using System.Collections.Concurrent;
using System.IO.MemoryMappedFiles;
using System.Text;
using System.Threading;
public sealed class IdontknowASDLogListener : IDisposable {
    MemoryMappedFile map;
    MemoryMappedViewAccessor view;
    EventWaitHandle ready, data;
    Thread worker;
    volatile bool stopping;
    public volatile int ProcessId;
    public readonly ConcurrentQueue<string> Lines = new ConcurrentQueue<string>();
    public IdontknowASDLogListener() {
        try {
            map=MemoryMappedFile.CreateNew("DBWIN_BUFFER",4096);
            view=map.CreateViewAccessor();
            bool created;
            ready=new EventWaitHandle(false,EventResetMode.AutoReset,"DBWIN_BUFFER_READY",out created);
            if(!created)throw new InvalidOperationException("An existing DBWIN reader owns the ready event");
            data=new EventWaitHandle(false,EventResetMode.AutoReset,"DBWIN_DATA_READY",out created);
            if(!created)throw new InvalidOperationException("An existing DBWIN reader owns the data event");
            worker=new Thread(Read) { IsBackground=true };worker.Start();
        } catch { Dispose();throw; }
    }
    void Read() {
        var bytes=new byte[4092];
        ready.Set();
        while(!stopping) {
            if(!data.WaitOne(100))continue;
            if(stopping)break;
            int pid=view.ReadInt32(0);
            if(ProcessId!=0 && pid==ProcessId) {
                view.ReadArray(4,bytes,0,bytes.Length);
                int length=Array.IndexOf(bytes,(byte)0);
                string line=Encoding.UTF8.GetString(bytes,0,length<0?bytes.Length:length);
                if(line.StartsWith("ASD")||line.StartsWith("Pipeline cache:"))Lines.Enqueue(line);
            }
            ready.Set();
        }
    }
    public void Dispose() {
        stopping=true;
        if(worker!=null) { data.Set();worker.Join();worker=null; }
        if(data!=null)data.Dispose();if(ready!=null)ready.Dispose();
        if(view!=null)view.Dispose();if(map!=null)map.Dispose();
    }
}
'@
        }
        $game=Join-Path (Split-Path $record.Project) 'Build/Idontknow.exe'
        $listener=[IdontknowASDLogListener]::new()
        $process=$null
        try{
            $info=[Diagnostics.ProcessStartInfo]::new()
            $info.FileName=$game;$info.WorkingDirectory=Split-Path $game
            $info.Arguments='--smoke-test';$info.UseShellExecute=$false;$info.CreateNoWindow=$true
            $process=[Diagnostics.Process]::Start($info);$listener.ProcessId=$process.Id
            if(!$process.WaitForExit(90000)){$process.Kill();$process.WaitForExit();throw 'Game launch exceeded 90 seconds'}
            if($process.ExitCode-ne 0){throw "Game exit code: $($process.ExitCode)"}
        }finally{
            $listener.Dispose()
            $gameLog=$listener.Lines.ToArray() -join "`n"
            Set-Content -LiteralPath (Join-Path $root 'Diagnostics/asd-installed-game.log') -Value $gameLog
            if($process){$process.Dispose()}
        }
        if($gameLog-notmatch 'ASD statistics: defaultPSDB=1' -or $gameLog-notmatch 'project identity set' -or $gameLog-notmatch 'Pipeline cache: bypassed for ASD verification'){throw 'Game rendered but its ASD connection could not be verified; inspect asd-installed-game.log'}
        Write-Output "Game rendered and exited 0. $($gameLog -split "`n" | Where-Object {$_ -match '^ASD statistics:'} | Select-Object -Last 1)"
    }
    if($sceneHash -and (Get-FileHash -LiteralPath $scene).Hash-ne $sceneHash){throw 'Source scene changed during ASD checks'}
    Write-Output 'PASS: persisted ASD bindings; source scene preserved.'
}finally{$env:IDK_ASD_VERIFICATION=$previous}
