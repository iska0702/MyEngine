param()
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path (Split-Path $PSScriptRoot -Parent) '..'))
$destination=Join-Path $root 'ThirdParty/MsQuic-2.6.1-OpenSSL'
if((Test-Path (Join-Path $destination 'build/native/include/msquic.h')) -and (Test-Path (Join-Path $destination 'build/native/bin/x64/msquic.dll'))){return}
$url='https://api.nuget.org/v3-flatcontainer/microsoft.native.quic.msquic.openssl/2.6.1/microsoft.native.quic.msquic.openssl.2.6.1.nupkg'
$sha='E8BFDD6A4D4E561FD5213EAB3C617D231D2F2E5E926E3D3DE682FFA540938B08'
New-Item -ItemType Directory -Force -Path $destination | Out-Null
$archive=Join-Path $destination 'msquic.zip'
Invoke-WebRequest $url -OutFile $archive
if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash-ne $sha){throw 'MsQuic package SHA-256 mismatch'}
Expand-Archive -LiteralPath $archive -DestinationPath $destination -Force
Write-Output 'Installed Microsoft MsQuic 2.6.1 OpenSSL runtime and headers (pinned package verified).'
