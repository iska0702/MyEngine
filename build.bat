@echo off
setlocal enabledelayedexpansion
set VSCMD_SKIP_SENDTELEMETRY=1
cd /D "%~dp0"
if /I "%~1"=="release" if not "%IDK_INTERNAL_PROJECT_BUILD%"=="1" (
    set "IDK_BUILD_PROJECT=%~3"
    if not defined IDK_BUILD_PROJECT set "IDK_BUILD_PROJECT=%~dp0Projects\Test\Test.idkp"
    if "%~2"=="" (
        powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Build\BuildProject.ps1" -ProjectFile "!IDK_BUILD_PROJECT!"
    ) else (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Build\BuildProject.ps1" -ProjectFile "!IDK_BUILD_PROJECT!" -Platform "%~2"
    )
    exit /b !errorlevel!
)

:: Locate Visual Studio and Windows SDK on local machines or GitHub-hosted runners.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe not found: "%VSWHERE%"
    exit /b 1
)
set "VSINSTALL="
for /f "delims=" %%V in ('"%VSWHERE%" -latest -products * -property installationPath') do set "VSINSTALL=%%V"
if not defined VSINSTALL (
    echo ERROR: Visual Studio installation not found.
    exit /b 1
)
set "VCVARS=%VSINSTALL%\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%VCVARS%" (
    echo ERROR: C++ build tools missing: "%VCVARS%"
    exit /b 1
)
call "%VCVARS%" x64
if errorlevel 1 exit /b !errorlevel!
if not defined WindowsSDKVersion (
    echo ERROR: Windows SDK was not configured by Visual Studio.
    exit /b 1
)

:: -----------------------------------------------------------------------------
:: Determine Build Target & Platform: Editor vs Release, Steam vs Epic
:: -----------------------------------------------------------------------------
set BUILD_TARGET=%1
set BUILD_PLATFORM=%2

if /I "%BUILD_TARGET%"=="editor"       goto PARSE_PLATFORM_EDITOR
if /I "%BUILD_TARGET%"=="1"            goto PARSE_PLATFORM_EDITOR
if /I "%BUILD_TARGET%"=="release"      goto PARSE_PLATFORM_RELEASE
if /I "%BUILD_TARGET%"=="2"            goto PARSE_PLATFORM_RELEASE
if /I "%BUILD_TARGET%"=="server-linux" goto SET_SERVER_LINUX
if /I "%BUILD_TARGET%"=="server_linux" goto SET_SERVER_LINUX
if /I "%BUILD_TARGET%"=="linux"        goto SET_SERVER_LINUX
if /I "%BUILD_TARGET%"=="6"            goto SET_SERVER_LINUX
if /I "%BUILD_TARGET%"=="server" if /I "%BUILD_PLATFORM%"=="linux" goto SET_SERVER_LINUX
if /I "%BUILD_TARGET%"=="server"       goto SET_SERVER
if /I "%BUILD_TARGET%"=="5"            goto SET_SERVER
if /I "%BUILD_TARGET%"=="mac"          goto SET_MACOS_CLIENT
if /I "%BUILD_TARGET%"=="macos"        goto SET_MACOS_CLIENT
if /I "%BUILD_TARGET%"=="apple"        goto SET_MACOS_CLIENT
if /I "%BUILD_TARGET%"=="8"            goto SET_MACOS_CLIENT
if /I "%BUILD_TARGET%"=="ps4"          goto SET_PS4_CLIENT
if /I "%BUILD_TARGET%"=="orbis"        goto SET_PS4_CLIENT
if /I "%BUILD_TARGET%"=="playstation"  goto SET_PS4_CLIENT
if /I "%BUILD_TARGET%"=="playstation4" goto SET_PS4_CLIENT
if /I "%BUILD_TARGET%"=="9"            goto SET_PS4_CLIENT
if /I "%BUILD_TARGET%"=="all"          goto SET_ALL
if /I "%BUILD_TARGET%"=="4"            goto SET_ALL

echo ========================================================================
echo   Idontknow Build System (DirectX 12 + Multi-Platform Store Support)
echo ========================================================================
echo   [1] Editor Build (Steamworks)     -^> EditorBuild/       (SDK 1.65)
echo   [2] Editor Build (Epic Games EOS) -^> EditorBuild/       (Epic Online Services)
echo   [3] Project Game (Steamworks)     -^> Projects/Test/Build/
echo   [4] Project Game (Epic EOS)       -^> Projects/Test/Build/
echo   [5] Dedicated Server (Windows)    -^> ServerBuild/       (IdontknowServer.exe)
echo   [6] Dedicated Server (Linux ELF)  -^> ServerBuild_Linux/ (IdontknowServer for Linux)
echo   [7] Build All (Editor + Servers)
echo   [8] macOS Client (Apple Silicon)  -^> MacBuild/          (Native Mach-O + Metal Backend)
echo   [9] PlayStation 4 (OpenOrbis PS4) -^> PS4Build/          (Native ELF64 + FSELF eboot.bin + PKG)
echo ========================================================================
set /p TARGET_CHOICE="Select build option [1/2/3/4/5/6/7/8/9, default 1]: "

if "%TARGET_CHOICE%"=="1" goto SET_EDITOR_STEAM
if "%TARGET_CHOICE%"=="2" goto SET_EDITOR_EPIC
if "%TARGET_CHOICE%"=="3" goto SET_RELEASE_STEAM
if "%TARGET_CHOICE%"=="4" goto SET_RELEASE_EPIC
if "%TARGET_CHOICE%"=="5" goto SET_SERVER
if "%TARGET_CHOICE%"=="6" goto SET_SERVER_LINUX
if "%TARGET_CHOICE%"=="7" goto SET_ALL
if "%TARGET_CHOICE%"=="8" goto SET_MACOS_CLIENT
if "%TARGET_CHOICE%"=="9" goto SET_PS4_CLIENT
if /I "%TARGET_CHOICE%"=="server"       goto SET_SERVER
if /I "%TARGET_CHOICE%"=="server-linux" goto SET_SERVER_LINUX
if /I "%TARGET_CHOICE%"=="linux"        goto SET_SERVER_LINUX
if /I "%TARGET_CHOICE%"=="mac"          goto SET_MACOS_CLIENT
if /I "%TARGET_CHOICE%"=="macos"        goto SET_MACOS_CLIENT
if /I "%TARGET_CHOICE%"=="apple"        goto SET_MACOS_CLIENT
if /I "%TARGET_CHOICE%"=="ps4"          goto SET_PS4_CLIENT
if /I "%TARGET_CHOICE%"=="orbis"        goto SET_PS4_CLIENT
if /I "%TARGET_CHOICE%"=="all"          goto SET_ALL

:SET_EDITOR_STEAM
if /I "%MYENGINE_ENABLE_STEAM%"=="0" goto SET_EDITOR_STANDALONE
set TARGET_DIR=EditorBuild
set TARGET_NAME=Editor Build (Steamworks SDK v1.65)
set MODE_DEFINE=/DENGINE_EDITOR=1 /DENGINE_PLATFORM_STEAM=1
set PLATFORM_TYPE=STEAM
goto BUILD_CLIENT

:SET_EDITOR_EPIC
set TARGET_DIR=EditorBuild
set TARGET_NAME=Editor Build (Epic Games EOS v1.19.1.2)
set MODE_DEFINE=/DENGINE_EDITOR=1 /DENGINE_PLATFORM_EPIC=1
set PLATFORM_TYPE=EPIC
goto BUILD_CLIENT

:SET_RELEASE_STEAM
if not "%IDK_INTERNAL_PROJECT_BUILD%"=="1" goto BUILD_DEFAULT_PROJECT_STEAM
if /I "%MYENGINE_ENABLE_STEAM%"=="0" goto SET_RELEASE_STANDALONE
set TARGET_DIR=.ProjectBuild
set TARGET_NAME=Release Game Build (Steamworks SDK v1.65)
set MODE_DEFINE=/DENGINE_RELEASE=1 /DENGINE_PLATFORM_STEAM=1
set PLATFORM_TYPE=STEAM
goto BUILD_CLIENT

