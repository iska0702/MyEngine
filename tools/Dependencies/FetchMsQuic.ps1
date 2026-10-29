param()
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path (Split-Path $PSScriptRoot -Parent) '..'))
$destination=Join-Path $root 'ThirdParty/Microsoft/MsQuic-2.6.3-OpenSSL'
if((Test-Path (Join-Path $destination 'build/native/include/msquic.h')) -and (Test-Path (Join-Path $destination 'build/native/bin/x64/msquic.dll'))){return}
$url='https://api.nuget.org/v3-flatcontainer/microsoft.native.quic.msquic.openssl/2.6.3/microsoft.native.quic.msquic.openssl.2.6.3.nupkg'
$sha='41505C3861EF386C8788DD6D9E0FFEA75B275B7D0651C2B940628AB729CF27BD'
New-Item -ItemType Directory -Force -Path $destination | Out-Null
$archive=Join-Path $destination 'msquic.zip'
Invoke-WebRequest $url -OutFile $archive
if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash-ne $sha){throw 'MsQuic package SHA-256 mismatch'}
Expand-Archive -LiteralPath $archive -DestinationPath $destination -Force
Write-Output 'Installed Microsoft MsQuic 2.6.3 OpenSSL runtime and headers (pinned package verified).'
