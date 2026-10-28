param([Parameter(Mandatory=$true)][string]$Directory)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../tools/Security/ContentCrypto.ps1')
$key=Get-ContentEncryptionKey
New-Item -ItemType Directory -Force -Path "$Directory/Content","$Directory/Saved" | Out-Null
[IO.File]::WriteAllText("$Directory/Content/A.txt",'file-A-secret',[Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText("$Directory/Content/B.txt",'file-B-secret',[Text.UTF8Encoding]::new($false))
# Independent .NET AesGcm API generates a fixed interop vector, without using our CNG wrapper.
$knownKey=[byte[]](0..31);$iv=[byte[]](32..47);$plain=[Text.Encoding]::UTF8.GetBytes('AES-256-GCM interoperability fixture')
$aad=[byte[]]($iv+[Text.Encoding]::UTF8.GetBytes('oracle'));$cipher=[byte[]]::new($plain.Length);$tag=[byte[]]::new(16)
$aes=[Security.Cryptography.AesGcm]::new($knownKey,16)
try{$aes.Encrypt([byte[]]$iv[0..11],$plain,$cipher,$tag,$aad)}finally{$aes.Dispose()}
$writer=[IO.BinaryWriter]::new([IO.File]::Create("$Directory/oracle.bin"))
try{$writer.Write($iv);$writer.Write($knownKey);$writer.Write([uint32]$plain.Length);$writer.Write($plain);$writer.Write($tag);$writer.Write($cipher)}finally{$writer.Dispose()}
# Legacy fixture exists only to exercise explicit editor migration.
$payload=[Text.Encoding]::UTF8.GetBytes("===SCENE_START===`nMODEL LegacyFixture`n===SCENE_END===`n===IMGUI_START===`nlegacy`n===IMGUI_END===`n")
$memory=[IO.MemoryStream]::new();$compress=[IO.Compression.DeflateStream]::new($memory,[IO.Compression.CompressionLevel]::Optimal,$true)
$compress.Write($payload,0,$payload.Length);$compress.Dispose();$compressed=$memory.ToArray();$memory.Dispose()
$cbc=[Security.Cryptography.Aes]::Create();$cbc.Key=[Text.Encoding]::ASCII.GetBytes('MyEngineAES256SecurePack2026!@#$');$cbc.GenerateIV()
$encrypt=$cbc.CreateEncryptor();$legacy=$encrypt.TransformFinalBlock($compressed,0,$compressed.Length);$encrypt.Dispose()
$writer=[IO.BinaryWriter]::new([IO.File]::Create("$Directory/Saved/Legacy.save"))
try{$writer.Write([uint32]0x45564153);$writer.Write([uint32]1);$writer.Write($cbc.IV);$writer.Write([uint32]$payload.Length);$writer.Write([uint32]$compressed.Length);$writer.Write([uint32]$legacy.Length);$writer.Write($legacy)}finally{$writer.Dispose();$cbc.Dispose()}
Push-Location $Directory
try{& (Join-Path $PSScriptRoot '../tools/Content/pack_all_content.ps1') -SourceContent "$Directory/Content"}finally{Pop-Location}
