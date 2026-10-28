if(NOT EXISTS "${CMAKE_SOURCE_DIR}/ThirdParty/MsQuic-2.6.1-OpenSSL/build/native/include/msquic.h")
    message(FATAL_ERROR "Run tools/Dependencies/FetchMsQuic.ps1 before configuring the Windows client")
endif()
# Windows client dependencies match the supported build.bat path.
include("${CMAKE_CURRENT_LIST_DIR}/ClientSources.cmake")
set(WINDOWS_CLIENT_SOURCES ${CLIENT_COMMON_SOURCES} src/Renderer/Renderer.cpp)
file(GLOB IMGUI_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/imgui/*.cpp")
add_executable(Idontknow WIN32 ${WINDOWS_CLIENT_SOURCES} ${IMGUI_SOURCES})
option(MYENGINE_EDITOR "Build editor features" ON)
if(MYENGINE_EDITOR)
    target_compile_definitions(Idontknow PRIVATE ENGINE_EDITOR=1)
else()
    target_compile_definitions(Idontknow PRIVATE ENGINE_RELEASE=1)
endif()
option(MYENGINE_ENABLE_STEAM "Enable Steamworks integration" OFF)
if(MYENGINE_ENABLE_STEAM)
    target_compile_definitions(Idontknow PRIVATE ENGINE_PLATFORM_STEAM=1)
    set(MYENGINE_PACKAGE_PLATFORM STEAM)
    target_link_libraries(Idontknow PRIVATE "${CMAKE_SOURCE_DIR}/ThirdParty/steamworks_sdk_165/sdk/redistributable_bin/win64/steam_api64.lib")
    target_link_options(Idontknow PRIVATE /DELAYLOAD:steam_api64.dll)
else()
    target_compile_definitions(Idontknow PRIVATE ENGINE_PLATFORM_STANDALONE=1)
    set(MYENGINE_PACKAGE_PLATFORM STANDALONE)
endif()
target_compile_definitions(Idontknow PRIVATE WIN32_LEAN_AND_MEAN UNICODE _UNICODE _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS)
target_compile_options(Idontknow PRIVATE /bigobj /EHsc /utf-8 /W3 /MP4)
target_include_directories(Idontknow PRIVATE
    "${CMAKE_SOURCE_DIR}/ThirdParty/AgilitySDK-1.619.6/build/native/include"
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/src/Core"
    "${CMAKE_SOURCE_DIR}/src/Renderer"
    "${CMAKE_SOURCE_DIR}/src/Input"
    "${CMAKE_SOURCE_DIR}/src/Audio"
    "${CMAKE_SOURCE_DIR}/src/Storage"
    "${CMAKE_SOURCE_DIR}/src/Sequencer"
    "${CMAKE_SOURCE_DIR}/ThirdParty/EngineDependencies"
    "${CMAKE_SOURCE_DIR}/ThirdParty/EngineDependencies/lua54/lua-5.4.7/src"
    "${CMAKE_SOURCE_DIR}/src/imgui"
    "${CMAKE_SOURCE_DIR}/ThirdParty/streamline-sdk-v2.14.1/include"
    "${CMAKE_SOURCE_DIR}/ThirdParty/DirectX-Headers/include"
    "${CMAKE_SOURCE_DIR}/ThirdParty/FidelityFX-SDK-v1.1.4/sdk/include/FidelityFX"
    "${CMAKE_SOURCE_DIR}/ThirdParty/FidelityFX-SDK-v1.1.4/sdk/include/FidelityFX/gpu"
    "${CMAKE_SOURCE_DIR}/ThirdParty/XeSS_SDK_3.0.2/inc"
    "${CMAKE_SOURCE_DIR}/ThirdParty/XeSS_SDK_3.0.2/inc/xess"
    "${CMAKE_SOURCE_DIR}/ThirdParty/XeSS_SDK_3.0.2/inc/xess_fg"
    "${CMAKE_SOURCE_DIR}/ThirdParty/XeSS_SDK_3.0.2/inc/xell"
    "${CMAKE_SOURCE_DIR}/ThirdParty/DirectStorage/sdk-package/native/include"
    "${CMAKE_SOURCE_DIR}/ThirdParty/PhysX/physx/include"
    "${CMAKE_SOURCE_DIR}/ThirdParty/WebView2/build/native/include"
    "${CMAKE_SOURCE_DIR}/ThirdParty/discord_social_sdk/include"
)
add_custom_command(TARGET Idontknow POST_BUILD
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File
        "${CMAKE_SOURCE_DIR}/cmake/PackageWindows.ps1" -OutputDirectory "$<TARGET_FILE_DIR:Idontknow>" -Platform "${MYENGINE_PACKAGE_PLATFORM}"
    VERBATIM)
target_link_libraries(Idontknow PRIVATE
    "d3d12.lib"
    "dxgi.lib"
    "user32.lib"
    "comdlg32.lib"
    "d3dcompiler.lib"
    "dxcompiler.lib"
    "bcrypt.lib"
    "wintrust.lib"
    "crypt32.lib"
    "hid.lib"
    "setupapi.lib"
    "cfgmgr32.lib"
    "xinput.lib"
    "ole32.lib"
    "advapi32.lib"
    "ws2_32.lib"
    "shlwapi.lib"
    "iphlpapi.lib"
    "${CMAKE_SOURCE_DIR}/ThirdParty/WebView2/build/native/x64/WebView2LoaderStatic.lib"
    "${CMAKE_SOURCE_DIR}/ThirdParty/streamline-sdk-v2.14.1/lib/x64/sl.interposer.lib"
    "${CMAKE_SOURCE_DIR}/ThirdParty/XeSS_SDK_3.0.2/lib/libxess.lib"
    "${CMAKE_SOURCE_DIR}/ThirdParty/XeSS_SDK_3.0.2/lib/libxess_fg.lib"
    "${CMAKE_SOURCE_DIR}/ThirdParty/XeSS_SDK_3.0.2/lib/libxell.lib"
    "${CMAKE_SOURCE_DIR}/ThirdParty/XeSS_SDK_3.0.2/lib/libxess_dx11.lib"
    "${CMAKE_SOURCE_DIR}/ThirdParty/DirectStorage/sdk-package/native/lib/x64/dstorage.lib"
    "${CMAKE_SOURCE_DIR}/ThirdParty/PhysX/Lib/bin/win.x86_64.vc143.md/release/PhysX.lib"
    "${CMAKE_SOURCE_DIR}/ThirdParty/PhysX/Lib/bin/win.x86_64.vc143.md/release/PhysXCommon.lib"
    "${CMAKE_SOURCE_DIR}/ThirdParty/PhysX/Lib/bin/win.x86_64.vc143.md/release/PhysXExtensions_static.lib"
    "${CMAKE_SOURCE_DIR}/ThirdParty/PhysX/Lib/bin/win.x86_64.vc143.md/release/PhysXVehicle_static.lib"
    "${CMAKE_SOURCE_DIR}/ThirdParty/PhysX/Lib/bin/win.x86_64.vc143.md/release/PhysXFoundation.lib"
    "${CMAKE_SOURCE_DIR}/ThirdParty/PhysX/Lib/bin/win.x86_64.vc143.md/release/PhysXPvdSDK_static.lib"
    "${CMAKE_SOURCE_DIR}/ThirdParty/PhysX/Lib/bin/win.x86_64.vc143.md/release/PhysXCharacterKinematic_static.lib"
    "${CMAKE_SOURCE_DIR}/ThirdParty/discord_social_sdk/lib/release/discord_partner_sdk.lib"
    "delayimp.lib"
)
target_link_options(Idontknow PRIVATE /MANIFEST:EMBED "/MANIFESTINPUT:${CMAKE_SOURCE_DIR}/src/Core/app.manifest" /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT /IGNORE:4199
    /DELAYLOAD:sl.interposer.dll
    /DELAYLOAD:libxess.dll
    /DELAYLOAD:libxess_fg.dll
    /DELAYLOAD:libxell.dll
    /DELAYLOAD:libxess_dx11.dll
    /DELAYLOAD:dstorage.dll
    /DELAYLOAD:dstoragecore.dll
    /DELAYLOAD:dxcompiler.dll
    /DELAYLOAD:PhysX.dll
    /DELAYLOAD:PhysXCommon.dll
    /DELAYLOAD:PhysXFoundation.dll
    /DELAYLOAD:discord_partner_sdk.dll
)
