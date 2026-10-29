param([Parameter(Mandatory=$true)][string]$OutputDirectory, [switch]$DependenciesOnly, [string]$Platform = 'STANDALONE')
$ErrorActionPreference = 'Stop'
$sourceRoot = Split-Path $PSScriptRoot -Parent
$outputRoot = [System.IO.Path]::GetFullPath($OutputDirectory)
if ($outputRoot.TrimEnd('\') -eq [IO.Path]::GetFullPath($sourceRoot).TrimEnd('\')) { throw 'Cannot package into the source root' }
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
$legacyDll = Join-Path $outputRoot 'DLL'
if (Test-Path -LiteralPath $legacyDll) {
    $tpRoot = Join-Path $outputRoot 'ThirdParty'
    New-Item -ItemType Directory -Force -Path $tpRoot | Out-Null
    Copy-Item -Path (Join-Path $legacyDll '*') -Destination $tpRoot -Recurse -Force
    Remove-Item -LiteralPath $legacyDll -Recurse -Force
}
# Migrate generated runtime layouts without touching the source SDKs.
function Move-RuntimeFiles([string]$from, [string]$to) {
    $source = [IO.Path]::GetFullPath((Join-Path $outputRoot $from))
    $destination = [IO.Path]::GetFullPath((Join-Path $outputRoot $to))
    foreach ($path in @($source, $destination)) {
        if (!$path.StartsWith($outputRoot.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Runtime migration escapes output directory' }
    }
    if (!(Test-Path -LiteralPath $source)) { return }
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    foreach ($file in Get-ChildItem -LiteralPath $source -File) {
        Move-Item -LiteralPath $file.FullName -Destination (Join-Path $destination $file.Name) -Force
    }
    if (!(Get-ChildItem -LiteralPath $source)) { Remove-Item -LiteralPath $source }
}
Move-RuntimeFiles 'ThirdParty/PhysX' 'ThirdParty/NVIDIA/PhysX'
Move-RuntimeFiles 'ThirdParty/NVIDIA' 'ThirdParty/NVIDIA/Streamline'
foreach ($name in @('PhysX_64.dll', 'PhysXCommon_64.dll', 'PhysXFoundation_64.dll', 'PhysXCooking_64.dll', 'PVDRuntime_64.dll')) {
    $oldPhysX = Join-Path $outputRoot "ThirdParty/NVIDIA/PhysX/$name"
    if (Test-Path -LiteralPath $oldPhysX) { Remove-Item -LiteralPath $oldPhysX }
}
if ($Platform -ne 'STEAM') {
    foreach ($relative in @('ThirdParty/Steamworks', 'ThirdParty/Steam')) {
        $steamOutput = Join-Path $outputRoot $relative
        if (Test-Path -LiteralPath $steamOutput) {
            Get-ChildItem -LiteralPath $steamOutput -File | ForEach-Object { Remove-Item -LiteralPath $_.FullName }
            if (!(Get-ChildItem -LiteralPath $steamOutput)) { Remove-Item -LiteralPath $steamOutput }
        }
    }
}
$copies = @{
    'ThirdParty/Microsoft/MsQuic-2.6.3-OpenSSL/build/native/bin/x64/msquic.dll' = 'ThirdParty/Microsoft/MsQuic'
    'ThirdParty/Valve/steamworks_sdk_165/sdk/redistributable_bin/win64/steam_api64.dll' = 'ThirdParty/Steamworks'
    'ThirdParty/EpicGames/EOS-SDK-58105819-Release-v1.19.2.1/SDK/Bin/EOSSDK-Win64-Shipping.dll' = 'ThirdParty/EpicOnlineServices'
    'ThirdParty/Steamworks/lib/x64/*.dll' = 'ThirdParty/Steamworks'
    'ThirdParty/NVIDIA/PhysX/Lib/bin/win.x86_64.vc143.md/release/*.dll' = 'ThirdParty/NVIDIA/PhysX'
    'ThirdParty/Discord/discord_social_sdk/bin/release/*.dll' = 'ThirdParty/Discord'
    'ThirdParty/Discord/discord_social_sdk/bin/release/*.kef' = 'ThirdParty/Discord'
    'ThirdParty/NVIDIA/streamline-sdk-v2.14.1/bin/x64/*.dll' = 'ThirdParty/NVIDIA/Streamline'
    'ThirdParty/Intel/XeSS_SDK_3.0.2/bin/*.dll' = 'ThirdParty/Intel'
    'ThirdParty/Microsoft/DirectStorage/sdk-package/native/bin/x64/*.dll' = 'ThirdParty/Microsoft/DirectStorage'
    'ThirdParty/AMD/FidelityFX-SDK-v1.1.4/bin/amd_*.dll' = 'ThirdParty/AMD'
    'ThirdParty/AMD/FidelityFX-SDK-v1.1.4/bin/dx*.dll' = 'ThirdParty/Microsoft/DirectX'
    'ThirdParty/AMD/FidelityFX-SDK-v1.1.4/bin/WinPixEventRuntime.dll' = 'ThirdParty/Microsoft/DirectX'
    'ThirdParty/AMD/FidelityFX-SDK-v1.1.4/PrebuiltSignedDLL/amd_*.dll' = 'ThirdParty/AMD'
    'ThirdParty/AMD/FidelityFX-SDK-v1.1.4/PrebuiltSignedDLL/dx*.dll' = 'ThirdParty/Microsoft/DirectX'
    'ThirdParty/Microsoft/WebView2-1.0.4314.35/build/native/x64/WebView2Loader.dll' = 'ThirdParty/Microsoft/WebView2'
    'ThirdParty/AMD/FidelityFX-SDK-v1.1.4/PrebuiltSignedDLL/WinPixEventRuntime.dll' = 'ThirdParty/Microsoft/DirectX'
}
# Remove legacy Epic naming and the inactive platform's generated runtime folder.
$obsoletePlatforms = @('ThirdParty/Epic')
if ($Platform -ne 'EPIC') { $obsoletePlatforms += 'ThirdParty/EpicOnlineServices' }
foreach ($relative in $obsoletePlatforms) {
    $directory = [IO.Path]::GetFullPath((Join-Path $outputRoot $relative))
    if (!$directory.StartsWith($outputRoot.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected platform directory' }
    if (Test-Path -LiteralPath $directory) {
        Get-ChildItem -LiteralPath $directory -File | Remove-Item -Force
        if (!(Get-ChildItem -LiteralPath $directory)) { Remove-Item -LiteralPath $directory }
    }
}
$requiredRuntime = if ($Platform -eq 'EPIC') {
    'ThirdParty/EpicGames/EOS-SDK-58105819-Release-v1.19.2.1/SDK/Bin/EOSSDK-Win64-Shipping.dll'
} elseif ($Platform -eq 'STEAM') {
    'ThirdParty/Valve/steamworks_sdk_165/sdk/redistributable_bin/win64/steam_api64.dll'
} else { $null }
if ($requiredRuntime -and !(Test-Path -LiteralPath (Join-Path $sourceRoot $requiredRuntime))) { throw "Missing platform runtime: $requiredRuntime" }
foreach ($entry in $copies.GetEnumerator()) {
    if ($Platform -ne 'STEAM' -and $entry.Value -eq 'ThirdParty/Steamworks') { continue }
    if ($Platform -ne 'EPIC' -and $entry.Value -eq 'ThirdParty/EpicOnlineServices') { continue }
    $files = @(Get-ChildItem -Path (Join-Path $sourceRoot $entry.Key) -File -ErrorAction SilentlyContinue | Where-Object { $_.Name -ne 'amd_fidelityfx_vk.dll' })
    if ($files.Count -eq 0) { continue }
    $destination = Join-Path $outputRoot $entry.Value
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    $files | Copy-Item -Destination $destination -Force
}
$streamlinePix = Join-Path $outputRoot 'ThirdParty/NVIDIA/Streamline/WinPixEventRuntime.dll'
if (Test-Path -LiteralPath $streamlinePix) {
    $directX = Join-Path $outputRoot 'ThirdParty/Microsoft/DirectX'
    New-Item -ItemType Directory -Force -Path $directX | Out-Null
    Move-Item -LiteralPath $streamlinePix -Destination (Join-Path $directX 'WinPixEventRuntime.dll') -Force
}
$vkDll = Join-Path $outputRoot 'ThirdParty/AMD/amd_fidelityfx_vk.dll'
# The application explicitly loads the pinned Agility runtime.
$agility=Join-Path $sourceRoot 'ThirdParty/Microsoft/AgilitySDK-1.620.1/build/native/bin/x64'
$agilityOutput=Join-Path $outputRoot 'ThirdParty/Microsoft/Agility'
New-Item -ItemType Directory -Path $agilityOutput -Force | Out-Null
foreach($name in @('D3D12Core.dll','d3d12SDKLayers.dll')) {
    $source=Join-Path $agility $name
    if(!(Test-Path -LiteralPath $source)){throw "Missing Agility SDK runtime: $source"}
    Copy-Item -LiteralPath $source -Destination $agilityOutput -Force
}
if (Test-Path -LiteralPath $vkDll) { Remove-Item -LiteralPath $vkDll -Force }
if (!$DependenciesOnly) {
    $shaders = Join-Path $outputRoot 'Contents/shaders'
    New-Item -ItemType Directory -Force -Path $shaders | Out-Null
    Copy-Item -Path (Join-Path $sourceRoot 'shaders/*') -Destination $shaders -Recurse -Force
    Push-Location $outputRoot
    try {
        & (Join-Path $sourceRoot 'tools/Content/pack_all_content.ps1')
        & (Join-Path $sourceRoot 'tools/Security/generate_dll_manifest.ps1') -TargetDir ThirdParty -TargetPak Contents/content01.pak
    } finally { Pop-Location }
}
