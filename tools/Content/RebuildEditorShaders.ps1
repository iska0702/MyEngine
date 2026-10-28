# Shader-only iteration; no compiler object files or C++ rebuild required.
$ErrorActionPreference='Stop'
$engine=[IO.Path]::GetFullPath((Join-Path (Split-Path $PSScriptRoot -Parent) '..'))
$editor=Join-Path $engine 'EditorBuild'
if(!(Test-Path -LiteralPath (Join-Path $editor 'Idontknow.exe'))){throw 'Build the editor first'}
if(Get-Process Idontknow -ErrorAction SilentlyContinue | Where-Object {$_.Path -eq (Join-Path $editor 'Idontknow.exe')}){throw 'Close the editor before updating its shader archives'}
$dxc='D:\Windows Kits\10\bin\10.0.26100.0\x64\dxc.exe'
if(!(Test-Path -LiteralPath $dxc)){$dxc=Join-Path $engine 'ThirdParty/FidelityFX-SDK-v1.1.4/sdk/tools/ffx_shader_compiler/libs/dxc/bin/x64/dxc.exe'}
$stage=Join-Path $engine ('Diagnostics/ShaderBuilds/'+[Guid]::NewGuid().ToString('N'))
$archive=Join-Path $engine ('Diagnostics/Archive/ShaderRebuilds/'+[Guid]::NewGuid().ToString('N'))
$previousRuntime=$env:IDK_RUNTIME_ROOT
try{
    $env:IDK_RUNTIME_ROOT=$null
    $shaders=Join-Path $stage 'Contents/shaders'
    New-Item -ItemType Directory -Path $shaders -Force | Out-Null
    $count=0
    foreach($line in [IO.File]::ReadAllLines((Join-Path $engine 'build.bat'))){
        if($line -match '^\s*%DXC_EXE%\s+-T\s+(\S+)(?:\s+-E\s+(\S+))?\s+-Fo\s+Contents\\shaders\\(\S+)\s+\.\.\\shaders\\(\S+)'){
            $profile=$matches[1];$entry=$matches[2];$output=$matches[3];$source=$matches[4]
            $compilerArgs=@('-T',$profile,'-Fo',(Join-Path $shaders $output),(Join-Path $engine ('shaders/'+$source)))
            if($entry){$compilerArgs+=@('-E',$entry)}
            & $dxc @compilerArgs
            if($LASTEXITCODE){throw "Shader compilation failed: $source $entry"}
            ++$count
        }
    }
    if($count -lt 20){throw 'Windows shader compile list was not found'}
    Get-ChildItem -LiteralPath (Join-Path $engine 'shaders') -Filter '*.hlsl' | Copy-Item -Destination $shaders
    Push-Location $stage
    try{& (Join-Path $engine 'tools/Content/pack_all_content.ps1') -SourceContent (Join-Path $engine 'Templates/Default/Content')}finally{Pop-Location}
    & (Join-Path $engine 'tools/Security/generate_dll_manifest.ps1') -TargetDir (Join-Path $editor 'ThirdParty') -TargetPak (Join-Path $stage 'Contents/content01.pak')
    New-Item -ItemType Directory -Path $archive -Force | Out-Null
    $packed=Get-ChildItem -LiteralPath (Join-Path $stage 'Contents') -Filter '*.pak'
    foreach($file in $packed){Copy-Item -LiteralPath (Join-Path $editor ('Contents/'+$file.Name)) -Destination $archive}
    try{foreach($file in $packed){Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $editor 'Contents') -Force}}
    catch{Get-ChildItem -LiteralPath $archive -Filter '*.pak' | Copy-Item -Destination (Join-Path $editor 'Contents') -Force;throw}
    Write-Output "Editor shaders updated: $count compiled shaders, 9 PAK archives."
}finally{
    $env:IDK_RUNTIME_ROOT=$previousRuntime
    if(Test-Path -LiteralPath $stage){
        if(!$stage.StartsWith((Join-Path $engine 'Diagnostics/ShaderBuilds/'),[StringComparison]::OrdinalIgnoreCase)){throw 'Invalid shader staging path'}
        Remove-Item -LiteralPath $stage -Recurse -Force
    }
}
