param([string]$BuildDirectory=(Join-Path (Split-Path $PSScriptRoot -Parent) '../Projects/Test/Build'))
$ErrorActionPreference='Stop'
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'Security/ContentCrypto.ps1')
$key=Get-ContentEncryptionKey
$build=(Resolve-Path -LiteralPath $BuildDirectory).Path
$entries=@{};$total=0
for($volume=1;$volume-le 9;$volume++){
    $path=Join-Path $build ('Contents/content{0:00}.pak'-f $volume)
    $bytes=[IO.File]::ReadAllBytes($path);$reader=[IO.BinaryReader]::new([IO.MemoryStream]::new($bytes,$false))
    try{
        if($reader.ReadUInt32()-ne 0x334B5045){throw 'Expected authenticated EPK3 archive'}
        $iv=$reader.ReadBytes(16);$cipherSize=$reader.ReadUInt32();$plainSize=$reader.ReadUInt32()
        if($cipherSize-lt 20-or $cipherSize-gt 20MB-or $cipherSize-ne $plainSize+16-or 28L+$cipherSize-gt $bytes.Length){throw 'Invalid index bounds'}
        $index=Decrypt-Data ($reader.ReadBytes($cipherSize)) $iv $key 'index'
        if($index.Length-ne $plainSize){throw 'Index size mismatch'}
        $table=[IO.BinaryReader]::new([IO.MemoryStream]::new($index,$false))
        try{
            $count=$table.ReadUInt32();if($count-gt 65536-or $index.Length-ne 4L+276L*$count){throw 'Invalid index entries'}
            for($i=0;$i-lt $count;$i++){
                $name=[Text.Encoding]::Unicode.GetString($table.ReadBytes(256)).TrimEnd([char]0,[char]32).Replace('\','/')
                $offset=$table.ReadUInt32();$size=$table.ReadUInt32();$compressedSize=$table.ReadUInt32();$rawSize=$table.ReadUInt32();$flags=$table.ReadUInt32()
                if($offset-lt 28L+$cipherSize-or $size-lt 32-or $size-32-ne $compressedSize-or [uint64]$offset+$size-gt $bytes.Length-or $rawSize-gt 512MB-or $flags-gt 1){throw "Invalid payload bounds: $name"}
                $reader.BaseStream.Position=$offset;$iv=$reader.ReadBytes(16)
                $decoded=Decrypt-Data ($reader.ReadBytes($size-16)) $iv $key ("file/"+$name)
                if($decoded.Length-ne $compressedSize){throw 'Compressed payload size mismatch'}
                if($flags-eq 1){
                    $stream=[IO.MemoryStream]::new($decoded,$false);$deflate=[IO.Compression.DeflateStream]::new($stream,[IO.Compression.CompressionMode]::Decompress)
                    $output=[IO.MemoryStream]::new();$buffer=[byte[]]::new(65536)
                    try{while(($read=$deflate.Read($buffer,0,$buffer.Length))-gt 0){if($output.Length+$read-gt $rawSize){throw 'Decompression exceeded authenticated length'};$output.Write($buffer,0,$read)};$raw=$output.ToArray()}finally{$deflate.Dispose();$stream.Dispose();$output.Dispose()}
                }else{$raw=$decoded}
                if($raw.Length-ne $rawSize-or $entries.ContainsKey($name)){throw "Invalid or duplicate asset: $name"}
                $entries[$name]=$raw;$total++
            }
        }finally{$table.Dispose()}
    }finally{$reader.Dispose()}
}
$path=Join-Path $build 'Contents/content10.pak';$bytes=[IO.File]::ReadAllBytes($path);$reader=[IO.BinaryReader]::new([IO.MemoryStream]::new($bytes,$false))
try{
    if($reader.ReadUInt32()-ne 0x334B5053-or $reader.ReadUInt32()-ne 2){throw 'Expected authenticated SPK3 v2 archive'}
    $count=$reader.ReadUInt32();$header=$reader.ReadUInt32()
    if($count-gt 65536-or 48L+160L*$count-gt $header-or $header-gt $bytes.Length){throw 'Invalid streaming index bounds'}
    $iv=$reader.ReadBytes(16);$index=Decrypt-Data ($reader.ReadBytes(16+160*$count)) $iv $key "stream-index/2/$count/$header"
    $table=[IO.BinaryReader]::new([IO.MemoryStream]::new($index,$false))
    try{for($i=0;$i-lt $count;$i++){
        $name=[Text.Encoding]::UTF8.GetString($table.ReadBytes(128)).Split([char]0)[0]
        $offset=$table.ReadUInt64();$compressedSize=$table.ReadUInt32();$metadata=$table.ReadBytes(20)
        if($offset-lt $header-or $compressedSize-gt 256MB-or $offset+32+$compressedSize-gt $bytes.Length){throw 'Invalid streaming payload bounds'}
        $reader.BaseStream.Position=$offset;$iv=$reader.ReadBytes(16)
        $decoded=Decrypt-Data ($reader.ReadBytes(16+$compressedSize)) $iv $key ("stream/"+$name)
        if($decoded.Length-ne $compressedSize){throw 'Streaming payload size mismatch'}
    }}finally{$table.Dispose()}
}finally{$reader.Dispose()}
Write-Output "PASS: all 10 archives authenticated; $total assets decoded and $count encrypted GDeflate payloads verified"
if($entries.ContainsKey('Runtime/files.txt')){
    $id=([Text.Encoding]::UTF8.GetString($entries['Runtime/files.txt'])-split "`n")[0].Trim()
    Write-Output "Runtime build ID: $id"
}
if(Get-ChildItem -LiteralPath $build -Recurse -File | Where-Object {$_.Name-eq 'GeneratedContentKey.h'-or $_.Name-eq 'content-key.dpapi'}){throw 'Build contains local key material'}
