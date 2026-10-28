# ==============================================================================
# Idontknow - PlayStation 4 (OpenOrbis Toolchain) Build System
# Compiles native PS4 ELF binary and signs it to eboot.bin (FSELF)
# ==============================================================================
param(
    [string]$ToolchainPath = $env:OO_PS4_TOOLCHAIN,
    [string]$Title = "Idontknow PS4",
    [string]$TitleId = "MYENG0001",
    [string]$ContentId = "IV0000-MYENG0001_00-MYENGINE00000001",
    [string]$Version = "01.00",
    [switch]$Rebuild,
    [switch]$SkipContentPack
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent (Split-Path $PSScriptRoot -Parent)
Set-Location $ScriptDir

Write-Host "========================================================================" -ForegroundColor Cyan
Write-Host "  Idontknow PS4 Build System (OpenOrbis Toolchain + Zig C++20)" -ForegroundColor Cyan
Write-Host "========================================================================" -ForegroundColor Cyan

# ------------------------------------------------------------------------------
# 1. Locate OpenOrbis PS4 Toolchain
# ------------------------------------------------------------------------------
if (-not $ToolchainPath -or -not (Test-Path $ToolchainPath)) {
    $defaultPath = Join-Path $ScriptDir "ThirdParty/OpenOrbis-PS4-Toolchain"
    if (Test-Path $defaultPath) {
        $ToolchainPath = $defaultPath
    } else {
        Write-Error "OpenOrbis PS4 Toolchain not found! Please set `$env:OO_PS4_TOOLCHAIN or pass -ToolchainPath."
        exit 1
    }
}
$ToolchainPath = [System.IO.Path]::GetFullPath($ToolchainPath)
$env:OO_PS4_TOOLCHAIN = $ToolchainPath
Write-Host "[Toolchain] OpenOrbis Root: $ToolchainPath" -ForegroundColor Green

$LibDir = Join-Path $ToolchainPath "lib"
$LinkScript = Join-Path $ToolchainPath "link.x"
$IncludeDir = Join-Path $ToolchainPath "include"
$MakeFselfScript = Join-Path $ToolchainPath "scripts\make_fself.py"

if (-not (Test-Path $LinkScript)) {
    Write-Error "Missing OpenOrbis linker script at $LinkScript"
    exit 1
}
if (-not (Test-Path $MakeFselfScript)) {
    Write-Error "Missing make_fself.py at $MakeFselfScript"
    exit 1
}

# ------------------------------------------------------------------------------
# 2. Locate Zig Cross-Compiler
# ------------------------------------------------------------------------------
$ZigExe = $null
$zigCandidates = @(
    (Join-Path $ScriptDir "ThirdParty\zig\zig-windows-x86_64-0.13.0\zig.exe"),
    (Join-Path $ScriptDir "ThirdParty\zig\zig.exe")
)
foreach ($c in $zigCandidates) {
    if (Test-Path $c) {
        $ZigExe = $c
        break
    }
}
if (-not $ZigExe) {
    $cmd = Get-Command zig -ErrorAction SilentlyContinue
    if ($cmd) { $ZigExe = $cmd.Source }
}
if (-not $ZigExe) {
    Write-Error "Zig compiler not found! Please ensure Zig is placed in ThirdParty\zig\"
    exit 1
}
Write-Host "[Compiler] Zig Executable:  $ZigExe" -ForegroundColor Green

# ------------------------------------------------------------------------------
# 3. Locate Python Interpreter (for make_fself.py)
# ------------------------------------------------------------------------------
$PythonExe = $null
$pythonCandidates = @(
    "C:\Users\iskan\AppData\Local\Python\bin\python.exe",
    (Join-Path $env:LOCALAPPDATA "Python\bin\python.exe"),
    "D:\Programs\Adobe Substance 3D Painter\resources\pythonsdk\python.exe"
)
foreach ($pc in $pythonCandidates) {
    if (Test-Path $pc) {
        $PythonExe = $pc
        break
    }
}
if (-not $PythonExe) {
    $cmd = Get-Command python -ErrorAction SilentlyContinue
    if ($cmd) { $PythonExe = $cmd.Source }
}
if (-not $PythonExe) {
    Write-Error "Python not found! Python 3 is required to sign PS4 ELF binaries (make_fself.py)."
    exit 1
}
Write-Host "[Python]   Python Executable: $PythonExe" -ForegroundColor Green

# ------------------------------------------------------------------------------
# 4. Prepare PS4Build Directory Structure
# ------------------------------------------------------------------------------
$OutDir = Join-Path $ScriptDir "PS4Build"
$ObjDir = Join-Path $OutDir "obj"
$SceModuleDir = Join-Path $OutDir "sce_module"
$SceSysDir = Join-Path $OutDir "sce_sys"
$SceAboutDir = Join-Path $SceSysDir "about"
$ContentsDir = Join-Path $OutDir "Contents"

$directories = @($OutDir, $ObjDir, $SceModuleDir, $SceSysDir, $SceAboutDir, $ContentsDir)
foreach ($d in $directories) {
    if (-not (Test-Path $d)) {
        New-Item -ItemType Directory -Path $d -Force | Out-Null
    }
}

# ------------------------------------------------------------------------------
# 5. Extract PRX Modules & Boilerplate Assets
# ------------------------------------------------------------------------------
$SelfZip = Join-Path $ToolchainPath "extra\OpenOrbis PS4 SELF Project.zip"
if (Test-Path $SelfZip) {
    # Extract libc.prx, libSceFios2.prx, right.sprx, icon0.png
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zipArchive = [System.IO.Compression.ZipFile]::OpenRead($SelfZip)
    try {
        foreach ($entry in $zipArchive.Entries) {
            $destPath = $null
            if ($entry.FullName -like "*libc.prx*") {
                $destPath = Join-Path $SceModuleDir "libc.prx"
            } elseif ($entry.FullName -like "*libSceFios2.prx*") {
                $destPath = Join-Path $SceModuleDir "libSceFios2.prx"
            } elseif ($entry.FullName -like "*right.sprx*") {
                $destPath = Join-Path $SceAboutDir "right.sprx"
            } elseif ($entry.FullName -like "*icon0.png*" -and -not (Test-Path (Join-Path $SceSysDir "icon0.png"))) {
                $destPath = Join-Path $SceSysDir "icon0.png"
            }
            if ($destPath -and (-not (Test-Path $destPath) -or $Rebuild)) {
                [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $destPath, $true)
                Write-Host "[Extracted] $($entry.FullName) -> $destPath" -ForegroundColor DarkGray
            }
        }
    } finally {
        $zipArchive.Dispose()
    }
}

# ------------------------------------------------------------------------------
# 6. Generate param.sfo
# ------------------------------------------------------------------------------
$SfoPath = Join-Path $SceSysDir "param.sfo"
$SfoGenScript = Join-Path $ScriptDir "tools\ps4\generate_sfo.ps1"
if (Test-Path $SfoGenScript) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File $SfoGenScript `
        -OutputPath $SfoPath `
        -Title $Title `
        -TitleId $TitleId `
        -ContentId $ContentId `
        -Version $Version `
        -Category "gd"
} else {
    Write-Warning "generate_sfo.ps1 not found at $SfoGenScript!"
}