:SET_RELEASE_EPIC
if not "%IDK_INTERNAL_PROJECT_BUILD%"=="1" goto BUILD_DEFAULT_PROJECT_EPIC
set TARGET_DIR=.ProjectBuild
set TARGET_NAME=Release Game Build (Epic Games EOS v1.19.1.2)
set MODE_DEFINE=/DENGINE_RELEASE=1 /DENGINE_PLATFORM_EPIC=1
set PLATFORM_TYPE=EPIC
goto BUILD_CLIENT

:PARSE_PLATFORM_EDITOR
if /I "%BUILD_PLATFORM%"=="standalone" goto SET_EDITOR_STANDALONE
if /I "%BUILD_PLATFORM%"=="epic" goto SET_EDITOR_EPIC
if /I "%BUILD_PLATFORM%"=="eos"  goto SET_EDITOR_EPIC
goto SET_EDITOR_STEAM

:PARSE_PLATFORM_RELEASE
if /I "%BUILD_PLATFORM%"=="standalone" goto SET_RELEASE_STANDALONE
if /I "%BUILD_PLATFORM%"=="epic" goto SET_RELEASE_EPIC
if /I "%BUILD_PLATFORM%"=="eos"  goto SET_RELEASE_EPIC
goto SET_RELEASE_STEAM

:SET_EDITOR_STANDALONE
set TARGET_DIR=EditorBuild
set TARGET_NAME=Editor Build (Standalone, Steamworks disabled)
set MODE_DEFINE=/DENGINE_EDITOR=1 /DENGINE_PLATFORM_STANDALONE=1
set PLATFORM_TYPE=STANDALONE
goto BUILD_CLIENT

:SET_RELEASE_STANDALONE
set TARGET_DIR=.ProjectBuild
set TARGET_NAME=Release Game Build (Standalone, Steamworks disabled)
set MODE_DEFINE=/DENGINE_RELEASE=1 /DENGINE_PLATFORM_STANDALONE=1
set PLATFORM_TYPE=STANDALONE
goto BUILD_CLIENT

:SET_SERVER
goto BUILD_DEDICATED_SERVER

:SET_SERVER_LINUX
goto BUILD_DEDICATED_SERVER_LINUX

:SET_MACOS_CLIENT
goto BUILD_MACOS_CLIENT

:SET_ALL
set TARGET_DIR=EditorBuild
set TARGET_NAME=Editor Build (Standalone)
set MODE_DEFINE=/DENGINE_EDITOR=1 /DENGINE_PLATFORM_STANDALONE=1
set PLATFORM_TYPE=STANDALONE
call :BUILD_CLIENT_PROC
if %errorlevel% neq 0 exit /b !errorlevel!
call :BUILD_DEDICATED_SERVER
if errorlevel 1 exit /b 1
call :BUILD_DEDICATED_SERVER_LINUX
exit /b !errorlevel!

:: -----------------------------------------------------------------------------
:: CLIENT BUILD PROCEDURE
:: -----------------------------------------------------------------------------
:BUILD_CLIENT
call :BUILD_CLIENT_PROC
exit /b !errorlevel!

:BUILD_CLIENT_PROC
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Dependencies\FetchMsQuic.ps1"
if errorlevel 1 exit /b 1

if not exist "%~dp0ThirdParty\PhysX\Lib\bin\win.x86_64.vc143.md\release\PhysX.lib" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Build\BuildPhysX.ps1"
    if errorlevel 1 exit /b 1
)
echo.
echo ========================================================================
echo  Building Client: %TARGET_NAME%
echo  Output Directory: %~dp0\%TARGET_DIR%\
echo ========================================================================
echo.

if not exist %~dp0\%TARGET_DIR% mkdir %~dp0\%TARGET_DIR%
cd /D %~dp0\%TARGET_DIR%
del log.txt build_log.txt > nul 2>&1

:: Create folder structure
if not exist Contents mkdir Contents
if not exist Contents\shaders mkdir Contents\shaders
if not exist ThirdParty mkdir ThirdParty
if exist DLL (
    xcopy /E /Y /I /EXCLUDE:"%~dp0tools\Build\BuildCopyExclusions.txt" DLL\* ThirdParty\ > nul 2>&1
    rmdir /S /Q DLL > nul 2>&1
)
if not exist ThirdParty\Discord mkdir ThirdParty\Discord
if not exist ThirdParty\NVIDIA\Streamline mkdir ThirdParty\NVIDIA\Streamline
if not exist ThirdParty\DirectX mkdir ThirdParty\DirectX
if not exist ThirdParty\NVIDIA\PhysX mkdir ThirdParty\NVIDIA\PhysX
if not exist ThirdParty\AMD mkdir ThirdParty\AMD
if not exist ThirdParty\Intel mkdir ThirdParty\Intel
if not exist ThirdParty\DirectStorage mkdir ThirdParty\DirectStorage

:: Clean up any legacy loose files in target directory
if exist UI rmdir /S /Q UI > nul 2>&1
if exist Config rmdir /S /Q Config > nul 2>&1
if exist Content rmdir /S /Q Content > nul 2>&1
if exist src rmdir /S /Q src > nul 2>&1
if exist build rmdir /S /Q build > nul 2>&1
if exist Contents\Levels rmdir /S /Q Contents\Levels > nul 2>&1
if exist Contents\world.pak del /Q Contents\world.pak > nul 2>&1
if exist license.lic del /Q license.lic > nul 2>&1

