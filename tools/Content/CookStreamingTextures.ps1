param([ValidateSet('EditorBuild','.ProjectBuild')][string]$Build='EditorBuild')
$ErrorActionPreference='Stop'
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'Security/ContentCrypto.ps1')
$AesKey=Get-ContentEncryptionKey
$root=[IO.Path]::GetFullPath((Join-Path (Split-Path $PSScriptRoot -Parent) '..'))
$directory=Join-Path $root 'Diagnostics/Tools/StreamingCooker'
New-Item -ItemType Directory -Path $directory -Force | Out-Null
$source=Join-Path (Split-Path $PSScriptRoot -Parent) 'Content/CookStreamingTexture.cpp'
$tool=Join-Path $directory 'CookStreamingTexture.exe'
if(!(Test-Path $tool) -or (Get-Item $source).LastWriteTime -gt (Get-Item $tool).LastWriteTime){
    $buildText=[IO.File]::ReadAllText((Join-Path $root 'build.bat'))
    $bootstrap=$buildText.Substring(0,$buildText.IndexOf(':: Determine Build Target'))
    $job=$bootstrap+@"
cd /D "$directory"
cl.exe /nologo /EHsc /O2 /MD /std:c++20 /DUNICODE /D_UNICODE /I"$root/src" /I"$root/ThirdParty/Libraries/EngineDependencies" /I"$root/ThirdParty/Microsoft/DirectStorage/sdk-package/native/include" "$source" /Fe"$tool" /link d3d12.lib dxgi.lib > compile.log 2>&1
exit /b %errorlevel%
"@
    $batch=Join-Path $directory 'compile.bat';[IO.File]::WriteAllText($batch,$job,[Text.Encoding]::ASCII)
    & $batch
    if($LASTEXITCODE -ne 0){Get-Content (Join-Path $directory 'compile.log') -Tail 12;throw 'Streaming cooker compilation failed'}
}

