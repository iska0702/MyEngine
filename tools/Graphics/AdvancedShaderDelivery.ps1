param(
    [ValidateSet('Support','Capture','Compile','Test','Register','Unregister')][string]$Action='Support',
    [string]$ProjectFile='',
    [string]$Database='',
    [int]$Adapter=0,
    [string]$Executable='',
    [ValidateSet('raster','dxr','vertex','tessellation')][string[]]$Modes=@('raster','dxr','vertex','tessellation')
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path (Split-Path $PSScriptRoot -Parent) '..'))
$sdk=Join-Path $root 'ThirdParty/AgilitySDK-1.619.6/build/native/bin/x64'
$compiler=Join-Path $sdk 'D3D12StateObjectCompiler.exe'
$config=Join-Path $sdk 'd3dconfig.exe'
function Invoke-Checked([string]$Tool,[string[]]$ToolArguments){
    & $Tool @ToolArguments
    if($LASTEXITCODE-ne 0){throw "$Tool failed ($LASTEXITCODE)"}
}
if(!(Test-Path -LiteralPath $compiler)){throw 'Install the pinned Agility SDK 1.619.6 in ThirdParty first.'}
if($Action-eq 'Support'){Invoke-Checked $compiler @('list','--adapters');return}
if(!$ProjectFile){$ProjectFile=Join-Path $root 'Projects/Test/Test.idkp'}
$ProjectFile=(Resolve-Path -LiteralPath $ProjectFile).Path
$project=Split-Path $ProjectFile -Parent
$output=Join-Path $project 'Saved/ASD'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$exe=Join-Path $root 'EditorBuild/Idontknow.exe'
if($Action-in @('Register','Unregister')){
    $identityLine=Get-Content -LiteralPath $ProjectFile | Where-Object {$_-match '^shaderAppId\s+"([0-9a-fA-F-]{36})"$'} | Select-Object -First 1
    if(!$identityLine){throw 'Open the project once in the editor to create its ASD identity'}
    [void]($identityLine-match '^shaderAppId\s+"([0-9a-fA-F-]{36})"$')
    $installer='Idontknow-ASD-'+$matches[1]
    if($Action-eq 'Unregister'){
        Invoke-Checked $compiler @('clear-registrations','--installer',$installer,'--scope','user')
        return
    }
    if(!$Database){throw 'Specify -Database with the tested .psdb path'}
    $Database=(Resolve-Path -LiteralPath $Database).Path
    if([IO.Path]::GetExtension($Database)-ne '.psdb'){throw 'Register requires a .psdb'}
    if(!$Executable){$Executable=$exe}
    $Executable=(Resolve-Path -LiteralPath $Executable).Path
    Invoke-Checked $compiler @('register','--installer',$installer,'--scope','user','--exe-path',$Executable,'--psdb',$Database)
    Write-Output "Registered a local driver-specific PSDB for $Executable. Recompile and register again after driver or shader updates."
    return
}
if($Action-eq 'Compile'){
    if(!$Database){throw 'Specify -Database with the captured .sodb path'}
    $Database=(Resolve-Path -LiteralPath $Database).Path
    if([IO.Path]::GetExtension($Database)-ne '.sodb'){throw 'Compile requires an .sodb'}
    $psdb=[IO.Path]::ChangeExtension($Database,'.psdb')
    if(Test-Path -LiteralPath $psdb){throw 'PSDB already exists; choose a fresh capture to avoid overwriting it'}
    $report=@(& $compiler compile --adapter "$Adapter" $Database $psdb 2>&1)
    $report | Tee-Object -FilePath ([IO.Path]::ChangeExtension($Database,'.compile.log')) | Write-Output
    if($LASTEXITCODE-ne 0){throw "Offline compilation failed ($LASTEXITCODE)"}
    if($report -match '\([1-9][0-9]* failed\)'){
        Write-Warning 'The driver compiler rejected some pipelines. The PSDB is partial; rejected entries use normal runtime compilation. See the compile log.'
    }
    Write-Output "Local driver-specific PSDB: $psdb"
    return
}
if(Get-Process Idontknow -ErrorAction SilentlyContinue){throw 'Close engine instances before ASD capture/testing'}
New-Item -ItemType Directory -Path (Join-Path $root 'Diagnostics') -Force | Out-Null
$lock=[IO.File]::Open((Join-Path $root 'Diagnostics/asd.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
$previousVerification=$env:IDK_ASD_VERIFICATION
$env:IDK_ASD_VERIFICATION='1'
try{
    # d3dconfig options are global for its app list. Preserve the values we touch;
    # never use --reset, which would erase unrelated developer settings.
    $device=@(& $config device);if($LASTEXITCODE-ne 0){throw 'Cannot read d3dconfig settings'}
    $previous=@{}
    foreach($line in $device){if($line-match '^\s*(enable-pso-db|pso-db-path|precompiled-db-path|precompiled-db-registration-override)\s*=\s*(.*)$'){$previous[$matches[1]]=$matches[2].Trim()}}
    foreach($key in @('enable-pso-db','pso-db-path','precompiled-db-path','precompiled-db-registration-override')){
        if(!$previous.ContainsKey($key)){throw "Cannot safely preserve d3dconfig option: $key"}
    }
    $apps=@(& $config apps);if($LASTEXITCODE-ne 0){throw 'Cannot read d3dconfig applications'}
    $otherApps=@($apps | ForEach-Object {$_.Trim()} | Where-Object {$_ -and $_ -ne 'apps' -and $_ -notmatch '^[-]+$|^<no apps>$' -and $_ -ine $exe})
    if($otherApps.Count){throw 'Other d3dconfig apps are configured; capture settings would affect them. Preserve them and run this workflow in a separate Windows developer account.'}
    $alreadyRegistered=@($apps | Where-Object { $_.Trim()-ieq $exe }).Count-gt 0
    $added=$false
    $snapshot=Join-Path $output ('d3dconfig-before-'+[Guid]::NewGuid().ToString('N')+'.xml')
    $snapshotReady=$false
    try{
        if(!$alreadyRegistered){Invoke-Checked $config @('apps','--add',$exe);$added=$true}
        Invoke-Checked $config @('--export',$snapshot)
        [xml]$savedConfiguration=Get-Content -LiteralPath $snapshot
        if($savedConfiguration.DocumentElement.Name-ne 'Direct3D'){throw 'Invalid Direct3D configuration snapshot'}
        $snapshotReady=$true
        if($Action-eq 'Capture'){
            $Database=Join-Path $output ('capture-'+[DateTime]::Now.ToString('yyyyMMdd-HHmmss-fff')+'.sodb')
            Invoke-Checked $config @('device',"pso-db-path=$Database")
            Invoke-Checked $config @('device','enable-pso-db=true')
        }else{
            if(!$Database){throw 'Specify -Database with the .psdb path'}
            $Database=(Resolve-Path -LiteralPath $Database).Path
            if([IO.Path]::GetExtension($Database)-ne '.psdb'){throw 'Test requires a .psdb'}
            Invoke-Checked $config @('device','enable-pso-db=false')
            Invoke-Checked $config @('device',"precompiled-db-path=$Database")
            Invoke-Checked $config @('device','precompiled-db-registration-override=true')
        }
        foreach($mode in $Modes){
            $arguments='--benchmark 120 --benchmark-native --benchmark-no-vsync --benchmark-editor-ui'
            if($mode-ne 'dxr'){$arguments+=' --benchmark-no-raytracing'}
            if($mode-eq 'vertex'){$arguments+=' --benchmark-vertex-pipeline'}
            if($mode-eq 'tessellation'){$arguments+=' --benchmark-tessellation'}
            & (Join-Path (Split-Path $PSScriptRoot -Parent) 'Diagnostics/RunBenchmark.ps1') -Name "asd_$($Action.ToLower())_$mode" -ProjectFile $ProjectFile -Arguments $arguments
            $log=Join-Path $root "Diagnostics/Benchmarks/EditorBuild/asd_$($Action.ToLower())_$mode/run.log"
            $statistics=@(Select-String -LiteralPath $log -Pattern 'ASD statistics:' | ForEach-Object {$_.Line})
            $statistics | Write-Output
            if(!(Select-String -LiteralPath $log -SimpleMatch 'project identity set' -Quiet)){throw 'D3D12 application identity was not set; inspect the runtime log'}
            if($Action-eq 'Test' -and !(Select-String -LiteralPath $log -SimpleMatch 'ASD statistics: defaultPSDB=1' -Quiet)){
                throw 'The runtime did not report a registered default PSDB; inspect identity and driver compatibility'
            }
        }
    }finally{
        # Empty path assignments are interpreted as queries by d3dconfig, so use
        # its XML import to restore empty values exactly as well as nonempty ones.
        if($snapshotReady){Invoke-Checked $config @('--import',$snapshot)}
        if($added){Invoke-Checked $config @('apps','--remove',$exe)}
    }
    if($Action-eq 'Capture'){
        if(!(Test-Path -LiteralPath $Database)){throw 'The runtime did not create an SODB'}
        Invoke-Checked $compiler @('checkpoint-db',$Database)
        Invoke-Checked $compiler @('get-identity',$Database)
        Write-Output "Captured SODB: $Database"
    }else{
        Write-Output 'PSDB playback completed. Inspect the reported PSDB misses and unknown results; D3D12CacheListener (ETW) can provide additional detail.'
    }
}finally{$lock.Dispose();$env:IDK_ASD_VERIFICATION=$previousVerification}