# ------------------------------------------------------------------------------
# 7. Pack or Copy Game Content PAKs
# ------------------------------------------------------------------------------
if (-not $SkipContentPack) {
    if (Test-Path (Join-Path $ScriptDir "Contents\world.pak")) {
        Write-Host "Copying PAK archives from Contents\ to PS4Build\Contents\..." -ForegroundColor Gray
        Copy-Item -Path (Join-Path $ScriptDir "Contents\*.pak") -Destination $ContentsDir -Force
    } elseif (Test-Path (Join-Path $ScriptDir "ReleaseBuild\Contents\world.pak")) {
        Write-Host "Copying PAK archives from ReleaseBuild\Contents\ to PS4Build\Contents\..." -ForegroundColor Gray
        Copy-Item -Path (Join-Path $ScriptDir "ReleaseBuild\Contents\*.pak") -Destination $ContentsDir -Force
    }

    # Copy scene_save.txt directly to PS4Build root and Contents\
    if (Test-Path (Join-Path $ScriptDir "scene_save.txt")) {
        Write-Host "Copying scene_save.txt to PS4Build..." -ForegroundColor Gray
        Copy-Item -Path (Join-Path $ScriptDir "scene_save.txt") -Destination $OutDir -Force
        Copy-Item -Path (Join-Path $ScriptDir "scene_save.txt") -Destination $ContentsDir -Force
    }

    # Copy level files to Contents\Levels\
    if (Test-Path (Join-Path $ScriptDir "src\Content\Levels")) {
        $destLevels = Join-Path $ContentsDir "Levels"
        if (-not (Test-Path $destLevels)) { New-Item -ItemType Directory -Path $destLevels -Force | Out-Null }
        Copy-Item -Path (Join-Path $ScriptDir "src\Content\Levels\*") -Destination $destLevels -Force
    }
}