$content=if($env:IDK_PROJECT_CONTENT){$env:IDK_PROJECT_CONTENT}else{Join-Path $root 'Templates/Default/Content'}
$runtime=Join-Path $root "$Build/ThirdParty/Microsoft/DirectStorage"
$staging=[IO.Path]::GetFullPath((Join-Path $directory ("staging-"+$Build)))
if(!$staging.StartsWith([IO.Path]::GetFullPath($directory).TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Invalid staging directory'}
if(Test-Path $staging){Remove-Item -LiteralPath $staging -Recurse -Force}
New-Item -ItemType Directory -Path $staging -Force | Out-Null

$cookedList=@()
$imageFiles=Get-ChildItem -LiteralPath $content -Recurse -File | Where-Object {$_.Extension -in @('.png','.jpg','.jpeg','.tga','.bmp')}
$index=0
foreach($f in $imageFiles){
    $rel=$f.FullName.Substring($content.Length+1).Replace("\","/")
    $tempCooked=Join-Path $staging "tex_$index.gdst"
    & $tool $f.FullName $tempCooked $runtime
    if($LASTEXITCODE -ne 0){throw "Texture cooking failed: $rel ($LASTEXITCODE)"}
    
    # Read the 32-byte header
    $fs=[System.IO.File]::OpenRead($tempCooked)
    $br=New-Object System.IO.BinaryReader($fs)
    $magic=$br.ReadUInt32()
    $version=$br.ReadUInt32()
    $width=$br.ReadUInt32()
    $height=$br.ReadUInt32()
    $mips=$br.ReadUInt32()
    $format=$br.ReadUInt32()
    $rawBytes=$br.ReadUInt32()
    $compressedBytes=$br.ReadUInt32()
    $br.Close()
    $fs.Close()
    
    $cookedList += [PSCustomObject]@{
        Name = $rel
        Width = $width
        Height = $height
        Mips = $mips
        Format = $format
        RawBytes = $rawBytes
        CompressedBytes = $compressedBytes
        TempFile = $tempCooked
    }
    $index++
}

# Pack into content10.pak
$contentsDir = Join-Path $root "$Build/Contents"
if(-not (Test-Path $contentsDir)){ New-Item -ItemType Directory -Path $contentsDir -Force | Out-Null }
$pakPath = Join-Path $contentsDir "content10.pak"

function Align4K([uint64]$val) {
    $rem = $val % 4096
    if ($rem -eq 0) { return $val }
    return ($val + (4096 - $rem))
}

# TOC: Header (16 bytes) + entries ($cookedList.Count * 160 bytes)
$numEntries = [uint32]$cookedList.Count
$rawHeaderSize = [uint64](16 + 32 + ($numEntries * 160))
$alignedHeaderSize = Align4K $rawHeaderSize

# Calculate 4KB aligned offset for each entry's compressed payload
$currentOffset = $alignedHeaderSize
foreach($c in $cookedList){
    $c | Add-Member -MemberType NoteProperty -Name "DataOffset" -Value $currentOffset
    $currentOffset = Align4K ($currentOffset + $c.CompressedBytes + 32)
}

# Write content10.pak
$outStream = [System.IO.File]::Create($pakPath)
$bw = New-Object System.IO.BinaryWriter($outStream)

# 1. Header (16 bytes)
$bw.Write([uint32]0x334B5053) # 'SPAK'
$bw.Write([uint32]2)          # authenticated streaming version
$bw.Write([uint32]$numEntries)
$bw.Write([uint32]$alignedHeaderSize)

# 2. Authenticated/encrypted entries (160 bytes each)
$tableStream=[IO.MemoryStream]::new()
$tableWriter=[IO.BinaryWriter]::new($tableStream)
foreach($c in $cookedList){
    $nameBytes = [System.Text.Encoding]::UTF8.GetBytes($c.Name)
    $entryBuffer = New-Object byte[] 128
    [Array]::Copy($nameBytes, $entryBuffer, [math]::Min($nameBytes.Length, 127))
    $tableWriter.Write($entryBuffer)
    $tableWriter.Write([uint64]$c.DataOffset)
    $tableWriter.Write([uint32]$c.CompressedBytes)
    $tableWriter.Write([uint32]$c.RawBytes)
    $tableWriter.Write([uint32]$c.Width)
    $tableWriter.Write([uint32]$c.Height)
    $tableWriter.Write([uint32]$c.Mips)
    $tableWriter.Write([uint32]$c.Format)
}

$tableWriter.Flush()
$table=$tableStream.ToArray();$tableWriter.Dispose();$tableStream.Dispose()
$context="stream-index/2/$numEntries/$alignedHeaderSize"
$encryptedTable=Encrypt-Data $table $AesKey $context
$bw.Write($encryptedTable.IV);$bw.Write($encryptedTable.Cipher)

# Pad header to $alignedHeaderSize
$headerPadding = $alignedHeaderSize - $rawHeaderSize
if($headerPadding -gt 0){
    $padBytes = New-Object byte[] $headerPadding
    $bw.Write($padBytes)
}

# 3. Payloads: write GDeflate data aligned to 4KB
foreach($c in $cookedList){
    $fs = [System.IO.File]::OpenRead($c.TempFile)
    $fs.Seek(32, [System.IO.SeekOrigin]::Begin) | Out-Null
    $payloadBytes = New-Object byte[] $c.CompressedBytes
    $fs.Read($payloadBytes, 0, $c.CompressedBytes) | Out-Null
    $fs.Close()
    
    $encrypted=Encrypt-Data $payloadBytes $AesKey ("stream/"+$c.Name)
    $bw.Write($encrypted.IV);$bw.Write($encrypted.Cipher)
    
    $nextOffset = Align4K ($c.DataOffset + $c.CompressedBytes + 32)
    $padLen = $nextOffset - ($c.DataOffset + $c.CompressedBytes + 32)
    if($padLen -gt 0){
        $padBytes = New-Object byte[] $padLen
        $bw.Write($padBytes)
    }
}

$bw.Close()
$outStream.Close()

# Clean up staging and any loose Streaming folder
if(Test-Path $staging){ Remove-Item -LiteralPath $staging -Recurse -Force }
$streamingFolder = Join-Path $root "$Build/Contents/Streaming"
if(Test-Path $streamingFolder){ Remove-Item -LiteralPath $streamingFolder -Recurse -Force }

Write-Host "DirectStorage textures packed into content10.pak ($numEntries textures, AES-256-GCM, 4KB aligned)."
