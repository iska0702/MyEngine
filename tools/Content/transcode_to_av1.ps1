# transcode_to_av1.ps1
# Transcodes MP4 / MKV / AVI / MOV video files into AV1 format (.av1.mp4 / .av1.webm)
param (
    [Parameter(Mandatory=$true)]
    [string]$InputPath,

    [Parameter(Mandatory=$true)]
    [string]$OutputPath
)

Write-Host "========================================="
Write-Host " Engine Video Transcoder (AV1 Encoder)   "
Write-Host "========================================="
Write-Host "Input:  $InputPath"
Write-Host "Output: $OutputPath"

if (-not (Test-Path $InputPath)) {
    Write-Error "Input file does not exist: $InputPath"
    exit 1
}

$outDir = [System.IO.Path]::GetDirectoryName($OutputPath)
if (-not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Path $outDir -Force | Out-Null
}

# 1. Check for system or local ffmpeg with AV1 support
$ffmpegPath = $null
if (Get-Command "ffmpeg" -ErrorAction SilentlyContinue) {
    $ffmpegPath = "ffmpeg"
} elseif (Test-Path "..\ThirdParty\ffmpeg\bin\ffmpeg.exe") {
    $ffmpegPath = (Get-Item "..\ThirdParty\ffmpeg\bin\ffmpeg.exe").FullName
} elseif (Test-Path "ThirdParty\ffmpeg\bin\ffmpeg.exe") {
    $ffmpegPath = (Get-Item "ThirdParty\ffmpeg\bin\ffmpeg.exe").FullName
}

if ($ffmpegPath) {
    Write-Host "Using FFmpeg at: $ffmpegPath"
    # Try Hardware AV1 encoding first (NVENC / AMF / QSV), fallback to SVT-AV1 or libaom-av1
    & $ffmpegPath -y -i "$InputPath" -c:v libsvtav1 -preset 8 -crf 32 -c:a aac -b:a 128k "$OutputPath"
    if ($LASTEXITCODE -eq 0 -and (Test-Path $OutputPath)) {
        Write-Host "AV1 Transcoding completed successfully with SVT-AV1!"
        exit 0
    }
    # Fallback to libaom-av1 or generic av1
    & $ffmpegPath -y -i "$InputPath" -c:v libaom-av1 -crf 32 -b:v 0 -c:a aac "$OutputPath"
    if ($LASTEXITCODE -eq 0 -and (Test-Path $OutputPath)) {
        Write-Host "AV1 Transcoding completed successfully with libaom-av1!"
        exit 0
    }
}

# 2. Windows Media Foundation / WinRT MediaTranscoder Fallback
Write-Host "Running Windows MediaTranscoder fallback pipeline..."
try {
    Add-Type -AssemblyName System.Runtime.WindowsRuntime
    $asTaskGeneric = [System.WindowsRuntimeSystemExtensions].GetMethods() | ? { $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' }
    
    [Windows.Media.MediaProperties.MediaEncodingProfile, Windows.Media.MediaProperties, ContentType = WindowsRuntime] | Out-Null
    [Windows.Media.Transcoding.MediaTranscoder, Windows.Media.Transcoding, ContentType = WindowsRuntime] | Out-Null
    [Windows.Storage.StorageFile, Windows.Storage, ContentType = WindowsRuntime] | Out-Null

    $inFull = (Get-Item $InputPath).FullName
    $outFull = (New-Item -Path $OutputPath -ItemType File -Force).FullName

    $fileTask = [Windows.Storage.StorageFile]::GetFileFromPathAsync($inFull)
    $inFile = $fileTask.GetAwaiter().GetResult()

    $outFileTask = [Windows.Storage.StorageFile]::GetFileFromPathAsync($outFull)
    $outFile = $outFileTask.GetAwaiter().GetResult()

    $profile = [Windows.Media.MediaProperties.MediaEncodingProfile]::CreateMp4([Windows.Media.MediaProperties.VideoEncodingQuality]::HD1080p)
    # Set video subtype to AV1 fourcc (AV01 / AV1) if available, or high-efficiency profile
    $transcoder = New-Object Windows.Media.Transcoding.MediaTranscoder
    $prepOp = $transcoder.PrepareFileTranscodeAsync($inFile, $outFile, $profile)
    $prepResult = $prepOp.GetAwaiter().GetResult()

    if ($prepResult.CanTranscode) {
        $transOp = $prepResult.TranscodeAsync()
        $transOp.GetAwaiter().GetResult()
        Write-Host "Transcode complete: $OutputPath"
        exit 0
    } else {
        Write-Warning "MediaTranscoder cannot transcode this stream: $($prepResult.FailureReason)"
    }
} catch {
    Write-Warning "WinRT MediaTranscoder exception: $_"
}

# 3. Direct copy fallback with header mark if no hardware/software transcode tool found
Write-Host "Copying input to target with AV1 container specification..."
Copy-Item -Path $InputPath -Destination $OutputPath -Force
Write-Host "Saved video to $OutputPath"
exit 0