# ------------------------------------------------------------------------------
# 8. Define PS4 Client Sources
# ------------------------------------------------------------------------------
$c_sources = @(
    "ThirdParty/EngineDependencies/lua54/lua-5.4.7/src/onelua.c",
    "ThirdParty/EngineDependencies/ufbx.c"
)

$cpp_sources = @(
    "src/Core/main.cpp",
    "src/Core/Application.cpp",
    "src/Core/Window.cpp",
    "src/Core/Timer.cpp",
    "src/Platform/PS4/PS4Window.cpp",
    "src/Platform/PS4/PS4PosixStubs.cpp",
    "src/Platform/PlatformUtils.cpp",
    "src/Renderer/PS4/PS4Device.cpp",
    "src/Renderer/PS4/PS4RendererBackend.cpp",
    "src/Renderer/PS4/Renderer_PS4.cpp",
    "src/Renderer/Camera.cpp",
    "src/Renderer/CameraSystem.cpp",
    "src/Renderer/Texture.cpp",
    "src/Renderer/TextureStreamer.cpp",
    "src/Renderer/ModelLoader.cpp",
    "src/Audio/PS4Audio.cpp",
    "src/Audio/DualSenseAudio.cpp",
    "src/Audio/AudioSystem.cpp",
    "src/Input/DualSense.cpp",
    "src/Input/DualShock.cpp",
    "src/Input/InputMapping.cpp",
    "src/Debug/Profiler.cpp",
    "src/Debug/DebugConsole.cpp",
    "src/Storage/SaveSystem.cpp",
    "src/Storage/PakCompression.cpp",
    "src/Physics/PhysicsSystem.cpp",
    "src/Physics/CharacterController.cpp",
    "src/Physics/ClothSystem.cpp",
    "src/Physics/HairSystem.cpp",
    "src/UI/WebMenuSystem.cpp",
    "src/Steam/SteamSystem.cpp",
    "src/Epic/EpicSystem.cpp",
    "src/Discord/DiscordSystem.cpp",
    "src/Security/SecuritySystem.cpp",
    "src/Security/MBAObfuscator.cpp",
    "src/Security/VirtualMachine.cpp",
    "src/Security/HardwareID.cpp",
    "src/Security/LicenseSystem.cpp",
    "src/Level/LevelSystem.cpp",
    "src/Prefab/Prefab.cpp",
    "src/Prefab/PrefabSystem.cpp",
    "src/Asset/AssetRegistry.cpp",
    "src/AI/NavigationSystem.cpp",
    "src/AI/NavMesh.cpp",
    "src/AI/NavAgent.cpp",
    "src/Sequencer/Sequencer.cpp",
    "src/Rain/RainSystem.cpp",
    "src/Rain/WeatherSystem.cpp",
    "src/PostProcess/PostProcessSystem.cpp",
    "src/Scene/SceneNode.cpp",
    "src/Scene/SceneGraph.cpp",
    "src/Script/ScriptSystem.cpp",
    "src/Networking/BitReader.cpp",
    "src/Networking/BitWriter.cpp",
    "src/Networking/Packet.cpp",
    "src/Networking/NetworkSocket.cpp",
    "src/Networking/NetworkConnection.cpp",
    "src/Networking/NetworkObject.cpp",
    "src/Networking/NetworkReplication.cpp",
    "src/Networking/NetworkServer.cpp",
    "src/Networking/NetworkClient.cpp",
    "src/Networking/MultiplayerManager.cpp",
    "src/Networking/ClientAntiCheat.cpp",
    "src/Networking/ServerAntiCheat.cpp",
    "src/imgui/imgui.cpp",
    "src/imgui/imgui_draw.cpp",
    "src/imgui/imgui_tables.cpp",
    "src/imgui/imgui_widgets.cpp",
    "src/imgui/ImGuizmo.cpp",
    "src/UI/ContentBrowser.cpp"
)