:: Pre-compile shaders with DXC into Contents\shaders
set "DXC_EXE=%WindowsSdkDir%bin\%WindowsSDKVersion%x64\dxc.exe"
if exist "%DXC_EXE%" (
    echo Compiling shaders...
    "%DXC_EXE%" -T as_6_5 -E ASMain -Fo Contents\shaders\shaders_ASMain.cso ..\shaders\shaders.hlsl > nul
    "%DXC_EXE%" -T vs_6_5 -E RasterVS -Fo Contents\shaders\raster_fallback_RasterVS.cso ..\shaders\raster_fallback.hlsl > nul
    if errorlevel 1 exit /b 1
    "%DXC_EXE%" -T vs_6_5 -E TessVS -Fo Contents\shaders\tessellation_TessVS.cso ..\shaders\tessellation.hlsl > nul
    if errorlevel 1 exit /b 1
    "%DXC_EXE%" -T hs_6_5 -E TessHS -Fo Contents\shaders\tessellation_TessHS.cso ..\shaders\tessellation.hlsl > nul
    if errorlevel 1 exit /b 1
    "%DXC_EXE%" -T ds_6_5 -E TessDS -Fo Contents\shaders\tessellation_TessDS.cso ..\shaders\tessellation.hlsl > nul
    if errorlevel 1 exit /b 1
    "%DXC_EXE%" -T ms_6_5 -E MSMain -Fo Contents\shaders\shaders_MSMain.cso ..\shaders\shaders.hlsl > nul
    "%DXC_EXE%" -T ps_6_5 -E PSMain -Fo Contents\shaders\shaders_PSMain.cso ..\shaders\shaders.hlsl > nul
    "%DXC_EXE%" -T cs_6_5 -E CSMain -Fo Contents\shaders\hiz_CSMain.cso ..\shaders\hiz.hlsl > nul
    "%DXC_EXE%" -T cs_6_5 -E CSMain -Fo Contents\shaders\vsm_management_CSMain.cso ..\shaders\vsm_management.hlsl > nul
    "%DXC_EXE%" -T cs_6_5 -E CSMain -Fo Contents\shaders\astra_radiosity_CSMain.cso ..\shaders\astra_radiosity.hlsl > nul
    "%DXC_EXE%" -T cs_6_5 -E CSMain -Fo Contents\shaders\taa_CSMain.cso ..\shaders\taa.hlsl > nul
    "%DXC_EXE%" -T cs_6_5 -E InjectLightCS -Fo Contents\shaders\volumetric_fog_InjectLightCS.cso ..\shaders\volumetric_fog.hlsl > nul
    "%DXC_EXE%" -T cs_6_5 -E AccumulateFogCS -Fo Contents\shaders\volumetric_fog_AccumulateFogCS.cso ..\shaders\volumetric_fog.hlsl > nul
    "%DXC_EXE%" -T cs_6_5 -E CSMain -Fo Contents\shaders\ssr_CSMain.cso ..\shaders\ssr.hlsl > nul
    "%DXC_EXE%" -T vs_6_5 -E VSMain -Fo Contents\shaders\rvt_VSMain.cso ..\shaders\rvt.hlsl > nul
    "%DXC_EXE%" -T ps_6_5 -E PSMain -Fo Contents\shaders\rvt_PSMain.cso ..\shaders\rvt.hlsl > nul
    "%DXC_EXE%" -T vs_6_5 -E VSMain -Fo Contents\shaders\rvt_decal_VSMain.cso ..\shaders\rvt_decal.hlsl > nul
    "%DXC_EXE%" -T ps_6_5 -E PSMain -Fo Contents\shaders\rvt_decal_PSMain.cso ..\shaders\rvt_decal.hlsl > nul
    "%DXC_EXE%" -T vs_6_5 -E VSMain -Fo Contents\shaders\skybox_VSMain.cso ..\shaders\skybox.hlsl > nul
    "%DXC_EXE%" -T ps_6_5 -E PSMain -Fo Contents\shaders\skybox_PSMain.cso ..\shaders\skybox.hlsl > nul
    "%DXC_EXE%" -T vs_6_5 -E VSMain -Fo Contents\shaders\postprocess_VSMain.cso ..\shaders\postprocess.hlsl > nul
    "%DXC_EXE%" -T ps_6_5 -E PSMain -Fo Contents\shaders\postprocess_PSMain.cso ..\shaders\postprocess.hlsl > nul
    "%DXC_EXE%" -T vs_6_5 -E VSMain -Fo Contents\shaders\mboit_gather_VSMain.cso ..\shaders\mboit_gather.hlsl > nul
    "%DXC_EXE%" -T ps_6_5 -E PSMain -Fo Contents\shaders\mboit_gather_PSMain.cso ..\shaders\mboit_gather.hlsl > nul
    "%DXC_EXE%" -T vs_6_5 -E VSMain -Fo Contents\shaders\mboit_resolve_VSMain.cso ..\shaders\mboit_resolve.hlsl > nul
    "%DXC_EXE%" -T ps_6_5 -E PSMain -Fo Contents\shaders\mboit_resolve_PSMain.cso ..\shaders\mboit_resolve.hlsl > nul
    "%DXC_EXE%" -T vs_6_5 -E VSMain -Fo Contents\shaders\composite_VSMain.cso ..\shaders\composite.hlsl > nul
    "%DXC_EXE%" -T ps_6_5 -E PSMain -Fo Contents\shaders\composite_PSMain.cso ..\shaders\composite.hlsl > nul
    "%DXC_EXE%" -T vs_6_0 -E VSMain -Fo Contents\shaders\ssao_VSMain.cso ..\shaders\ssao.hlsl > nul
    "%DXC_EXE%" -T ps_6_0 -E PSMain -Fo Contents\shaders\ssao_PSMain.cso ..\shaders\ssao.hlsl > nul
    "%DXC_EXE%" -T cs_6_0 -E CSMain -Fo Contents\shaders\ssgi_CSMain.cso ..\shaders\ssgi.hlsl > nul
    "%DXC_EXE%" -T vs_6_0 -E VSMain -Fo Contents\shaders\ssao_blur_VSMain.cso ..\shaders\ssao_blur.hlsl > nul
    "%DXC_EXE%" -T ps_6_0 -E PSMain -Fo Contents\shaders\ssao_blur_PSMain.cso ..\shaders\ssao_blur.hlsl > nul
    "%DXC_EXE%" -T vs_6_5 -E VSMain -Fo Contents\shaders\fsr_upscale_VSMain.cso ..\shaders\fsr_upscale.hlsl > nul
    "%DXC_EXE%" -T ps_6_5 -E PSMain -Fo Contents\shaders\fsr_upscale_PSMain.cso ..\shaders\fsr_upscale.hlsl > nul
    "%DXC_EXE%" -T lib_6_3 -Fo Contents\shaders\raytracing_.cso ..\shaders\raytracing.hlsl > nul
    
    :: Copy HLSL files for runtime fallback
    copy /Y ..\shaders\*.hlsl Contents\shaders\ > nul
    
    echo Shader compilation done.
) else (
    echo WARNING: DXC not found, shaders will be compiled at runtime ^(slow^)
)

:: Copy main Content to Contents for packing
echo Copying content from src\Content for packing...
set "IDK_CONTENT_SOURCE=%IDK_PROJECT_CONTENT%"
if not defined IDK_CONTENT_SOURCE set "IDK_CONTENT_SOURCE=%~dp0Templates\Default\Content"
if exist "%IDK_CONTENT_SOURCE%" xcopy /E /Y /I "%IDK_CONTENT_SOURCE%\*" Contents\Content\ > nul
if defined IDK_PROJECT_CONTENT xcopy /E /Y /I "%IDK_PROJECT_CONTENT%\*" Contents\Content\ > nul

:: Pack content into 9 PAKs (content01.pak - content09.pak)
echo Packing all content from src\Content (Shaders + Models + Textures + Videos + UI + Config)...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Content\pack_all_content.ps1" -SourceContent "%IDK_CONTENT_SOURCE%"
if errorlevel 1 (
    echo Content packing FAILED!
    exit /b 1
)
echo All general content packed into 9 PAK files (content01.pak - content09.pak) in Contents\

:: Clean up temporary loose unpacked files and directories
if exist UI rmdir /S /Q UI > nul 2>&1
if exist Config rmdir /S /Q Config > nul 2>&1
if exist Content rmdir /S /Q Content > nul 2>&1
if exist src rmdir /S /Q src > nul 2>&1
if exist Contents\Content rmdir /S /Q Contents\Content > nul 2>&1
if exist Contents\shaders rmdir /S /Q Contents\shaders > nul 2>&1
if exist Contents\Levels rmdir /S /Q Contents\Levels > nul 2>&1
if exist Contents\world.pak del /Q Contents\world.pak > nul 2>&1
if exist Contents\*.level del /Q Contents\*.level > nul 2>&1

