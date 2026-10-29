param(
    [ValidateSet('Install','Status','Remove')][string]$Action='Install',
    [string]$ProjectFile='',
    [string]$Database='',
    [string[]]$Executables=@(),
    [switch]$RequireRegistration
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path (Split-Path $PSScriptRoot -Parent) '..'))
$sdk=Join-Path $root 'ThirdParty/Microsoft/AgilitySDK-1.619.6/build/native/bin/x64'
$config=Join-Path $sdk 'd3dconfig.exe'
$compiler=Join-Path $sdk 'D3D12StateObjectCompiler.exe'
$state=Join-Path $root 'Diagnostics/asd-local-install.json'
New-Item -ItemType Directory (Split-Path $state) -Force | Out-Null
$lock=[IO.File]::Open((Join-Path $root 'Diagnostics/asd.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
function Checked([string]$Tool,[string[]]$Arguments){
    & $Tool @Arguments | Out-Host
    if($LASTEXITCODE-ne 0){throw "$Tool failed ($LASTEXITCODE)"}
}
function SaveRecord($Record){
    $temporary=$state+'.tmp'
    $Record | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $temporary
    [IO.File]::Move($temporary,$state,$true)
}
function ExportConfiguration([string]$Path){
    Checked $config @('--export',$Path)
    [xml]$xml=Get-Content -LiteralPath $Path
    if($xml.DocumentElement.Name-ne 'Direct3D'){throw 'Invalid Direct3D configuration'}
    return $xml
}
function AppNode($Xml,[string]$Exe){
    return @($Xml.SelectNodes('/Direct3D/Direct3D12/*') | Where-Object {$_.LocalName -match '^Application\d+$' -and $_.SelectSingleNode('Name').GetAttribute('Value') -ieq $Exe}) | Select-Object -First 1
}
function Behaviors($Node){
    $result=[ordered]@{}
    foreach($pair in $Node.SelectSingleNode('D3DBehaviors').GetAttribute('Value').Split(';')){
        $parts=$pair.Split('=',2);if($parts.Count-eq 2){$result[$parts[0]]=$parts[1]}
    }
    return $result
}
function SetBehaviors($Node,$Values){
    $Node.SelectSingleNode('D3DBehaviors').SetAttribute('Value',(($Values.Keys | ForEach-Object {$_+'='+$Values[$_]}) -join ';'))
}
try{
    $installed=if(Test-Path -LiteralPath $state){Get-Content -LiteralPath $state -Raw | ConvertFrom-Json}else{$null}
    if($Action-eq 'Status'){
        if(!$installed){Write-Output 'No local ASD installation managed by this engine.';return}
        $current=ExportConfiguration (Join-Path $root 'Diagnostics/asd-status.xml')
        Write-Output "Mode: $($installed.Mode); project: $($installed.Project); database: $($installed.Database)"
        if(!(Test-Path -LiteralPath $installed.Database) -or (Get-FileHash -LiteralPath $installed.Database).Hash-ne $installed.Hash){throw 'Installed PSDB is missing or changed'}
        foreach($entry in $installed.Applications){
            if($installed.Mode-eq 'DeveloperOverride'){
                $node=AppNode $current $entry.Executable
                if(!$node){throw "Missing application binding: $($entry.Executable)"}
                $values=Behaviors $node
                if($values.PrecompiledDatabasePath-ne $installed.Database -or $values.PrecompiledDatabaseRegistrationOverride-ne '1' -or $values.PipelineStateDatabaseEnabled-ne '0'){throw 'ASD configuration changed outside this installer'}
            }
            Write-Output "Bound: $($entry.Executable)"
        }
        Checked $compiler @('list-registrations','--installer',$installed.Installer,'--scope','user')
        return
    }
    if(Get-Process Idontknow -ErrorAction SilentlyContinue){throw 'Close the editor/game before changing ASD bindings'}
    if($Action-eq 'Remove'){
        if(!$installed){Write-Output 'No managed local ASD installation.';return}
        if($installed.Mode-eq 'Registered'){
            Checked $compiler @('clear-registrations','--installer',$installed.Installer,'--scope','user')
        }else{
            $path=Join-Path $root 'Diagnostics/asd-remove.xml'
            $xml=ExportConfiguration $path
            foreach($entry in $installed.Applications){
                $node=AppNode $xml $entry.Executable
                if(!$node){throw 'Application binding changed; refusing to overwrite external changes'}
                $values=Behaviors $node
                if($values.PrecompiledDatabasePath-ne $installed.Database -or $values.PrecompiledDatabaseRegistrationOverride-ne '1' -or $values.PipelineStateDatabaseEnabled-ne '0'){throw 'ASD fields changed; refusing to overwrite external changes'}
                foreach($property in $entry.Previous.PSObject.Properties){$values[$property.Name]=[string]$property.Value}
                SetBehaviors $node $values
            }
            $xml.Save($path);Checked $config @('--import',$path)
            foreach($entry in $installed.Applications){
                if($entry.Added){
                    $node=AppNode $xml $entry.Executable
                    if($node.OuterXml-eq $entry.PreviousXml){Checked $config @('apps','--remove',$entry.Executable)}
                    else{Write-Output "Retained app entry with externally changed non-ASD options: $($entry.Executable)"}
                }
            }
        }
        Move-Item -LiteralPath $state -Destination ($state+'.removed-'+[DateTime]::Now.ToString('yyyyMMdd-HHmmss'))
        Write-Output 'Local ASD bindings removed; previous app settings restored. Databases retained.'
        return
    }
    if($installed){throw 'A local ASD installation already exists. Use Status or Remove before installing a new database.'}
    if(!$Database){throw 'Specify a tested .psdb with -Database'}
    $Database=(Resolve-Path -LiteralPath $Database).Path
    if([IO.Path]::GetExtension($Database)-ne '.psdb'){throw 'A .psdb is required'}
    if(!$ProjectFile){$ProjectFile=Join-Path $root 'Projects/Test/Test.idkp'}
    $ProjectFile=(Resolve-Path -LiteralPath $ProjectFile).Path
    $identity=Get-Content -LiteralPath $ProjectFile | Where-Object {$_ -match '^shaderAppId\s+"([0-9a-fA-F-]{36})"$'} | Select-Object -First 1
    if(!$identity -or $identity -notmatch '^shaderAppId\s+"([0-9a-fA-F-]{36})"$'){throw 'Project has no shader application identity'}
    $installer='Idontknow-ASD-'+$matches[1]
    if(!$Executables.Count){
        $Executables=@((Join-Path $root 'EditorBuild/Idontknow.exe'))
        $game=Join-Path (Split-Path $ProjectFile) 'Build/Idontknow.exe'
        if(Test-Path -LiteralPath $game){$Executables+= $game}
    }
    $Executables=@($Executables | ForEach-Object {(Resolve-Path -LiteralPath $_).Path} | Select-Object -Unique)
    $existing=@(& $compiler list-registrations --installer $installer --scope user)
    if($LASTEXITCODE-ne 0 -or ($existing -join "`n") -notmatch 'No Registered Applications'){throw 'Existing native registrations must be preserved; remove them explicitly before using this local installer'}
    $hash=(Get-FileHash -LiteralPath $Database).Hash
    $folder=Join-Path (Split-Path $ProjectFile) ('Saved/ASD/Installed/'+$hash.ToLowerInvariant())
    New-Item -ItemType Directory -Path $folder -Force | Out-Null
    $target=Join-Path $folder 'shader.psdb'
    if($target.Contains(';')){throw 'Direct3D configuration does not support a semicolon in this database path'}
    if($Database-ine $target){Copy-Item -LiteralPath $Database -Destination $target -Force}
    $sodb=[IO.Path]::ChangeExtension($Database,'.sodb')
    if(Test-Path -LiteralPath $sodb){Copy-Item -LiteralPath $sodb -Destination (Join-Path $folder 'shader.sodb') -Force}
    $record=[ordered]@{Version=1;Project=$ProjectFile;Installer=$installer;Database=$target;Hash=$hash;Mode='Registered';Applications=@()}
    $registration=@()
    foreach($exe in $Executables){
        $result=@(& $compiler register --installer $installer --scope user --exe-path $exe --psdb $target 2>&1)
        $exit=$LASTEXITCODE;$registration+= $result
        if($exit-ne 0){break}
        $record.Applications+=@{Executable=$exe;Added=$false;Previous=@{}}
    }
    $registration | Set-Content (Join-Path $folder 'registration.log')
    if($exit-ne 0){
        # Roll back only successful registrations made by this operation.
        if($record.Applications.Count){Checked $compiler @('clear-registrations','--installer',$installer,'--scope','user')}
        if($RequireRegistration -or ($registration -join "`n") -notmatch '0x80070006'){throw "Native registration failed; see $folder/registration.log"}
        Write-Output 'Native registration failed (0x80070006). Connecting a persistent, per-application developer override through Microsoft d3dconfig XML import.'
        $record.Mode='DeveloperOverride';$record.Applications=@()
        $added=@();$snapshot=Join-Path $folder 'before.xml';$snapshotReady=$false
        try{
            $apps=@(& $config apps);if($LASTEXITCODE-ne 0){throw 'Cannot list Direct3D apps'}
            foreach($exe in $Executables){
                $new=@($apps | Where-Object {$_.Trim() -ieq $exe}).Count-eq 0
                if($new){Checked $config @('apps','--add',$exe);$added+=$exe}
            }
            $xml=ExportConfiguration $snapshot;$snapshotReady=$true
            foreach($exe in $Executables){
                $node=AppNode $xml $exe;if(!$node){throw 'New app missing from exported configuration'}
                $previousXml=$node.OuterXml
                $values=Behaviors $node;$previous=[ordered]@{}
                foreach($key in @('PrecompiledDatabasePath','PrecompiledDatabaseRegistrationOverride','PipelineStateDatabaseEnabled')){
                    if(!$values.Contains($key)){throw "Missing Direct3D option: $key"};$previous[$key]=$values[$key]
                }
                $values.PrecompiledDatabasePath=$target;$values.PrecompiledDatabaseRegistrationOverride='1';$values.PipelineStateDatabaseEnabled='0'
                SetBehaviors $node $values
                $record.Applications+=@{Executable=$exe;Added=($added -contains $exe);Previous=$previous;PreviousXml=$previousXml}
            }
            $applied=Join-Path $folder 'applied.xml';$xml.Save($applied);Checked $config @('--import',$applied)
            SaveRecord $record
        }catch{
            if($snapshotReady){Checked $config @('--import',$snapshot)}
            foreach($exe in $added){Checked $config @('apps','--remove',$exe)}
            throw
        }
    }else{SaveRecord $record}
    Write-Output "ASD connected for ordinary launches. Mode: $($record.Mode). Verify defaultPSDB and cache misses in a new runtime session."
}finally{$lock.Dispose()}