$includes = @(
    "-I", "src",
    "-I", "src/Core",
    "-I", "src/Renderer",
    "-I", "src/Input",
    "-I", "src/Audio",
    "-I", "src/Storage",
    "-I", "src/Sequencer",
    "-I", "ThirdParty/EngineDependencies",
    "-I", "src/Platform",
    "-I", "src/Platform/DirectXMath",
    "-I", "src/Platform/WindowsCompat",
    "-I", "src/imgui",
    "-I", $IncludeDir,
    "-I", "ThirdParty/PhysX/physx/include",
    "-I", "ThirdParty/Steamworks/include",
    "-I", "ThirdParty/discord_social_sdk/include"
)

$defines = @(
    "-DENGINE_PLATFORM_PS4=1",
    "-D__PS4__=1",
    "-D__ORBIS__=1"
)

# ------------------------------------------------------------------------------
# 9. Compile Source Files
# ------------------------------------------------------------------------------
Write-Host "Compiling $($c_sources.Count + $cpp_sources.Count) translation units..." -ForegroundColor Yellow

$objFiles = @()
$total = $c_sources.Count + $cpp_sources.Count
$current = 0

foreach ($src in $c_sources) {
    $current++
    $objName = ($src -replace '[\\/:]', '_') + ".o"
    $objPath = Join-Path $ObjDir $objName
    $objFiles += $objPath

    $needCompile = $Rebuild -or (-not (Test-Path $objPath)) -or ((Get-Item $src).LastWriteTime -gt (Get-Item $objPath).LastWriteTime)
    if ($needCompile) {
        Write-Host "[$current/$total] Compiling (C) $src" -ForegroundColor DarkCyan
        & $ZigExe cc -target x86_64-linux-musl -fPIC -O2 @defines @includes -c $src -o $objPath
        if ($LASTEXITCODE -ne 0) {
            Write-Error "Failed to compile $src"
            exit $LASTEXITCODE
        }
    } else {
        Write-Host "[$current/$total] Up to date: $src" -ForegroundColor DarkGray
    }
}

foreach ($src in $cpp_sources) {
    $current++
    $objName = ($src -replace '[\\/:]', '_') + ".o"
    $objPath = Join-Path $ObjDir $objName
    $objFiles += $objPath

    $needCompile = $Rebuild -or (-not (Test-Path $objPath)) -or ((Get-Item $src).LastWriteTime -gt (Get-Item $objPath).LastWriteTime)
    if ($needCompile) {
        Write-Host "[$current/$total] Compiling (C++) $src" -ForegroundColor DarkCyan
        & $ZigExe c++ -target x86_64-linux-musl -std=c++20 -fPIC -O2 -Wno-narrowing -Wno-invalid-offsetof @defines @includes -c $src -o $objPath
        if ($LASTEXITCODE -ne 0) {
            Write-Error "Failed to compile $src"
            exit $LASTEXITCODE
        }
    } else {
        Write-Host "[$current/$total] Up to date: $src" -ForegroundColor DarkGray
    }
}

Write-Host "All translation units successfully compiled!" -ForegroundColor Green

# ------------------------------------------------------------------------------
# 10. Link PS4 ELF Executable
# ------------------------------------------------------------------------------
$ElfOut = Join-Path $OutDir "Idontknow.elf"
Write-Host "Linking PS4 ELF executable: $ElfOut..." -ForegroundColor Yellow

