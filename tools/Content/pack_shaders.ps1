$shadersPath = "shaders"
$pakPath = "Contents\content01.pak"

if (-not (Test-Path $shadersPath)) {
    Write-Host "ERROR: Shaders path not found: $shadersPath"
    exit 1
}

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



$files = Get-ChildItem -Path $shadersPath -File
$numFiles = $files.Count
Write-Host "Packing, compressing and encrypting $numFiles files into $pakPath"

$processedFiles = @()
foreach ($file in $files) {
    $rawBytes = [System.IO.File]::ReadAllBytes($file.FullName)
    $uncompressedLen = $rawBytes.Length
    
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
    
    $enc = Encrypt-Data -plainBytes $dataToEncrypt -key $AesKey -Context ("file/shaders/"+$file.Name.Replace("\","/"))
    
    $processedFiles += [PSCustomObject]@{
        Name = "shaders/"+$file.Name
        UncompressedLength = $uncompressedLen
        CompressedLength = $compressedLen
        IsCompressed = $isCompressed
        IV = $enc.IV
        Cipher = $enc.Cipher
        CipherTotalLength = (16 + $enc.Cipher.Length)
    }
}

$plainIndexStream = New-Object System.IO.MemoryStream
$indexWriter = New-Object System.IO.BinaryWriter($plainIndexStream)
$indexWriter.Write([uint32]$numFiles)

$dummyIndexBytes = New-Object byte[] (4 + ($numFiles * 276))
$dummyEncIndex = Encrypt-Data -plainBytes $dummyIndexBytes -key $AesKey
$indexCipherSize = $dummyEncIndex.Cipher.Length

$headerSize = 4 + 16 + 4 + 4 + $indexCipherSize
$currentOffset = $headerSize

foreach ($pf in $processedFiles) {
    $key = $pf.Name
    if ($key.Length -gt 128) { $key = $key.Substring(0, 128) }
    $paddedKey = $key.PadRight(128).Substring(0, 128)
    $nameBytes = [System.Text.Encoding]::Unicode.GetBytes($paddedKey)
    
    $indexWriter.Write($nameBytes)
    $indexWriter.Write([uint32]$currentOffset)
    $indexWriter.Write([uint32]$pf.CipherTotalLength)
    $indexWriter.Write([uint32]$pf.CompressedLength)
    $indexWriter.Write([uint32]$pf.UncompressedLength)
    $indexWriter.Write([uint32]$pf.IsCompressed)
    
    $currentOffset += $pf.CipherTotalLength
}

$indexWriter.Flush()
$plainIndexBytes = $plainIndexStream.ToArray()
$indexWriter.Close()
$plainIndexStream.Close()

$encIndex = Encrypt-Data -plainBytes $plainIndexBytes -key $AesKey

$outFilePath = (Get-Item .).FullName + "\" + $pakPath
$pakDir = [System.IO.Path]::GetDirectoryName($outFilePath)
if (-not (Test-Path $pakDir)) { New-Item -ItemType Directory -Path $pakDir -Force > $null }

$f = [System.IO.File]::Create($outFilePath)
$b = New-Object System.IO.BinaryWriter($f)

$b.Write([byte]0x45) # 'E'
$b.Write([byte]0x50) # 'P'
$b.Write([byte]0x4B) # 'A'
$b.Write([byte]0x33) # '3'
$b.Write($encIndex.IV)
$b.Write([uint32]$encIndex.Cipher.Length)
$b.Write([uint32]$plainIndexBytes.Length)
$b.Write($encIndex.Cipher)

foreach ($pf in $processedFiles) {
    $b.Write($pf.IV)
    $b.Write($pf.Cipher)
}

$b.Close()
$f.Close()
Write-Host "Shader packing, compression & encryption complete!"