:: Source files list including Networking and Epic/Steam
set SOURCES=..\src\Core\main.cpp ..\src\Core\ProjectBuilder.cpp ..\src\Core\Application.cpp ..\src\Core\Window.cpp ..\src\Renderer\Renderer.cpp ..\src\Upscaling\XeSS\XeSSSystem.cpp ..\src\Storage\DirectStorage\DirectStorageSystem.cpp ..\src\Platform\PlatformUtils.cpp ..\src\Storage\PakCompression.cpp ..\src\Renderer\ModelLoader.cpp ..\src\Renderer\Camera.cpp ..\src\Renderer\CameraSystem.cpp ..\src\Core\Timer.cpp ..\src\Renderer\TextureStreamer.cpp ..\src\Renderer\Texture.cpp ..\src\Sequencer\Sequencer.cpp ..\src\Security\SecuritySystem.cpp ..\src\Security\MBAObfuscator.cpp ..\src\Security\VirtualMachine.cpp ..\src\Security\HardwareID.cpp ..\src\Security\LicenseSystem.cpp ..\src\Rain\RainSystem.cpp ..\src\Rain\WeatherSystem.cpp ..\src\Physics\PhysicsSystem.cpp ..\src\Physics\ClothSystem.cpp ..\src\Physics\HairSystem.cpp ..\src\Physics\CharacterController.cpp ..\src\UI\WebMenuSystem.cpp ..\src\UI\ContentBrowser.cpp ..\src\Level\LevelSystem.cpp ..\src\Storage\SaveSystem.cpp ..\src\Steam\SteamSystem.cpp ..\src\Epic\EpicSystem.cpp ..\src\Discord\DiscordSystem.cpp ..\src\Animation\SkinnedMesh.cpp ..\src\Animation\CharacterAsset.cpp ..\src\Animation\AnimationSystem.cpp ..\src\Animation\BoneClassifierAI.cpp ..\src\Animation\BoneMappingWindow.cpp ..\src\Animation\Ozz\OzzRuntime.cpp ..\src\Animation\Ozz\OzzSerializer.cpp ..\src\Animation\Ozz\OzzImporter.cpp ..\src\Animation\MotionMatching\MotionDatabase.cpp ..\src\Animation\MotionMatching\MotionTrajectory.cpp ..\src\Animation\MotionMatching\MotionMatcher.cpp ..\src\Animation\MotionMatching\MotionController.cpp ..\src\Input\DualSense.cpp ..\src\Input\DualShock.cpp ..\src\Audio\DualSenseAudio.cpp ..\src\Audio\AudioSystem.cpp ..\ThirdParty\EngineDependencies\ufbx.c ..\ThirdParty\EngineDependencies\lua54\lua-5.4.7\src\onelua.c ..\src\Networking\BitWriter.cpp ..\src\Networking\BitReader.cpp ..\src\Networking\Packet.cpp ..\src\Networking\NetworkSocket.cpp ..\src\Networking\NetworkConnection.cpp ..\src\Networking\NetworkObject.cpp ..\src\Networking\NetworkReplication.cpp ..\src\Networking\ServerAntiCheat.cpp ..\src\Networking\ClientAntiCheat.cpp ..\src\Networking\NetworkServer.cpp ..\src\Networking\NetworkClient.cpp ..\src\Networking\MultiplayerManager.cpp ..\src\Scene\SceneNode.cpp ..\src\Scene\SceneGraph.cpp ..\src\Script\ScriptSystem.cpp ..\src\AI\NavMesh.cpp ..\src\AI\NavAgent.cpp ..\src\AI\NavigationSystem.cpp ..\src\VFX\ParticleEmitter.cpp ..\src\VFX\ParticleSystem.cpp ..\src\Terrain\TerrainSystem.cpp ..\src\PostProcess\PostProcessSystem.cpp ..\src\Asset\AssetRegistry.cpp ..\src\Prefab\Prefab.cpp ..\src\Prefab\PrefabSystem.cpp ..\src\Debug\Profiler.cpp ..\src\Debug\DebugConsole.cpp ..\src\Input\InputMapping.cpp

for %%f in (%SOURCES%) do (
    if not exist %%f (
        echo ERROR: Source file %%f missing!
        exit /b 1
    )
)

echo SOURCES: %SOURCES%
:: Platform specific includes & libs
if "%PLATFORM_TYPE%"=="EPIC" (
    set PLATFORM_INC=/I..\ThirdParty\EOS-SDK-58105819-Release-v1.19.2.1\SDK\Include
    set PLATFORM_LIB=..\ThirdParty\EOS-SDK-58105819-Release-v1.19.2.1\SDK\Lib\EOSSDK-Win64-Shipping.lib
    set PLATFORM_DELAYLOAD=/DELAYLOAD:EOSSDK-Win64-Shipping.dll
 ) else if "%PLATFORM_TYPE%"=="STEAM" (
    set PLATFORM_INC=/I..\ThirdParty\steamworks_sdk_165\sdk\public
    set PLATFORM_LIB=..\ThirdParty\steamworks_sdk_165\sdk\redistributable_bin\win64\steam_api64.lib
    set PLATFORM_DELAYLOAD=/DELAYLOAD:steam_api64.dll
) else (
    set PLATFORM_INC=
    set PLATFORM_LIB=
    set PLATFORM_DELAYLOAD=
)