$linkFlags = @(
    "-target", "x86_64-linux-musl",
    "-pie",
    "-Wl,--script=$LinkScript",
    "-L", $LibDir
)




$linkLibs = @(
    "-lkernel",
    "-lc",
    "-lSceVideoOut",
    "-lScePad",
    "-lSceAudioOut",
    "-lSceUserService",
    "-lSceSysmodule"
)

& $ZigExe c++ @linkFlags @objFiles @linkLibs -o $ElfOut
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $ElfOut)) {
    Write-Error "Linking failed for $ElfOut"
    exit 1
}

$elfSize = (Get-Item $ElfOut).Length
Write-Host "PS4 ELF created successfully: $ElfOut ($([math]::Round($elfSize / 1MB, 2)) MB)" -ForegroundColor Green

# Patch ELF header: Sony PS4 Orbis OS expectations (EI_OSABI=0x09 FreeBSD, e_type=0xFE10 ET_SCE_DYNEXEC)
$fs = [System.IO.File]::Open($ElfOut, [System.IO.FileMode]::Open, [System.IO.FileAccess]::ReadWrite)
try {
    $fs.Seek(7, [System.IO.SeekOrigin]::Begin) | Out-Null
    $fs.WriteByte(0x09) # EI_OSABI = ELFOSABI_FREEBSD
    $fs.Seek(16, [System.IO.SeekOrigin]::Begin) | Out-Null
    $fs.WriteByte(0x10) # e_type = 0xFE10 (ET_SCE_DYNEXEC / ET_SCE_EXEC_ASLR)
    $fs.WriteByte(0xFE)
} finally {
    $fs.Close()
}
Write-Host "Patched PS4 ELF header (EI_OSABI=0x09 FreeBSD, e_type=0xFE10 ET_SCE_DYNEXEC)" -ForegroundColor Green

# ------------------------------------------------------------------------------
# 11. Generate Signed eboot.bin (FSELF)
# ------------------------------------------------------------------------------
# 11. Generate Orbis OELF and Signed eboot.bin (FSELF)
# ------------------------------------------------------------------------------
$EbootOut = Join-Path $OutDir "eboot.bin"
$OelfOut = Join-Path $OutDir "Idontknow.oelf"
$CreateFselfExe = Join-Path $ToolchainPath "bin\windows\create-fself.exe"

if (Test-Path $CreateFselfExe) {
    Write-Host "Converting ELF to Orbis OELF via create-fself.exe..." -ForegroundColor Yellow
    $env:OO_PS4_TOOLCHAIN = $ToolchainPath
    & $CreateFselfExe -in $ElfOut -out $OelfOut -eboot $EbootOut
    if ($LASTEXITCODE -eq 0 -and (Test-Path $OelfOut)) {
        $oelfSize = (Get-Item $OelfOut).Length
        Write-Host "OELF generated successfully: $OelfOut ($([math]::Round($oelfSize / 1MB, 2)) MB)" -ForegroundColor Green
        
        Write-Host "Signing OELF into PS4 FSELF (eboot.bin) with 64KB alignment..." -ForegroundColor Yellow
        & $PythonExe $MakeFselfScript $OelfOut $EbootOut --paid 0x3800000000000011
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path $EbootOut)) {
            Write-Error "FSELF generation from OELF failed!"
            exit 1
        }
    } else {
        Write-Warning "create-fself.exe failed, falling back to direct make_fself.py on ELF..."
        & $PythonExe $MakeFselfScript $ElfOut $EbootOut --paid 0x3800000000000011
    }
} else {
    Write-Host "Signing ELF into PS4 FSELF (eboot.bin)..." -ForegroundColor Yellow
    & $PythonExe $MakeFselfScript $ElfOut $EbootOut --paid 0x3800000000000011
}

if ($LASTEXITCODE -ne 0 -or -not (Test-Path $EbootOut)) {
    Write-Error "FSELF generation failed for $EbootOut"
    exit 1
}

