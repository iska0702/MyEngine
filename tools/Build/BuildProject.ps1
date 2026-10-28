param([Parameter(Mandatory=$true)][string]$ProjectFile,
      [ValidateSet('','standalone','steam','epic')][string]$Platform='',
      [switch]$CompileEngine)
$ErrorActionPreference='Stop'
$engine=[IO.Path]::GetFullPath((Join-Path (Split-Path $PSScriptRoot -Parent) '..')).TrimEnd('\')
$file=(Resolve-Path -LiteralPath $ProjectFile).Path
if([IO.Path]::GetExtension($file)-ne '.idkp'){throw 'Select an .idkp project'}
if(!$CompileEngine){
    $editor=Join-Path $engine 'EditorBuild/Idontknow.exe'
    if(!(Test-Path -LiteralPath $editor)){throw 'Install the editor and its runtime build support first'}
    $info=[Diagnostics.ProcessStartInfo]::new()
    $info.FileName=$editor;$info.WorkingDirectory=Split-Path $editor
    $info.Arguments='--build-project "'+$file+'"'
    if($Platform){$info.Arguments+=' --build-platform '+$Platform}
    $info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
    $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
    $process=[Diagnostics.Process]::Start($info)
    try{
        $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
        $process.WaitForExit();$output=$stdout.GetAwaiter().GetResult();$errors=$stderr.GetAwaiter().GetResult()
        if($output){Write-Output $output};if($errors){Write-Output $errors}
        if($process.ExitCode-ne 0){throw 'Native project build failed; inspect Saved/BuildLogs in the project'}
    }finally{$process.Dispose()}
    return
}
$lines=[IO.File]::ReadAllLines($file)
if($lines[0]-ne 'IDKP 1'){throw 'Unsupported project format'}
$settings=@{}
foreach($line in $lines | Select-Object -Skip 1){if($line -match '^(\w+)\s+"(.*)"$'){$settings[$matches[1]]=$matches[2].Replace('\"','"').Replace('\\','\')}elseif($line -match '^(\w+)\s+(\S+)$'){$settings[$matches[1]]=$matches[2]}}
$project=Split-Path $file -Parent
function ProjectPath([string]$relative){
    if([IO.Path]::IsPathRooted($relative)){throw 'Project paths must be relative'}
    $path=[IO.Path]::GetFullPath((Join-Path $project $relative))
    if(!$path.StartsWith($project.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Project path escapes its folder'}
    return $path
}
if(!$settings.content -or !$settings.editorScene){throw 'Project requires content and editorScene settings'}
$content=ProjectPath $settings.content
if(!(Test-Path -LiteralPath $content -PathType Container)){throw 'Project Content directory not found'}
foreach($key in @('editorScene','startupLevel','mainMenu','splash')){if($settings[$key]){$path=ProjectPath $settings[$key];if($key-ne 'editorScene'-and !(Test-Path -LiteralPath $path)){throw "Project asset not found: $($settings[$key])"}}}
if(!$Platform){$Platform=$settings.platform}
if($Platform -notin @('standalone','steam','epic')){throw 'Invalid project build platform'}
$validator=Join-Path $engine 'EditorBuild/Idontknow.exe'
if(Test-Path -LiteralPath $validator){
    $check=Start-Process -FilePath $validator -ArgumentList ('--project-check --project "'+$file+'"') -PassThru -Wait -WindowStyle Hidden
    if($check.ExitCode-ne 0){throw 'Project settings validation failed'}
}
$lock=$null
$previousInternal=$env:IDK_INTERNAL_PROJECT_BUILD;$previousContent=$env:IDK_PROJECT_CONTENT
$previousRuntime=$env:IDK_RUNTIME_ROOT
$payload=Join-Path $engine ('Diagnostics/ProjectPayload/'+[Guid]::NewGuid().ToString('N'))
$stage=Join-Path $engine '.ProjectBuild'
$destination=Join-Path $project 'Build'
try{
    New-Item -ItemType Directory -Path (Join-Path $engine 'Diagnostics') -Force | Out-Null
    $lock=[IO.File]::Open((Join-Path $engine 'Diagnostics/project-build.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    if(Test-Path -LiteralPath $stage){
        if((Get-Item -LiteralPath $stage).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Refusing linked build staging folder'}
        if([IO.Path]::GetFullPath($stage)-ne (Join-Path $engine '.ProjectBuild')){throw 'Invalid staging folder'}
        Remove-Item -LiteralPath $stage -Recurse -Force
    }
    New-Item -ItemType Directory -Path $payload -Force | Out-Null
    $runtime=@($lines | ForEach-Object {
        if($_ -match '^content\s+'){'content "Content"'}
        elseif($_ -match '^(startupLevel|mainMenu|splash)\s+"(.+)"$' -and $matches[2].StartsWith($settings.content+'/')){$matches[1]+' "Content/'+$matches[2].Substring($settings.content.Length+1)+'"'}
        else{$_}
    })
    [IO.File]::WriteAllLines((Join-Path $payload 'Project.idkp'),$runtime,[Text.UTF8Encoding]::new($false))
    foreach($key in @('editorScene','startupLevel','mainMenu','splash')){
        $relative=$settings[$key]
        if($relative -and ($key -eq 'editorScene' -or !$relative.StartsWith($settings.content+'/'))){
            $source=ProjectPath $relative
            if(Test-Path -LiteralPath $source -PathType Leaf){
                $target=Join-Path $payload $relative
                New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
                Copy-Item -LiteralPath $source -Destination $target -Force
            }
        }
    }
    $env:IDK_RUNTIME_ROOT=$payload
    $env:IDK_INTERNAL_PROJECT_BUILD='1';$env:IDK_PROJECT_CONTENT=$content
    & (Join-Path $engine 'build.bat') release $Platform
    if($LASTEXITCODE-ne 0){throw 'Project compilation failed; the previous project Build was preserved'}
    $new=Join-Path $project ('.Build-'+[Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $new -Force | Out-Null
    Get-ChildItem -LiteralPath $stage | Copy-Item -Destination $new -Recurse -Force
    foreach($entry in Get-ChildItem -LiteralPath (Join-Path $new 'Contents') -Force){
        if($entry.PSIsContainer -or $entry.Name -notmatch '^content(0[1-9]|10)\.pak$'){throw "Unexpected unpacked game content: $($entry.Name)"}
    }
    foreach($entry in Get-ChildItem -LiteralPath $new -Force){
        if($entry.Name -notin @('Idontknow.exe','Contents','ThirdParty')){throw "Unexpected file in game build root: $($entry.Name)"}
    }
    if(Test-Path -LiteralPath $destination){
        $archive=Join-Path $engine ('Diagnostics/Archive/ProjectBuilds/'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Path (Split-Path $archive -Parent) -Force|Out-Null
        Move-Item -LiteralPath $destination -Destination $archive
    }
    Move-Item -LiteralPath $new -Destination $destination
    if([IO.Path]::GetFullPath($stage)-ne (Join-Path $engine '.ProjectBuild')){throw 'Invalid staging path'}
    Remove-Item -LiteralPath $stage -Recurse -Force
    Write-Output "Project build ready: $destination\Idontknow.exe"
}finally{
    $env:IDK_INTERNAL_PROJECT_BUILD=$previousInternal;$env:IDK_PROJECT_CONTENT=$previousContent
    $env:IDK_RUNTIME_ROOT=$previousRuntime
    if(Test-Path -LiteralPath $payload){
        if(!$payload.StartsWith((Join-Path $engine 'Diagnostics/ProjectPayload/'),[StringComparison]::OrdinalIgnoreCase)){throw 'Invalid payload cleanup path'}
        Remove-Item -LiteralPath $payload -Recurse -Force
    }
    if($lock){$lock.Dispose()}
}
