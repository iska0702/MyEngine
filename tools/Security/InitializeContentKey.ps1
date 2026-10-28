. (Join-Path (Split-Path $PSScriptRoot -Parent) 'Security/ContentCrypto.ps1')
$key=Get-ContentEncryptionKey
[Array]::Clear($key,0,$key.Length)