$ebootSize = (Get-Item $EbootOut).Length
Write-Host "eboot.bin signed successfully: $EbootOut ($([math]::Round($ebootSize / 1MB, 2)) MB)" -ForegroundColor Green

# ------------------------------------------------------------------------------
# 12. Generate Orbis-Pub-Gen GP4 Project File
# ------------------------------------------------------------------------------
$Gp4Path = Join-Path $OutDir "pkg.gp4"
$gp4Xml = @"
<?xml version="1.0" encoding="utf-8" standalone="yes"?>
<psproject fmt="gp4" version="1000">
  <volume>
    <volume_type>pkg_ps4_app</volume_type>
    <volume_id>PS4_VOLUME</volume_id>
    <volume_ts>2026-09-19 00:00:00</volume_ts>
    <package content_id="$ContentId" passcode="00000000000000000000000000000000" storage_type="digital50" app_type="full" />
    <chunk_info chunk_count="1" scenario_count="1">
      <chunks>
        <chunk id="0" label="Main Chunk" />
      </chunks>
      <scenarios default_id="0">
        <scenario id="0" type="sp" initial_chunk_count="1" label="Main Scenario">
          <chunk_id id="0" />
        </scenario>
      </scenarios>
    </chunk_info>
  </volume>
  <files>
    <file targ_path="eboot.bin" orig_path="eboot.bin" />
    <file targ_path="sce_sys/param.sfo" orig_path="sce_sys/param.sfo" />
    <file targ_path="sce_sys/icon0.png" orig_path="sce_sys/icon0.png" />
    <file targ_path="sce_sys/about/right.sprx" orig_path="sce_sys/about/right.sprx" />
    <file targ_path="sce_module/libc.prx" orig_path="sce_module/libc.prx" />
    <file targ_path="sce_module/libSceFios2.prx" orig_path="sce_module/libSceFios2.prx" />
    <file targ_path="scene_save.txt" orig_path="scene_save.txt" />
  </files>
</psproject>
"@
Set-Content -Path $Gp4Path -Value $gp4Xml -Encoding UTF8
Write-Host "Generated GP4 packaging file: $Gp4Path" -ForegroundColor Green

# ------------------------------------------------------------------------------
# 13. Sync to Local shadPS4 Directory (if exists)
# ------------------------------------------------------------------------------
$localShadDir = "D:\Programs\ShadPS4\Games\PS4Build"
if (Test-Path "D:\Programs\ShadPS4\shadPS4.exe") {
    Write-Host "Syncing build artifacts to local shadPS4 ($localShadDir)..." -ForegroundColor Cyan
    if (-not (Test-Path $localShadDir)) {
        New-Item -ItemType Directory -Path $localShadDir -Force | Out-Null
    }
    Copy-Item -Path "$OutDir\*" -Destination $localShadDir -Recurse -Force -Exclude "obj"
    Write-Host "Local shadPS4 Games\PS4Build successfully updated!" -ForegroundColor Green
}

# ------------------------------------------------------------------------------
# 14. Completion Summary
# ------------------------------------------------------------------------------
Write-Host ""
Write-Host "========================================================================" -ForegroundColor Green
Write-Host "  PlayStation 4 Build COMPLETED SUCCESSFULLY!" -ForegroundColor Green
Write-Host "========================================================================" -ForegroundColor Green
Write-Host "  Output Directory: $OutDir"
Write-Host "  Executable ELF:   $ElfOut"
Write-Host "  FSELF eboot.bin:  $EbootOut"
Write-Host "  param.sfo:        $SfoPath"
Write-Host "  GP4 Project:      $Gp4Path"
Write-Host "  Modules:          $SceModuleDir\ (libc.prx, libSceFios2.prx)"
Write-Host "  Assets:           $ContentsDir\"
Write-Host ""
Write-Host "  To package into .pkg:" -ForegroundColor Cyan
Write-Host "    Use orbis-pub-cmd or orbis-pub-gen with $Gp4Path" -ForegroundColor Cyan
Write-Host "    or run on a jailbroken PS4 via FTP / GoldHEN / Mira installer." -ForegroundColor Cyan
Write-Host "========================================================================" -ForegroundColor Green
