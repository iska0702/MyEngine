# generate_dll_manifest.ps1
# Generates cryptographic SHA-256 integrity manifest with HMAC-SHA256 digital signature,
# and embeds it directly into a content archive (default: Contents\content01.pak) as EPK3 authenticated file entry.
param (
    [string]$TargetDir = "ThirdParty",
    [string]$TargetPak = "Contents\content01.pak"
)

$ErrorActionPreference = 'Stop'

$targetPath = (Resolve-Path $TargetDir).Path
$targetPakFullPath = [System.IO.Path]::GetFullPath($TargetPak)

# 1. Master signing secret key matching engine binary


# 2. 256-bit AES Master Key for EPAK Encryption
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'Security/ContentCrypto.ps1')
$AesKey = Get-ContentEncryptionKey
$masterKey = [IdkFileCrypto]::Derive($AesKey,"Idontknow/file-signing/v1")

function Compress-Data([byte[]]$data) {
    $ms = New-Object System.IO.MemoryStream
    $deflate = New-Object System.IO.Compression.DeflateStream($ms, [System.IO.Compression.CompressionLevel]::Optimal)
    $deflate.Write($data, 0, $data.Length)
    $deflate.Close()
    $res = $ms.ToArray()
    $ms.Close()
    return $res
}





# 3. Gather and hash all DLLs
$lines = @()
Get-ChildItem -Path $targetPath -Recurse -Filter "*.dll" | Sort-Object FullName | ForEach-Object {
    $full = $_.FullName
    $rel = $full.Substring($targetPath.Length).TrimStart('\', '/').Replace('\', '/')
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    $stream = [System.IO.File]::OpenRead($full)
    try {
        $hash = [System.BitConverter]::ToString($sha256.ComputeHash($stream)).Replace('-', '').ToLowerInvariant()
    } finally {
        $stream.Dispose()
        $sha256.Dispose()
    }
    $lines += "${rel}:${hash}"
}

# 4. Construct payload body and compute HMAC-SHA256
$body = [string]::Join("`r`n", $lines) + "`r`n"
$bodyBytes = [System.Text.Encoding]::UTF8.GetBytes($body)

$hmac = [System.Security.Cryptography.HMACSHA256]::new($masterKey)
$sigBytes = $hmac.ComputeHash($bodyBytes)
$sigHex = -join ($sigBytes | ForEach-Object { '{0:x2}' -f $_ })

$manifestContent = "HMAC-SHA256:${sigHex}`r`n${body}"
$rawBytes = [System.Text.Encoding]::UTF8.GetBytes($manifestContent)
$uncompressedLen = $rawBytes.Length

# 5. Compress & Encrypt file payload
$compBytes = Compress-Data -data $rawBytes
$isCompressed = 1
$compressedLen = $compBytes.Length
$enc = Encrypt-Data -plainBytes $compBytes -key $AesKey -Context "file/security/integrity.manifest"

$manifestBlob = New-Object byte[] (16 + $enc.Cipher.Length)
[System.Buffer]::BlockCopy($enc.IV, 0, $manifestBlob, 0, 16)
[System.Buffer]::BlockCopy($enc.Cipher, 0, $manifestBlob, 16, $enc.Cipher.Length)

$manifestKey = "security/integrity.manifest"
$paddedKey = $manifestKey.PadRight(128).Substring(0, 128)
$manifestNameBytes = [System.Text.Encoding]::Unicode.GetBytes($paddedKey)

$manifestRecord = [PSCustomObject]@{
    Name = $manifestKey
    NameBytes = $manifestNameBytes
    DataBlob = $manifestBlob
    CipherTotalLength = $manifestBlob.Length
    CompressedLength = $compressedLen
    UncompressedLength = $uncompressedLen
    Flags = 1 # Deflate compressed
}

# 5b. Construct & Embed Signed Cryptographic License Container (240 bytes)
$licMagic = [System.Text.Encoding]::ASCII.GetBytes("MYENG_LIC_V1`0`0`0`0")
$payloadBytes = New-Object byte[] 188
$payloadStream = New-Object System.IO.MemoryStream(,$payloadBytes)
$pw = New-Object System.IO.BinaryWriter($payloadStream)

# hwidHash: 0 (Universal Retail Distribution)
$pw.Write([uint64]0)
# boundHwid: char[32]
$boundHwidBytes = [System.Text.Encoding]::ASCII.GetBytes("UNIVERSAL_DISTRIBUTION".PadRight(32, [char]0))
$pw.Write($boundHwidBytes, 0, 32)
# productName: char[64]
$prodNameBytes = [System.Text.Encoding]::ASCII.GetBytes("Idontknow Commercial Release".PadRight(64, [char]0))
$pw.Write($prodNameBytes, 0, 64)
# licensee: char[64]
$licenseeBytes = [System.Text.Encoding]::ASCII.GetBytes("Licensed Game User".PadRight(64, [char]0))
$pw.Write($licenseeBytes, 0, 64)
# issueTimestamp: uint64
$nowUnix = [uint64][System.DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
$pw.Write($nowUnix)
# expiryTimestamp: uint64 (0 = lifetime)
$pw.Write([uint64]0)
# flags: uint32 (1 = Active)
$pw.Write([uint32]1)
$pw.Flush()
$pw.Close()
$payloadStream.Close()

# HMAC-SHA256 signature of payload using $masterKey
$hmacLic = [System.Security.Cryptography.HMACSHA256]::new($masterKey)
$licSig = $hmacLic.ComputeHash($payloadBytes)

# Assemble LicenseContainer (240 bytes: 16 magic + 4 size + 188 payload + 32 sig)
$licContainerBytes = New-Object byte[] 240
[System.Buffer]::BlockCopy($licMagic, 0, $licContainerBytes, 0, 16)
$sizeBytes = [System.BitConverter]::GetBytes([uint32]188)
[System.Buffer]::BlockCopy($sizeBytes, 0, $licContainerBytes, 16, 4)
[System.Buffer]::BlockCopy($payloadBytes, 0, $licContainerBytes, 20, 188)
[System.Buffer]::BlockCopy($licSig, 0, $licContainerBytes, 208, 32)

$licCompBytes = Compress-Data -data $licContainerBytes
$licEnc = Encrypt-Data -plainBytes $licCompBytes -key $AesKey -Context "file/security/license.lic"

$licBlob = New-Object byte[] (16 + $licEnc.Cipher.Length)
[System.Buffer]::BlockCopy($licEnc.IV, 0, $licBlob, 0, 16)
[System.Buffer]::BlockCopy($licEnc.Cipher, 0, $licBlob, 16, $licEnc.Cipher.Length)

$licKey = "security/license.lic"
$paddedLicKey = $licKey.PadRight(128).Substring(0, 128)
$licNameBytes = [System.Text.Encoding]::Unicode.GetBytes($paddedLicKey)

$licenseRecord = [PSCustomObject]@{
    Name = $licKey
    NameBytes = $licNameBytes
    DataBlob = $licBlob
    CipherTotalLength = $licBlob.Length
    CompressedLength = $licCompBytes.Length
    UncompressedLength = $licContainerBytes.Length
    Flags = 1 # Deflate compressed
}

# 6. Read existing target PAK or initialize new file record list
$fileRecords = @()

if (Test-Path $targetPakFullPath) {
    $existingBytes = [System.IO.File]::ReadAllBytes($targetPakFullPath)
    if($existingBytes.Length-lt 48){throw 'Existing archive is truncated; refusing to replace it'}
    if ($existingBytes.Length -ge 28) {
        $ms = New-Object System.IO.MemoryStream(,$existingBytes)
        $br = New-Object System.IO.BinaryReader($ms)
        $magic = $br.ReadUInt32()
        if($magic-ne 0x334B5045){throw 'Existing archive is not authenticated EPK3; rebuild its content first'}
        if ($magic -eq 0x334B5045) { # 'EPAK'
            $indexIV = $br.ReadBytes(16)
            $indexCipherSize = $br.ReadUInt32()
            $indexPlainSize = $br.ReadUInt32()
            if($indexCipherSize-lt 20-or $indexCipherSize-gt 20MB-or $indexCipherSize-ne [uint64]$indexPlainSize+16-or 28L+$indexCipherSize-gt $existingBytes.Length){throw 'Existing archive index bounds are invalid'}
            $indexCipher = $br.ReadBytes($indexCipherSize)

            try {
                $plainIndex = Decrypt-Data -cipherBytes $indexCipher -iv $indexIV -key $AesKey
                $idxMs = New-Object System.IO.MemoryStream(,$plainIndex)
                $idxBr = New-Object System.IO.BinaryReader($idxMs)
                $numFiles = $idxBr.ReadUInt32()
                if($plainIndex.Length-ne $indexPlainSize-or $numFiles-gt 65536-or $plainIndex.Length-ne 4L+276L*$numFiles){throw 'Existing archive index size is invalid'}

                for ($i = 0; $i -lt $numFiles; $i++) {
                    $nameBytes = $idxBr.ReadBytes(256)
                    $name = [System.Text.Encoding]::Unicode.GetString($nameBytes).TrimEnd("`0", " ")
                    $offset = $idxBr.ReadUInt32()
                    $cipherLen = $idxBr.ReadUInt32()
                    $cLen = $idxBr.ReadUInt32()
                    $uLen = $idxBr.ReadUInt32()
                    $flags = $idxBr.ReadUInt32()
                    if($offset-lt 28L+$indexCipherSize-or $cipherLen-lt 32-or [uint64]$offset+$cipherLen-gt $existingBytes.Length-or $cLen-ne $cipherLen-32-or $uLen-gt 512MB-or $flags-gt 1){throw 'Existing archive payload bounds are invalid'}

                    # Don't duplicate if already existing
                    if ($name -eq "security/integrity.manifest" -or $name -eq "integrity.manifest" -or
                        $name -eq "security/license.lic" -or $name -eq "license.lic") {
                        continue
                    }

                    $ms.Seek($offset, [System.IO.SeekOrigin]::Begin) | Out-Null
                    $dataBlob = $br.ReadBytes($cipherLen)
                    if($dataBlob.Length-ne $cipherLen){throw "Truncated existing payload"}
                    $oldIv=[byte[]]$dataBlob[0..15];$oldCipher=[byte[]]$dataBlob[16..($dataBlob.Length-1)]
                    $verified=Decrypt-Data $oldCipher $oldIv $AesKey ("file/"+$name.Replace("\","/"))
                    if($verified.Length-ne $cLen){throw 'Existing archive compressed size mismatch'}

                    $fileRecords += [PSCustomObject]@{
                        Name = $name
                        NameBytes = $nameBytes
                        DataBlob = $dataBlob
                        CipherTotalLength = $cipherLen
                        CompressedLength = $cLen
                        UncompressedLength = $uLen
                        Flags = $flags
                    }
                }
            } catch {
                throw "Existing PAK failed authentication; refusing to discard its content: $_"
            }
        }
        $br.Close()
        $ms.Close()
    }
}

# Add manifest and license records to the archive entries
$fileRecords += $manifestRecord
$fileRecords += $licenseRecord

# 7. Build updated Index & Encrypt
$newFileCount = $fileRecords.Count
$dummyIndexBytes = New-Object byte[] (4 + ($newFileCount * 276))
$dummyEncIndex = Encrypt-Data -plainBytes $dummyIndexBytes -key $AesKey
$indexCipherSize = $dummyEncIndex.Cipher.Length
$headerSize = 4 + 16 + 4 + 4 + $indexCipherSize

$plainIndexStream = New-Object System.IO.MemoryStream
$indexWriter = New-Object System.IO.BinaryWriter($plainIndexStream)
$indexWriter.Write([uint32]$newFileCount)

$currentOffset = $headerSize
foreach ($r in $fileRecords) {
    $indexWriter.Write($r.NameBytes)
    $indexWriter.Write([uint32]$currentOffset)
    $indexWriter.Write([uint32]$r.CipherTotalLength)
    $indexWriter.Write([uint32]$r.CompressedLength)
    $indexWriter.Write([uint32]$r.UncompressedLength)
    $indexWriter.Write([uint32]$r.Flags)
    $currentOffset += $r.CipherTotalLength
}
$indexWriter.Flush()
$newPlainIndexBytes = $plainIndexStream.ToArray()
$indexWriter.Close()
$plainIndexStream.Close()

$newEncIndex = Encrypt-Data -plainBytes $newPlainIndexBytes -key $AesKey

# 8. Write EPK3 authenticated file atomically
$pakDir = [System.IO.Path]::GetDirectoryName($targetPakFullPath)
if (-not (Test-Path $pakDir)) { New-Item -ItemType Directory -Path $pakDir -Force > $null }

$tempPak = "${targetPakFullPath}.tmp"
$f = [System.IO.File]::Create($tempPak)
$b = New-Object System.IO.BinaryWriter($f)

# 'E' 'P' 'A' 'K'
$b.Write([byte]0x45)
$b.Write([byte]0x50)
$b.Write([byte]0x4B)
$b.Write([byte]0x33)
$b.Write($newEncIndex.IV)
$b.Write([uint32]$newEncIndex.Cipher.Length)
$b.Write([uint32]$newPlainIndexBytes.Length)
$b.Write($newEncIndex.Cipher)

foreach ($r in $fileRecords) {
    $b.Write($r.DataBlob)
}
$b.Flush()
$b.Close()
$f.Close()

Move-Item -Path $tempPak -Destination $targetPakFullPath -Force

# 9. Clean up loose unencrypted integrity.manifest, license.lic & remove old security.pak
$looseManifest = Join-Path $targetPath "integrity.manifest"
if (Test-Path $looseManifest) {
    Remove-Item -Path $looseManifest -Force -ErrorAction SilentlyContinue
}
$oldSecurityPak = Join-Path $pakDir "security.pak"
if (Test-Path $oldSecurityPak) {
    Remove-Item -Path $oldSecurityPak -Force -ErrorAction SilentlyContinue
}
$looseLic1 = Join-Path $targetPath "..\license.lic"
if (Test-Path $looseLic1) {
    Remove-Item -Path $looseLic1 -Force -ErrorAction SilentlyContinue
}
$looseLic2 = Join-Path (Get-Location) "license.lic"
if (Test-Path $looseLic2) {
    Remove-Item -Path $looseLic2 -Force -ErrorAction SilentlyContinue
}

Write-Host "Embedded signed & encrypted DLL integrity manifest and license into: $targetPakFullPath ($($lines.Count) DLLs hashed, total files in PAK: $newFileCount)"
