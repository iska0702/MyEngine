param([ValidateSet('standalone','steam','epic')][string[]]$Platforms=@('standalone','steam','epic'),
      [string]$StandaloneBuild='')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path (Split-Path $PSScriptRoot -Parent) '..'))
$editor=Join-Path $root 'EditorBuild/Idontknow.exe'
$cooker=Join-Path $root 'Diagnostics/Tools/StreamingCooker/CookStreamingTexture.exe'
if(Get-Process Idontknow -ErrorAction SilentlyContinue){throw 'Close the editor/game before preparing engine SDK runtimes'}
if(!(Test-Path -LiteralPath $editor)-or !(Test-Path -LiteralPath $cooker)){throw 'Build the Windows editor once before preparing its runtime support'}
$stage=Join-Path $root '.ProjectBuild'
$previousInternal=$env:IDK_INTERNAL_PROJECT_BUILD;$previousContent=$env:IDK_PROJECT_CONTENT;$previousRuntime=$env:IDK_RUNTIME_ROOT;$previousSteam=$env:MYENGINE_ENABLE_STEAM
try{
    $env:IDK_INTERNAL_PROJECT_BUILD='1';$env:IDK_PROJECT_CONTENT=$previousContent;$env:IDK_RUNTIME_ROOT=$null
    foreach($platform in $Platforms){
        if($platform-eq "steam"){$env:MYENGINE_ENABLE_STEAM="1"}
        if($platform-eq 'standalone'-and $StandaloneBuild){$source=(Resolve-Path -LiteralPath $StandaloneBuild).Path}
        else{
            & (Join-Path $root 'build.bat') release $platform
            if($LASTEXITCODE-ne 0){throw "Cannot compile engine runtime: $platform"}
            $source=$stage
        }
        $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=$editor;$info.WorkingDirectory=Split-Path $editor
        $info.Arguments='--install-runtime "'+$source+'" --build-platform '+$platform+' --texture-cooker "'+$cooker+'"'
        $info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
        $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
        $process=[Diagnostics.Process]::Start($info)
        try{$stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync();$process.WaitForExit();$out=$stdout.GetAwaiter().GetResult();$err=$stderr.GetAwaiter().GetResult();if($out){Write-Output $out};if($err){Write-Output $err};if($process.ExitCode-ne 0){throw "Cannot install native runtime: $platform"}}finally{$process.Dispose()}
    }
    & (Join-Path $root 'tools/Security/generate_dll_manifest.ps1') -TargetDir (Join-Path (Split-Path $editor) 'ThirdParty') -TargetPak (Join-Path (Split-Path $editor) 'Contents/content01.pak')
}finally{
    $env:IDK_INTERNAL_PROJECT_BUILD=$previousInternal;$env:IDK_PROJECT_CONTENT=$previousContent;$env:IDK_RUNTIME_ROOT=$previousRuntime;$env:MYENGINE_ENABLE_STEAM=$previousSteam
    if(Test-Path -LiteralPath $stage){
        if([IO.Path]::GetFullPath($stage)-ne (Join-Path $root '.ProjectBuild')-or ((Get-Item -LiteralPath $stage).Attributes-band [IO.FileAttributes]::ReparsePoint)){throw 'Invalid runtime staging directory'}
        Remove-Item -LiteralPath $stage -Recurse -Force
    }
}
