# pack_all_content.ps1
# This script compresses (Deflate Optimal) and encrypts (AES-256) all shaders and content into 10 balanced .pak files.
param([string]$SourceContent = (Join-Path (Split-Path $PSScriptRoot -Parent) '../src/Content'))

$buildContents = "Contents" # Relative to build directory
$packedPaksDir = "Contents"

# 256-bit AES Master Key
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'Security/ContentCrypto.ps1')
$AesKey = Get-ContentEncryptionKey

function Compress-Data([byte[]]$data) {
    $ms = New-Object System.IO.MemoryStream
    $deflate = New-Object System.IO.Compression.DeflateStream($ms, [System.IO.Compression.CompressionLevel]::Optimal)
    $deflate.Write($data, 0, $data.Length)
    $deflate.Close()
    $res = $ms.ToArray()
    $ms.Close()
    return $res
}



# 1. Gather all files to pack
$allFiles = @()

# Shaders
if (Test-Path "$buildContents\shaders") {
    $base = (Get-Item "$buildContents\shaders").FullName
    $shaderFiles = Get-ChildItem -Path "$buildContents\shaders" -File -Recurse
    foreach ($f in $shaderFiles) {
        $rel = $f.FullName.Substring($base.Length).TrimStart("\")
        $key = "shaders/" + $rel.Replace("\", "/")
        $allFiles += [PSCustomObject]@{ Path = $f.FullName; Key = $key; Length = $f.Length }
    }
}

# General Content
if (Test-Path $sourceContent) {
    $base = (Get-Item $sourceContent).FullName
    $contentFiles = Get-ChildItem -Path $sourceContent -File -Recurse
    foreach ($f in $contentFiles) {
        $rel = $f.FullName.Substring($base.Length).TrimStart("\")
        $key = "Content/" + $rel.Replace("\", "/")
        $allFiles += [PSCustomObject]@{ Path = $f.FullName; Key = $key; Length = $f.Length }
    }
}

if ($env:IDK_RUNTIME_ROOT) {
    $base=(Get-Item -LiteralPath $env:IDK_RUNTIME_ROOT).FullName
    foreach($file in Get-ChildItem -LiteralPath $base -File -Recurse){
        if($file.Name -eq 'runtime-files.txt'){continue}
        $key='Runtime/'+$file.FullName.Substring($base.Length).TrimStart('\').Replace('\','/')
        $allFiles += [PSCustomObject]@{Path=$file.FullName;Key=$key;Length=$file.Length}
    }
    $manifest=Join-Path $base 'runtime-files.txt'
    $entries=@([IO.Path]::GetFileName($base)) + @($allFiles | Where-Object {$_.Key.StartsWith('Content/') -or $_.Key.StartsWith('Runtime/')} | ForEach-Object {$_.Key})
    [IO.File]::WriteAllLines($manifest,$entries,[Text.UTF8Encoding]::new($false))
    $allFiles += [PSCustomObject]@{Path=$manifest;Key='Runtime/files.txt';Length=(Get-Item $manifest).Length}
}
$numFiles = $allFiles.Count
Write-Host "Found $numFiles files to pack."

# 2. Distribute files into 9 buckets (content01.pak ... content09.pak)
# content10.pak is dedicated to DirectStorage streaming textures
$numBuckets = 9
$buckets = New-Object 'System.Collections.Generic.List[PSCustomObject][]' $numBuckets
for ($i = 0; $i -lt $numBuckets; $i++) { $buckets[$i] = New-Object 'System.Collections.Generic.List[PSCustomObject]' }

$bucketIndex = 0
foreach ($file in $allFiles) {
    $buckets[$bucketIndex].Add($file)
    $bucketIndex = ($bucketIndex + 1) % $numBuckets
}

# 3. Write Compressed & Encrypted PAK files (EPK3 authenticated)
$totalRawBytes = 0
$totalCompressedBytes = 0

for ($i = 0; $i -lt $numBuckets; $i++) {
    $pakId = ($i + 1).ToString("00")
    $pakName = "content$pakId.pak"
    $pakPath = Join-Path $packedPaksDir $pakName
    
    $bucketFiles = $buckets[$i]
    $fileCount = $bucketFiles.Count
    Write-Host "Compressing & encrypting $pakName with $fileCount files..."
    
    # Process all files in this bucket: Compress -> Encrypt
    $processedFiles = @()
    foreach ($file in $bucketFiles) {
        $rawBytes = [System.IO.File]::ReadAllBytes($file.Path)
        $uncompressedLen = $rawBytes.Length
        $totalRawBytes += $uncompressedLen
        
        $isCompressed = 0
        $dataToEncrypt = $rawBytes
        $compressedLen = $uncompressedLen
        
        if ($uncompressedLen -gt 64) {
            $compBytes = Compress-Data -data $rawBytes
            if ($compBytes.Length -lt $uncompressedLen) {
                $dataToEncrypt = $compBytes
                $isCompressed = 1
                $compressedLen = $compBytes.Length
            }
        }
        $totalCompressedBytes += $compressedLen
        
        $enc = Encrypt-Data -plainBytes $dataToEncrypt -key $AesKey -Context ("file/"+$file.Key.Replace("\","/"))
        
        $processedFiles += [PSCustomObject]@{
            Key = $file.Key
            UncompressedLength = $uncompressedLen
            CompressedLength = $compressedLen
            IsCompressed = $isCompressed
            IV = $enc.IV
            Cipher = $enc.Cipher
            CipherTotalLength = (16 + $enc.Cipher.Length)
        }
    }
    
    # Prepare Plain Index: 4 bytes numFiles + fileCount * 276 bytes
    # Entry: 256 (name) + 4 (offset) + 4 (cipherTotalLength) + 4 (compressedLength) + 4 (uncompressedLength) + 4 (flags) = 276 bytes
    $plainIndexStream = New-Object System.IO.MemoryStream
    $indexWriter = New-Object System.IO.BinaryWriter($plainIndexStream)
    $indexWriter.Write([uint32]$fileCount)
    
    # Two-pass calculation for exact offsets
    $dummyIndexBytes = New-Object byte[] (4 + ($fileCount * 276))
    $dummyEncIndex = Encrypt-Data -plainBytes $dummyIndexBytes -key $AesKey
    $indexCipherSize = $dummyEncIndex.Cipher.Length
    
    $headerSize = 4 + 16 + 4 + 4 + $indexCipherSize
    $currentOffset = $headerSize
    
    foreach ($pf in $processedFiles) {
        $key = $pf.Key
        if ([string]::IsNullOrEmpty($key)) { $key = "unknown_" + [guid]::NewGuid().ToString() }
        if ($key.Length -gt 127) { throw "Archive path exceeds 127 UTF-16 units: $key" }
        
        $paddedKey = $key.PadRight(128).Substring(0, 128)
        $nameBytes = [System.Text.Encoding]::Unicode.GetBytes($paddedKey)
        
        $indexWriter.Write($nameBytes)
        $indexWriter.Write([uint32]$currentOffset)
        $indexWriter.Write([uint32]$pf.CipherTotalLength)
        $indexWriter.Write([uint32]$pf.CompressedLength)
        $indexWriter.Write([uint32]$pf.UncompressedLength)
        $indexWriter.Write([uint32]$pf.IsCompressed) # flags
        
        $currentOffset += $pf.CipherTotalLength
    }
    
    $indexWriter.Flush()
    $plainIndexBytes = $plainIndexStream.ToArray()
    $indexWriter.Close()
    $plainIndexStream.Close()
    
    # Encrypt the plain index
    $encIndex = Encrypt-Data -plainBytes $plainIndexBytes -key $AesKey
    
    # Create the output PAK file
    $outFilePath = (Get-Item .).FullName + "\" + $pakPath
    $pakDir = [System.IO.Path]::GetDirectoryName($outFilePath)
    if (-not (Test-Path $pakDir)) { New-Item -ItemType Directory -Path $pakDir -Force > $null }
    
    $f = [System.IO.File]::Create($outFilePath)
    $b = New-Object System.IO.BinaryWriter($f)
    
    # 1. Magic Header: 'E' 'P' 'A' 'K' (0x334B5045)
    $b.Write([byte]0x45) # 'E'
    $b.Write([byte]0x50) # 'P'
    $b.Write([byte]0x4B) # 'A'
    $b.Write([byte]0x33) # '3'
    
    # 2. Index IV (16 bytes)
    $b.Write($encIndex.IV)
    
    # 3. Index Cipher Size (4 bytes)
    $b.Write([uint32]$encIndex.Cipher.Length)
    
    # 4. Index Plain Size (4 bytes)
    $b.Write([uint32]$plainIndexBytes.Length)
    
    # 5. Encrypted Index Data
    $b.Write($encIndex.Cipher)
    
    # 6. Payload: Each file's IV (16 bytes) followed by its encrypted data
    foreach ($pf in $processedFiles) {
        $b.Write($pf.IV)
        $b.Write($pf.Cipher)
    }
    
    $b.Close()
    $f.Close()
}

$ratio = [math]::Round(($totalCompressedBytes / [math]::Max(1, $totalRawBytes)) * 100, 1)
Write-Host "All content compressed (Deflate) & encrypted (AES-256-GCM) into 9 EPAK files (content01.pak - content09.pak) successfully!"
Write-Host "Raw data: $totalRawBytes bytes -> Compressed: $totalCompressedBytes bytes (Compression ratio: $ratio%)"
