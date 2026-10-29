$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
$version='1.619.6'
$root=[IO.Path]::GetFullPath((Join-Path (Split-Path $PSScriptRoot -Parent) '..'))
$archive=Join-Path ([IO.Path]::GetTempPath()) ('idk-agility-'+[Guid]::NewGuid().ToString('N')+'.zip')
try{
    Invoke-WebRequest "https://api.nuget.org/v3-flatcontainer/microsoft.direct3d.d3d12/$version/microsoft.direct3d.d3d12.$version.nupkg" -OutFile $archive
    if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash-ne '08F0489281401AA430FC37322D6C3FC98A8025175AACD714C10D562F4963F1E9'){throw 'Agility SDK package hash mismatch'}
    $destination=Join-Path $root "ThirdParty/Microsoft/AgilitySDK-$version"
    Expand-Archive -LiteralPath $archive -DestinationPath $destination -Force
    $signature=Get-AuthenticodeSignature (Join-Path $destination 'build/native/bin/x64/D3D12Core.dll')
    if($signature.Status-ne 'Valid'){throw 'Agility SDK runtime signature is not valid'}
    Write-Output "Installed Microsoft Agility SDK $version"
}finally{if(Test-Path -LiteralPath $archive){Remove-Item -LiteralPath $archive -Force}}