cl.exe /nologo /MP4 /bigobj /Zm300 /EHsc /W3 /utf-8 /DNDEBUG %MODE_DEFINE% /DWIN32_LEAN_AND_MEAN /D_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS /MD /O2 /std:c++20 /DUNICODE /D_UNICODE /I.. /I..\src /I..\src\Core /I..\src\Renderer /I..\src\Input /I..\src\Audio /I..\src\Storage /I..\src\Sequencer /I..\ThirdParty\EngineDependencies /I..\ThirdParty\EngineDependencies\lua54\lua-5.4.7\src /I..\src\imgui /I..\ThirdParty\streamline-sdk-v2.14.1\include /I..\ThirdParty\AgilitySDK-1.619.6\build\native\include /I..\ThirdParty\DirectX-Headers\include /I..\ThirdParty\FidelityFX-SDK-v1.1.4\sdk\include\FidelityFX /I..\ThirdParty\FidelityFX-SDK-v1.1.4\sdk\include\FidelityFX\gpu /I..\ThirdParty\XeSS_SDK_3.0.2\inc /I..\ThirdParty\XeSS_SDK_3.0.2\inc\xess /I..\ThirdParty\XeSS_SDK_3.0.2\inc\xess_fg /I..\ThirdParty\XeSS_SDK_3.0.2\inc\xell /I..\ThirdParty\DirectStorage\sdk-package\native\include /I..\ThirdParty\PhysX\physx\include /I..\ThirdParty\WebView2\build\native\include /I..\ThirdParty\discord_social_sdk\include %PLATFORM_INC% /Fe"Idontknow.pending.exe" %SOURCES% ..\src\imgui\*.cpp d3d12.lib dxgi.lib user32.lib comdlg32.lib d3dcompiler.lib dxcompiler.lib bcrypt.lib wintrust.lib crypt32.lib hid.lib setupapi.lib cfgmgr32.lib xinput.lib ole32.lib advapi32.lib ws2_32.lib shlwapi.lib iphlpapi.lib ..\ThirdParty\WebView2\build\native\x64\WebView2LoaderStatic.lib ..\ThirdParty\streamline-sdk-v2.14.1\lib\x64\sl.interposer.lib ..\ThirdParty\XeSS_SDK_3.0.2\lib\libxess.lib ..\ThirdParty\XeSS_SDK_3.0.2\lib\libxess_fg.lib ..\ThirdParty\XeSS_SDK_3.0.2\lib\libxell.lib ..\ThirdParty\XeSS_SDK_3.0.2\lib\libxess_dx11.lib ..\ThirdParty\DirectStorage\sdk-package\native\lib\x64\dstorage.lib ..\ThirdParty\PhysX\Lib\bin\win.x86_64.vc143.md\release\PhysX.lib ..\ThirdParty\PhysX\Lib\bin\win.x86_64.vc143.md\release\PhysXCommon.lib ..\ThirdParty\PhysX\Lib\bin\win.x86_64.vc143.md\release\PhysXExtensions_static.lib ..\ThirdParty\PhysX\Lib\bin\win.x86_64.vc143.md\release\PhysXVehicle_static.lib ..\ThirdParty\PhysX\Lib\bin\win.x86_64.vc143.md\release\PhysXFoundation.lib ..\ThirdParty\PhysX\Lib\bin\win.x86_64.vc143.md\release\PhysXPvdSDK_static.lib ..\ThirdParty\PhysX\Lib\bin\win.x86_64.vc143.md\release\PhysXCharacterKinematic_static.lib ..\ThirdParty\discord_social_sdk\lib\release\discord_partner_sdk.lib %PLATFORM_LIB% /link /MANIFEST:EMBED /MANIFESTINPUT:..\src\Core\app.manifest /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT %PLATFORM_DELAYLOAD% /DELAYLOAD:sl.interposer.dll /DELAYLOAD:libxess.dll /DELAYLOAD:libxess_fg.dll /DELAYLOAD:libxell.dll /DELAYLOAD:libxess_dx11.dll /DELAYLOAD:dstorage.dll /DELAYLOAD:dstoragecore.dll /DELAYLOAD:dxcompiler.dll /DELAYLOAD:PhysX.dll /DELAYLOAD:PhysXCommon.dll /DELAYLOAD:PhysXFoundation.dll /DELAYLOAD:discord_partner_sdk.dll /ignore:4199 delayimp.lib > build_log.txt 2>&1
if errorlevel 1 (
    echo Compilation FAILED!
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Build\CleanBuildOutput.ps1" -BuildDirectory "%CD%"
    echo Check Diagnostics\Archive\%TARGET_DIR% for details.
    exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Build\RemoveBuildObjects.ps1"
if errorlevel 1 exit /b 1

:: Copy DLLs and dependencies into target folder
if exist ..\build\ThirdParty (
    xcopy /E /Y /I /EXCLUDE:"%~dp0tools\Build\BuildCopyExclusions.txt" ..\build\ThirdParty ThirdParty\ > nul
)
if not exist ThirdParty mkdir ThirdParty

:: Platform runtimes are packaged by cmake\PackageWindows.ps1 below.

:: Copy Discord Social SDK DLLs & Krisp assets
if not exist ThirdParty\Discord mkdir ThirdParty\Discord
if exist ..\ThirdParty\discord_social_sdk\bin\release (
    copy /Y ..\ThirdParty\discord_social_sdk\bin\release\*.dll ThirdParty\Discord\ > nul 2>&1
    copy /Y ..\ThirdParty\discord_social_sdk\bin\release\*.kef ThirdParty\Discord\ > nul 2>&1
)

:: Copy FidelityFX FSR DLLs
if exist ..\ThirdParty\FidelityFX-SDK-v1.1.4\bin (
    copy /Y ..\ThirdParty\FidelityFX-SDK-v1.1.4\bin\amd_*.dll ThirdParty\AMD\ > nul 2>&1
    copy /Y ..\ThirdParty\FidelityFX-SDK-v1.1.4\bin\D3D12\D3D12Core.dll ThirdParty\DirectX\ > nul 2>&1
    copy /Y ..\ThirdParty\FidelityFX-SDK-v1.1.4\bin\dxcompiler.dll ThirdParty\DirectX\ > nul 2>&1
    copy /Y ..\ThirdParty\FidelityFX-SDK-v1.1.4\bin\dxil.dll ThirdParty\DirectX\ > nul 2>&1
    copy /Y ..\ThirdParty\FidelityFX-SDK-v1.1.4\bin\WinPixEventRuntime.dll ThirdParty\DirectX\ > nul 2>&1
)
if exist ..\ThirdParty\FidelityFX-SDK-v1.1.4\PrebuiltSignedDLL (
    copy /Y ..\ThirdParty\FidelityFX-SDK-v1.1.4\PrebuiltSignedDLL\amd_*.dll ThirdParty\AMD\ > nul 2>&1
    copy /Y ..\ThirdParty\FidelityFX-SDK-v1.1.4\PrebuiltSignedDLL\D3D12Core.dll ThirdParty\DirectX\ > nul 2>&1
    copy /Y ..\ThirdParty\FidelityFX-SDK-v1.1.4\PrebuiltSignedDLL\dxcompiler.dll ThirdParty\DirectX\ > nul 2>&1
    copy /Y ..\ThirdParty\FidelityFX-SDK-v1.1.4\PrebuiltSignedDLL\dxil.dll ThirdParty\DirectX\ > nul 2>&1
    copy /Y ..\ThirdParty\FidelityFX-SDK-v1.1.4\PrebuiltSignedDLL\WinPixEventRuntime.dll ThirdParty\DirectX\ > nul 2>&1
)
if exist ThirdParty\AMD\amd_fidelityfx_vk.dll del /Q ThirdParty\AMD\amd_fidelityfx_vk.dll > nul 2>&1

:: Copy Streamline SDK Release DLLs
if exist ..\ThirdParty\streamline-sdk-v2.14.1\bin\x64 copy /Y ..\ThirdParty\streamline-sdk-v2.14.1\bin\x64\*.dll ThirdParty\NVIDIA\Streamline\ > nul 2>&1
if exist ThirdParty\NVIDIA\Streamline\WinPixEventRuntime.dll (
    copy /Y ThirdParty\NVIDIA\Streamline\WinPixEventRuntime.dll ThirdParty\DirectX\ > nul 2>&1
    del /Q ThirdParty\NVIDIA\Streamline\WinPixEventRuntime.dll > nul 2>&1
)

:: Copy Intel XeSS DLLs into ThirdParty\Intel\ only
if not exist ThirdParty\Intel mkdir ThirdParty\Intel
if exist ..\ThirdParty\XeSS_SDK_3.0.2\bin copy /Y ..\ThirdParty\XeSS_SDK_3.0.2\bin\*.dll ThirdParty\Intel\ > nul 2>&1
if exist libxess*.dll del /Q libxess*.dll > nul 2>&1
if exist libxell.dll del /Q libxell.dll > nul 2>&1

:: Copy DirectStorage DLLs into ThirdParty\DirectStorage\ only
if not exist ThirdParty\DirectStorage mkdir ThirdParty\DirectStorage
if exist ..\ThirdParty\DirectStorage\sdk-package\native\bin\x64 copy /Y ..\ThirdParty\DirectStorage\sdk-package\native\bin\x64\*.dll ThirdParty\DirectStorage\ > nul 2>&1
if exist dstorage*.dll del /Q dstorage*.dll > nul 2>&1

:: Embed cryptographic DLL integrity manifest into content archive (Contents\content01.pak)
powershell -NoProfile -ExecutionPolicy Bypass -File "..\cmake\PackageWindows.ps1" -OutputDirectory "%CD%" -DependenciesOnly -Platform "%PLATFORM_TYPE%"
if errorlevel 1 exit /b 1
echo Embedding DLL integrity manifest into content archives (Contents\content01.pak)...
powershell -NoProfile -ExecutionPolicy Bypass -File "..\tools\Security\generate_dll_manifest.ps1" -TargetDir "ThirdParty" -TargetPak "Contents\content01.pak"
if errorlevel 1 (
    echo DLL integrity manifest generation FAILED!
    exit /b 1
)
if exist Contents\security.pak del /Q Contents\security.pak > nul 2>&1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Content\CookStreamingTextures.ps1" -Build "%TARGET_DIR%"
if errorlevel 1 exit /b 1
if exist Contents\Streaming rmdir /S /Q Contents\Streaming > nul 2>&1

:: Clean up temporary & intermediate compiler files
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Build\RemoveBuildObjects.ps1"
if errorlevel 1 exit /b 1
del /Q *.obj > nul 2>&1
del /Q *.exp > nul 2>&1
del /Q *.lib > nul 2>&1
del /Q steam_appid.txt > nul 2>&1
del /Q ThirdParty\Steamworks\steam_appid.txt > nul 2>&1
if exist ..\steam_appid.txt del /Q ..\steam_appid.txt > nul 2>&1
if exist license.lic del /Q license.lic > nul 2>&1
if exist ..\license.lic del /Q ..\license.lic > nul 2>&1
if exist ..\EditorBuild\license.lic del /Q ..\EditorBuild\license.lic > nul 2>&1
if exist ..\.ProjectBuild\license.lic del /Q ..\.ProjectBuild\license.lic > nul 2>&1

:: In Release build, guarantee NO log.txt, NO build_log.txt, and NO license.lic remain
if /I "%TARGET_DIR%"==".ProjectBuild" (
    if exist log.txt del /Q log.txt > nul 2>&1
    if exist build_log.txt del /Q build_log.txt > nul 2>&1
    if exist license.lic del /Q license.lic > nul 2>&1
)

echo.
echo ========================================================================
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Build\CleanBuildOutput.ps1" -BuildDirectory "%CD%"
if errorlevel 1 exit /b 1
move /Y Idontknow.pending.exe Idontknow.exe > nul
if errorlevel 1 (
    echo Cannot replace Idontknow.exe. Close the running game or editor and try again.
    exit /b 1
)
echo  Build SUCCESSFUL!
echo  Target: %TARGET_NAME%
echo  Executable: %~dp0\%TARGET_DIR%\Idontknow.exe
echo ========================================================================
exit /b 0

:: -----------------------------------------------------------------------------
:: DEDICATED SERVER BUILD PROCEDURE (Headless, Pure Console, No DX12)
:: -----------------------------------------------------------------------------
:BUILD_DEDICATED_SERVER
echo.
echo ========================================================================
echo  Building Dedicated Server: Headless Console (No DX12 / No GPU / Low Memory)
echo  Output Directory: %~dp0\ServerBuild\
echo ========================================================================
echo.

if not exist %~dp0\ServerBuild mkdir %~dp0\ServerBuild
cd /D %~dp0\ServerBuild
del log.txt build_log.txt > nul 2>&1

set SERVER_SOURCES=..\src\Server\ServerMain.cpp ..\src\Server\DedicatedServer.cpp ..\src\Level\LevelSystem.cpp ..\src\Core\Timer.cpp ..\src\Networking\BitWriter.cpp ..\src\Networking\BitReader.cpp ..\src\Networking\Packet.cpp ..\src\Networking\NetworkSocket.cpp ..\src\Networking\NetworkConnection.cpp ..\src\Networking\NetworkObject.cpp ..\src\Networking\NetworkReplication.cpp ..\src\Networking\ServerAntiCheat.cpp ..\src\Networking\NetworkServer.cpp

for %%f in (%SERVER_SOURCES%) do (
    if not exist %%f (
        echo ERROR: Source file %%f missing!
        exit /b 1
    )
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Dependencies\FetchMsQuic.ps1"
if errorlevel 1 exit /b 1

echo SERVER SOURCES: %SERVER_SOURCES%
if exist IdontknowServer.exe del /Q IdontknowServer.exe > nul 2>&1

cl.exe /nologo /EHsc /W3 /DNDEBUG /DENGINE_SERVER=1 /DWIN32_LEAN_AND_MEAN /D_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS /MD /O2 /std:c++20 /DUNICODE /D_UNICODE /I.. /I..\src /I..\src\Core /Fe"IdontknowServer.exe" %SERVER_SOURCES% ws2_32.lib advapi32.lib user32.lib > build_log.txt 2>&1
if errorlevel 1 (
    echo Dedicated Server Compilation FAILED!
    echo Check ServerBuild\build_log.txt for details.
    exit /b 1
)

if not exist ThirdParty\Microsoft\MsQuic mkdir ThirdParty\Microsoft\MsQuic
copy /Y ..\ThirdParty\MsQuic-2.6.1-OpenSSL\build\native\bin\x64\msquic.dll ThirdParty\Microsoft\MsQuic\ > nul
if errorlevel 1 exit /b 1

:: Copy default server.cfg and map .pak files into server root
if exist ..\server.cfg copy /Y ..\server.cfg .\server.cfg > nul
if exist ..\ServerMaps\*.pak copy /Y ..\ServerMaps\*.pak .\ > nul 2>&1
if exist ServerMaps rmdir /S /Q ServerMaps > nul 2>&1

:: Clean up temporary .obj, .exp, .lib
del /Q *.obj > nul 2>&1
del /Q *.exp > nul 2>&1
del /Q *.lib > nul 2>&1

echo.
echo ========================================================================
echo  Dedicated Server Build SUCCESSFUL!
echo  Executable: %~dp0\ServerBuild\IdontknowServer.exe
echo  Config:     %~dp0\ServerBuild\server.cfg
echo ========================================================================
exit /b 0

:: -----------------------------------------------------------------------------
:: DEDICATED SERVER LINUX BUILD PROCEDURE (Headless ELF64, Native Linux Binary)
:: -----------------------------------------------------------------------------
:BUILD_DEDICATED_SERVER_LINUX
echo.
echo ========================================================================
echo  Building Dedicated Server: Linux ELF64 (No DX12 / Zero GPU Dependencies)
echo  Output Directory: %~dp0\ServerBuild_Linux\
echo ========================================================================
echo.

if not exist %~dp0\ServerBuild_Linux mkdir %~dp0\ServerBuild_Linux
cd /D %~dp0\ServerBuild_Linux
del log.txt build_log.txt > nul 2>&1

set SERVER_SOURCES=..\src\Server\ServerMain.cpp ..\src\Server\DedicatedServer.cpp ..\src\Level\LevelSystem.cpp ..\src\Core\Timer.cpp ..\src\Networking\BitWriter.cpp ..\src\Networking\BitReader.cpp ..\src\Networking\Packet.cpp ..\src\Networking\NetworkSocket.cpp ..\src\Networking\NetworkConnection.cpp ..\src\Networking\NetworkObject.cpp ..\src\Networking\NetworkReplication.cpp ..\src\Networking\ServerAntiCheat.cpp ..\src\Networking\NetworkServer.cpp

for %%f in (%SERVER_SOURCES%) do (
    if not exist %%f (
        echo ERROR: Source file %%f missing!
        exit /b 1
    )
)

echo SERVER SOURCES: %SERVER_SOURCES%
if exist IdontknowServer del /Q IdontknowServer > nul 2>&1

set ZIG_EXE=..\ThirdParty\zig\zig-windows-x86_64-0.13.0\zig.exe
if not exist %ZIG_EXE% set ZIG_EXE=..\ThirdParty\zig\zig.exe

if not exist %ZIG_EXE% (
    where wsl >nul 2>&1
    if %errorlevel% equ 0 (
        echo [Linux Build] Compiling native Linux server via WSL...
        cd /D %~dp0
        wsl make server
        if !errorlevel! equ 0 (
            copy /Y bin\IdontknowServer ServerBuild_Linux\IdontknowServer >nul 2>&1
            goto LINUX_BUILD_SUCCESS
        )
    )
)

if not exist %ZIG_EXE% (
    echo [Notice] Downloading lightweight Linux cross-compiler to ThirdParty\zig...
    if not exist ..\ThirdParty\zig mkdir ..\ThirdParty\zig
    curl.exe -L -o "..\ThirdParty\zig\zig.zip" "https://zig.linus.dev/zig/zig-windows-x86_64-0.13.0.zip"
    if exist ..\ThirdParty\zig\zig.zip (
        tar.exe -xf ..\ThirdParty\zig\zig.zip -C ..\ThirdParty\zig\
        del /Q ..\ThirdParty\zig\zig.zip >nul 2>&1
        set ZIG_EXE=..\ThirdParty\zig\zig-windows-x86_64-0.13.0\zig.exe
    )
)

if exist %ZIG_EXE% (
    echo [Linux Build] Compiling native Linux ELF binary using Zig toolchain...
    %ZIG_EXE% c++ -target x86_64-linux-gnu.2.28 -std=c++20 -O3 -DNDEBUG -DENGINE_SERVER=1 -I.. -I..\src %SERVER_SOURCES% -o IdontknowServer -pthread > build_log.txt 2>&1
    if errorlevel 1 (
        echo Linux Server Compilation FAILED!
        type build_log.txt
        exit /b 1
    )
) else (
    echo ERROR: A native Linux compiler is required. Install Zig or configure WSL.
    exit /b 1
)

:LINUX_BUILD_SUCCESS
if not exist IdontknowServer (
    echo ERROR: Linux executable was not produced.
    exit /b 1
)
:: Copy default server.cfg and map .pak files into server root
if exist ..\server.cfg copy /Y ..\server.cfg .\server.cfg > nul
if exist ..\ServerMaps\*.pak copy /Y ..\ServerMaps\*.pak .\ > nul 2>&1
if exist ServerMaps rmdir /S /Q ServerMaps > nul 2>&1

:: Clean up temporary .o or .obj files
del /Q *.o > nul 2>&1
del /Q *.obj > nul 2>&1

echo.
echo ========================================================================
echo  Dedicated Server (Linux) Build SUCCESSFUL!
echo  Directory:  %~dp0\ServerBuild_Linux\
if exist IdontknowServer (
    echo  Binary:     %~dp0\ServerBuild_Linux\IdontknowServer [Native Linux ELF64]
) else (
    echo  Binary:     %~dp0\ServerBuild_Linux\IdontknowServer.exe [Wine compatible]
)
echo  Config:     %~dp0\ServerBuild_Linux\server.cfg
echo.
echo  Ready to transfer:
echo    Simply copy the 'ServerBuild_Linux' folder to your Linux VPS / machine!
echo    Run on Linux:
echo      chmod +x IdontknowServer
echo      ./IdontknowServer --port 7777
echo ========================================================================
cd /D %~dp0
exit /b 0

:: -----------------------------------------------------------------------------
:: MACOS CLIENT (APPLE SILICON) BUILD PROCEDURE
:: -----------------------------------------------------------------------------
:BUILD_MACOS_CLIENT
echo.
echo ========================================================================
echo  Building macOS Client: Native Apple Silicon (Metal 3 + MetalFX)
echo  Output Directory: %~dp0\MacBuild\Idontknow.app\
echo ========================================================================
echo.

if not exist %~dp0\MacBuild mkdir %~dp0\MacBuild
cd /D %~dp0\MacBuild
del build_log.txt > nul 2>&1

:: Create macOS .app Bundle structure
if not exist obj mkdir obj
if not exist Idontknow.app mkdir Idontknow.app
if not exist Idontknow.app\Contents mkdir Idontknow.app\Contents
if not exist Idontknow.app\Contents\MacOS mkdir Idontknow.app\Contents\MacOS
if not exist Idontknow.app\Contents\Resources mkdir Idontknow.app\Contents\Resources
if not exist Idontknow.app\Contents\Resources\Contents mkdir Idontknow.app\Contents\Resources\Contents
if exist Idontknow.app\Contents\Resources\shaders rmdir /S /Q Idontknow.app\Contents\Resources\shaders > nul 2>&1
if exist Idontknow.app\Contents\Resources\*.pak del /Q Idontknow.app\Contents\Resources\*.pak > nul 2>&1

:: Locate Zig Toolchain
set ZIG_EXE=..\ThirdParty\zig\zig-windows-x86_64-0.13.0\zig.exe
if not exist %ZIG_EXE% set ZIG_EXE=..\ThirdParty\zig\zig.exe

if not exist %ZIG_EXE% (
    echo [Notice] Downloading lightweight Zig cross-compiler to ThirdParty\zig...
    if not exist ..\ThirdParty\zig mkdir ..\ThirdParty\zig
    curl.exe -L -o "..\ThirdParty\zig\zig.zip" "https://zig.linus.dev/zig/zig-windows-x86_64-0.13.0.zip"
    if exist ..\ThirdParty\zig\zig.zip (
        tar.exe -xf ..\ThirdParty\zig\zig.zip -C ..\ThirdParty\zig\
        del /Q ..\ThirdParty\zig\zig.zip >nul 2>&1
        set ZIG_EXE=..\ThirdParty\zig\zig-windows-x86_64-0.13.0\zig.exe
    )
)

if not exist %ZIG_EXE% (
    echo [ERROR] Zig cross-compiler not found at %ZIG_EXE%!
    cd /D %~dp0
    exit /b 1
)

:: Locate macOS SDK
set SDK_PATH=..\tools\macos-sdk\MacOSX14.0.sdk
if not exist %SDK_PATH% set SDK_PATH=tools\macos-sdk\MacOSX14.0.sdk
if not exist %SDK_PATH% (
    echo [ERROR] Apple macOS SDK not found at %SDK_PATH%!
    cd /D %~dp0
    exit /b 1
)

:: Copy Info.plist and generate PkgInfo
if exist ..\src\Platform\macOS\Info.plist (
    copy /Y ..\src\Platform\macOS\Info.plist Idontknow.app\Contents\Info.plist > nul
)
powershell -Command "[System.IO.File]::WriteAllBytes('Idontknow.app\Contents\PkgInfo', [System.Text.Encoding]::ASCII.GetBytes('APPL????'))"

:: Stage MSL Shaders for packing into PAK archives
if not exist Contents mkdir Contents
if not exist Contents\shaders mkdir Contents\shaders
copy /Y ..\shaders\MSL\*.metal Contents\shaders\ > nul

:: Pack all assets (MSL Shaders + Models + Textures + Videos + UI + Config) into 10 PAK archives
echo Packing engine content (MSL Shaders + Assets) into PAK archives...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Content\pack_all_content.ps1" -SourceContent "%IDK_CONTENT_SOURCE%"
if errorlevel 1 (
    echo Content packing FAILED!
    cd /D %~dp0
    exit /b 1
)

:: Copy PAK files into macOS App Bundle (Contents\Resources\Contents\)
echo Copying PAK archives to Idontknow.app\Contents\Resources\Contents\...
if exist Contents\*.pak (
    copy /Y Contents\*.pak Idontknow.app\Contents\Resources\Contents\ > nul
)

:: Clean up temporary loose staging files and directories
if exist Contents rmdir /S /Q Contents > nul 2>&1
if exist Idontknow.app\Contents\Resources\shaders rmdir /S /Q Idontknow.app\Contents\Resources\shaders > nul 2>&1
if exist Idontknow.app\Contents\Resources\*.pak del /Q Idontknow.app\Contents\Resources\*.pak > nul 2>&1

:: Step 1: Compile ufbx.c with Zig C compiler
echo [1/2] Compiling ufbx FBX runtime...
%ZIG_EXE% cc -target aarch64-macos.13.0-none -isysroot %SDK_PATH% -O2 -c ..\ThirdParty\EngineDependencies\ufbx.c -o obj\ufbx.o
if errorlevel 1 (
    echo ERROR: Compiling ufbx.c failed!
    cd /D %~dp0
    exit /b 1
)

:: Step 2: Compile & Link macOS Apple Silicon binary with Zig C++ compiler (Release, Clean Game without ImGui)
%ZIG_EXE% cc -target aarch64-macos.13.0-none -isysroot %SDK_PATH% -O2 -c ..\ThirdParty\EngineDependencies\lua54\lua-5.4.7\src\onelua.c -o obj\lua.o
if errorlevel 1 exit /b 1
echo [2/2] Compiling and Linking native Apple Silicon Mach-O binary (Release - Metal + MetalFX)...

set MACOS_SOURCES=..\src\Core\main.cpp ..\src\Core\Application.cpp ..\src\Core\Window.cpp ..\src\Upscaling\XeSS\XeSSSystem.cpp ..\src\Storage\DirectStorage\DirectStorageSystem.cpp ..\src\Platform\PlatformUtils.cpp ..\src\Storage\PakCompression.cpp ..\src\Renderer\ModelLoader.cpp ..\src\Renderer\Camera.cpp ..\src\Renderer\CameraSystem.cpp ..\src\Core\Timer.cpp ..\src\Renderer\TextureStreamer.cpp ..\src\Renderer\Texture.cpp ..\src\Sequencer\Sequencer.cpp ..\src\Security\SecuritySystem.cpp ..\src\Security\MBAObfuscator.cpp ..\src\Security\VirtualMachine.cpp ..\src\Security\HardwareID.cpp ..\src\Security\LicenseSystem.cpp ..\src\Rain\RainSystem.cpp ..\src\Rain\WeatherSystem.cpp ..\src\Physics\PhysicsSystem.cpp ..\src\Physics\ClothSystem.cpp ..\src\Physics\HairSystem.cpp ..\src\Physics\CharacterController.cpp ..\src\UI\WebMenuSystem.cpp ..\src\UI\ContentBrowser.cpp ..\src\Level\LevelSystem.cpp ..\src\Storage\SaveSystem.cpp ..\src\Steam\SteamSystem.cpp ..\src\Epic\EpicSystem.cpp ..\src\Discord\DiscordSystem.cpp ..\src\Animation\SkinnedMesh.cpp ..\src\Animation\CharacterAsset.cpp ..\src\Animation\AnimationSystem.cpp ..\src\Animation\BoneClassifierAI.cpp ..\src\Animation\BoneMappingWindow.cpp ..\src\Animation\Ozz\OzzRuntime.cpp ..\src\Animation\Ozz\OzzSerializer.cpp ..\src\Animation\Ozz\OzzImporter.cpp ..\src\Animation\MotionMatching\MotionDatabase.cpp ..\src\Animation\MotionMatching\MotionTrajectory.cpp ..\src\Animation\MotionMatching\MotionMatcher.cpp ..\src\Animation\MotionMatching\MotionController.cpp ..\src\Input\DualSense.cpp ..\src\Input\DualShock.cpp ..\src\Audio\DualSenseAudio.cpp ..\src\Audio\AudioSystem.cpp ..\src\Networking\BitWriter.cpp ..\src\Networking\BitReader.cpp ..\src\Networking\Packet.cpp ..\src\Networking\NetworkSocket.cpp ..\src\Networking\NetworkConnection.cpp ..\src\Networking\NetworkObject.cpp ..\src\Networking\NetworkReplication.cpp ..\src\Networking\ServerAntiCheat.cpp ..\src\Networking\ClientAntiCheat.cpp ..\src\Networking\NetworkServer.cpp ..\src\Networking\NetworkClient.cpp ..\src\Networking\MultiplayerManager.cpp ..\src\Scene\SceneNode.cpp ..\src\Scene\SceneGraph.cpp ..\src\Script\ScriptSystem.cpp ..\src\AI\NavMesh.cpp ..\src\AI\NavAgent.cpp ..\src\AI\NavigationSystem.cpp ..\src\VFX\ParticleEmitter.cpp ..\src\VFX\ParticleSystem.cpp ..\src\Terrain\TerrainSystem.cpp ..\src\PostProcess\PostProcessSystem.cpp ..\src\Asset\AssetRegistry.cpp ..\src\Prefab\Prefab.cpp ..\src\Prefab\PrefabSystem.cpp ..\src\Debug\Profiler.cpp ..\src\Debug\DebugConsole.cpp ..\src\Input\InputMapping.cpp ..\src\Platform\macOS\MacWindow.mm ..\src\Renderer\Metal\Renderer_macOS.mm ..\src\Renderer\Metal\MetalRendererBackend.mm ..\src\Renderer\Metal\MetalDevice.mm ..\src\Renderer\Metal\MetalBuffer.mm ..\src\Renderer\Metal\MetalTexture.mm ..\src\Renderer\Metal\MetalPipeline.mm ..\src\Upscaling\MetalFX\MetalFXSystem.mm ..\src\RayTracing\Metal\MetalRayTracing.mm obj\ufbx.o obj\lua.o

%ZIG_EXE% c++ -target aarch64-macos.13.0-none -isysroot %SDK_PATH% -isystem %SDK_PATH%\usr\include -F %SDK_PATH%\System\Library\Frameworks -L %SDK_PATH%\usr\lib -framework Cocoa -framework Metal -framework MetalKit -framework MetalFX -framework QuartzCore -framework Foundation -framework CoreGraphics -framework IOKit -framework GameController -framework AudioToolbox -lobjc -fobjc-arc -Wno-deprecated-declarations -Wno-availability -Wno-elaborated-enum-base -O2 -std=c++20 -DMYENGINE_MACOS=1 -D_MYENGINE_MACOS=1 -DENGINE_RELEASE=1 -I..\src -I..\src\Core -I..\src\Renderer -I..\src\Input -I..\src\Audio -I..\src\Storage -I..\src\Sequencer -I..\ThirdParty\EngineDependencies -I..\src\Platform\WindowsCompat -I..\src\imgui -I..\src\Platform\DirectXMath -I..\ThirdParty\PhysX\physx\include -I..\ThirdParty\steamworks_sdk_165\sdk\public -I..\ThirdParty\discord_social_sdk\include %MACOS_SOURCES% -o Idontknow.app\Contents\MacOS\Idontknow > build_log.txt 2>&1

if errorlevel 1 (
    echo macOS Client Compilation FAILED!
    type build_log.txt
    cd /D %~dp0
    exit /b 1
)

if not exist Idontknow.app\Contents\MacOS\Idontknow (
    echo ERROR: Mach-O binary was not produced!
    cd /D %~dp0
    exit /b 1
)

:: Clean up temporary build artifacts (build_log, obj, intermediate files)
del /Q build_log.txt > nul 2>&1
if exist obj rmdir /S /Q obj > nul 2>&1
if exist *.o del /Q *.o > nul 2>&1
if exist *.obj del /Q *.obj > nul 2>&1

echo.
echo ========================================================================
echo  macOS Client (Apple Silicon) Build SUCCESSFUL!
echo  App Bundle: %~dp0\MacBuild\Idontknow.app
echo  Binary:     %~dp0\MacBuild\Idontknow.app\Contents\MacOS\Idontknow [Native Mach-O ARM64]
echo  Packages:   %~dp0\MacBuild\Idontknow.app\Contents\Resources\Contents\ (10 PAK archives)
echo.
echo  Ready to transfer:
echo    Copy the 'Idontknow.app' bundle to your Mac!
echo    On macOS Terminal, run this once to grant execution rights and clear Gatekeeper:
echo      chmod +x Idontknow.app/Contents/MacOS/Idontknow ^&^& xattr -cr Idontknow.app ^&^& codesign --force --deep --sign - Idontknow.app
echo    Then run:
echo      open Idontknow.app
echo      (or ./Idontknow.app/Contents/MacOS/Idontknow)
echo ========================================================================
cd /D %~dp0
exit /b 0

:: -----------------------------------------------------------------------------
:: PLAYSTATION 4 CLIENT (OPENORBIS TOOLCHAIN) BUILD PROCEDURE
:: -----------------------------------------------------------------------------
:SET_PS4_CLIENT
:BUILD_PS4_CLIENT
echo.
echo ========================================================================
echo  Building PlayStation 4 Client: OpenOrbis PS4 Toolchain
echo  Output Directory: %~dp0\PS4Build\
echo ========================================================================
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Build\build_ps4.ps1" %*
if errorlevel 1 (
    echo [ERROR] PlayStation 4 build failed!
    cd /D %~dp0
    exit /b 1
)
cd /D %~dp0
exit /b 0

:BUILD_DEFAULT_PROJECT_STEAM
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Build\BuildProject.ps1" -ProjectFile "%~dp0Projects\Test\Test.idkp" -Platform steam
exit /b %errorlevel%
:BUILD_DEFAULT_PROJECT_EPIC
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Build\BuildProject.ps1" -ProjectFile "%~dp0Projects\Test\Test.idkp" -Platform epic
exit /b %errorlevel%
