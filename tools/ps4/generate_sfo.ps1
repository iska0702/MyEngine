# ==============================================================================
# PS4 param.sfo Binary Generator (Pure PowerShell)
# Produces valid Sony SFO (System File Object) format binary files
# ==============================================================================
param(
    [string]$OutputPath = "PS4Build/sce_sys/param.sfo",
    [string]$Title = "Idontknow PS4",
    [string]$TitleId = "MYENG0001",
    [string]$ContentId = "IV0000-MYENG0001_00-MYENGINE00000001",
    [string]$Version = "01.00",
    [string]$Category = "gd" # Game Digital
)

$ErrorActionPreference = "Stop"

# Define entries - MUST BE SORTED ALPHABETICALLY BY KEY!
$entries = @(
    @{ Key = "APP_TYPE";           Type = "Int";  MaxLen = 4;   Val = 1 },
    @{ Key = "APP_VER";            Type = "Utf8"; MaxLen = 8;   Val = $Version },
    @{ Key = "ATTRIBUTE";          Type = "Int";  MaxLen = 4;   Val = 0 },
    @{ Key = "CATEGORY";           Type = "Utf8"; MaxLen = 4;   Val = $Category },
    @{ Key = "CONTENT_ID";         Type = "Utf8"; MaxLen = 48;  Val = $ContentId },
    @{ Key = "DOWNLOAD_DATA_SIZE"; Type = "Int";  MaxLen = 4;   Val = 0 },
    @{ Key = "SYSTEM_VER";         Type = "Int";  MaxLen = 4;   Val = 0 },
    @{ Key = "TITLE";              Type = "Utf8"; MaxLen = 128; Val = $Title },
    @{ Key = "TITLE_ID";           Type = "Utf8"; MaxLen = 12;  Val = $TitleId },
    @{ Key = "VERSION";            Type = "Utf8"; MaxLen = 8;   Val = $Version }
)

# Sort alphabetically by key (required by PS4 OS SFO parser)
$entries = $entries | Sort-Object { $_.Key }

$entryCount = $entries.Count
$headerSize = 20
$indexTableSize = $entryCount * 16

# Build Key Table
$keyStream = New-Object System.IO.MemoryStream
$keyWriter = New-Object System.IO.BinaryWriter($keyStream)

# Build Data Table
$dataStream = New-Object System.IO.MemoryStream
$dataWriter = New-Object System.IO.BinaryWriter($dataStream)

$indexEntries = @()

foreach ($e in $entries) {
    $keyOffset = [uint16]$keyStream.Position
    $keyBytes = [System.Text.Encoding]::UTF8.GetBytes($e.Key)
    $keyWriter.Write($keyBytes)
    $keyWriter.Write([byte]0) # null terminator

    $dataOffset = [uint32]$dataStream.Position

    if ($e.Type -eq "Int") {
        $dataFmt = [uint16]0x0404 # Integer (Signed 32-bit integer)
        $actualLen = [uint32]4
        $maxLen = [uint32]4
        $dataWriter.Write([int32]$e.Val)
    } else {
        $dataFmt = [uint16]0x0204 # Text (UTF-8 format NULL terminated)
        $valBytes = [System.Text.Encoding]::UTF8.GetBytes($e.Val)
        $actualLen = [uint32]($valBytes.Length + 1)
        $maxLen = [uint32]$e.MaxLen

        $dataWriter.Write($valBytes)
        $dataWriter.Write([byte]0) # null terminator

        # Pad to maxLen
        $padding = $maxLen - $actualLen
        if ($padding -gt 0) {
            $dataWriter.Write((New-Object byte[] $padding))
        }
    }

    $indexEntries += @{
        KeyOffset = $keyOffset
        DataFmt = $dataFmt
        DataLen = $actualLen
        DataMaxLen = $maxLen
        DataOffset = $dataOffset
    }
}

# Align Key Table to 4 bytes
while (($keyStream.Position % 4) -ne 0) {
    $keyWriter.Write([byte]0)
}

$keyTableOffset = [uint32]($headerSize + $indexTableSize)
$dataTableOffset = [uint32]($keyTableOffset + $keyStream.Position)

# Build final binary SFO
$finalStream = New-Object System.IO.MemoryStream
$finalWriter = New-Object System.IO.BinaryWriter($finalStream)

# 1. Header (20 bytes)
$finalWriter.Write([byte[]]@(0x00, 0x50, 0x53, 0x46)) # '\0PSF'
$finalWriter.Write([uint32]0x00000101)                 # Version 1.1
$finalWriter.Write([uint32]$keyTableOffset)
$finalWriter.Write([uint32]$dataTableOffset)
$finalWriter.Write([uint32]$entryCount)

# 2. Index Table (16 bytes per entry)
foreach ($idx in $indexEntries) {
    $finalWriter.Write([uint16]$idx.KeyOffset)
    $finalWriter.Write([uint16]$idx.DataFmt)
    $finalWriter.Write([uint32]$idx.DataLen)
    $finalWriter.Write([uint32]$idx.DataMaxLen)
    $finalWriter.Write([uint32]$idx.DataOffset)
}

# 3. Key Table
$finalWriter.Write($keyStream.ToArray())

# 4. Data Table
$finalWriter.Write($dataStream.ToArray())

# Ensure output directory exists
$dir = [System.IO.Path]::GetDirectoryName($OutputPath)
if ($dir -and !(Test-Path $dir)) {
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
}

[System.IO.File]::WriteAllBytes($OutputPath, $finalStream.ToArray())

Write-Host "Generated PS4 param.sfo at $OutputPath (Size: $($finalStream.Length) bytes)"
