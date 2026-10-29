cmake_minimum_required(VERSION 3.20)
project(Idontknow VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Generate the local AES key header before any client/security target compiles.
if(WIN32)
    execute_process(COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/tools/Security/InitializeContentKey.ps1" RESULT_VARIABLE IDK_KEY_RESULT)
    if(NOT IDK_KEY_RESULT EQUAL 0)
        message(FATAL_ERROR "Cannot load local content encryption key")
    endif()
endif()

# Networking common sources
set(NETWORKING_SOURCES
    src/Networking/NetworkTypes.h
    src/Networking/BitWriter.h
    src/Networking/BitWriter.cpp
    src/Networking/BitReader.h
    src/Networking/BitReader.cpp
    src/Networking/Packet.h
    src/Networking/Packet.cpp
    src/Networking/NetworkSocket.h
    src/Networking/NetworkSocket.cpp
    src/Networking/NetworkConnection.h
    src/Networking/NetworkConnection.cpp
    src/Networking/NetworkObject.h
    src/Networking/NetworkObject.cpp
    src/Networking/NetworkReplication.h
    src/Networking/NetworkReplication.cpp
    src/Networking/ServerAntiCheat.cpp
    src/Networking/ClientAntiCheat.cpp
    src/Networking/NetworkServer.h
    src/Networking/NetworkServer.cpp
    src/Networking/NetworkClient.h
    src/Networking/NetworkClient.cpp
    src/Networking/MultiplayerManager.h
    src/Networking/MultiplayerManager.cpp
)

# Core game client source files
set(CLIENT_SOURCES
    src/Core/main.cpp
    src/Core/ProjectBuilder.cpp
    src/Core/Application.h
    src/Core/Application.cpp
    src/Core/Window.h
    src/Core/Window.cpp
    src/Renderer/Renderer.h
    src/Renderer/Renderer.cpp
    src/Renderer/Camera.h
    src/Renderer/Camera.cpp
    src/Core/Timer.h
    src/Core/Timer.cpp
    src/Physics/PhysicsSystem.h
    src/Physics/PhysicsSystem.cpp
    src/Physics/ClothSystem.h
    src/Physics/ClothSystem.cpp
    src/Physics/HairSystem.h
    src/Physics/HairSystem.cpp
    src/UI/WebMenuSystem.h
    src/UI/WebMenuSystem.cpp
    src/Storage/SaveSystem.h
    src/Storage/SaveSystem.cpp
    src/Steam/SteamSystem.h
    src/Steam/SteamSystem.cpp
    src/Input/DualSense.h
    src/Input/DualSense.cpp
    src/Input/DualShock.h
    src/Input/DualShock.cpp
    src/Audio/DualSenseAudio.h
    src/Audio/DualSenseAudio.cpp
    src/Upscaling/XeSS/XeSSSystem.h
    src/Upscaling/XeSS/XeSSSystem.cpp
    src/Storage/DirectStorage/DirectStorageSystem.h
    src/Storage/DirectStorage/DirectStorageSystem.cpp
    ${NETWORKING_SOURCES}
)

set(MYENGINE_CLIENT_DEFAULT OFF)
if(WIN32 OR APPLE OR PS4 OR SONY_PS4 OR ORBIS)
    set(MYENGINE_CLIENT_DEFAULT ON)
endif()
option(MYENGINE_BUILD_CLIENT "Build the graphical client" ${MYENGINE_CLIENT_DEFAULT})

if(MYENGINE_BUILD_CLIENT)
if (APPLE)
    enable_language(OBJC OBJCXX)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Target architectures" FORCE)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD 20)
    
    add_compile_options(
        -Wall
        -Wno-unknown-pragmas
        -Wno-unused-variable
        $<$<COMPILE_LANGUAGE:OBJC,OBJCXX>:-fobjc-arc>
    )

    set(MACOS_CLIENT_SOURCES
        src/Core/main.cpp
    src/Core/ProjectBuilder.cpp
        src/Core/Application.h
        src/Core/Application.cpp
        src/Core/Window.h
        src/Core/Window.cpp
        src/Platform/macOS/MacWindow.h
        src/Platform/macOS/MacWindow.mm
        src/Platform/PlatformUtils.h
        src/Platform/PlatformUtils.cpp
        src/Renderer/Renderer.h
        src/Renderer/Metal/Renderer_macOS.mm
        src/Renderer/Metal/MetalDevice.h
        src/Renderer/Metal/MetalDevice.mm
        src/Renderer/Metal/MetalBuffer.h
        src/Renderer/Metal/MetalBuffer.mm
        src/Renderer/Metal/MetalTexture.h
        src/Renderer/Metal/MetalTexture.mm
        src/Renderer/Metal/MetalPipeline.h
        src/Renderer/Metal/MetalPipeline.mm
        src/Renderer/Metal/MetalRendererBackend.h
        src/Renderer/Metal/MetalRendererBackend.mm
        src/Upscaling/MetalFX/MetalFXSystem.h
        src/Upscaling/MetalFX/MetalFXSystem.mm
        src/RayTracing/Metal/MetalRayTracing.h
        src/RayTracing/Metal/MetalRayTracing.mm
        src/imgui/imgui.cpp
        src/imgui/imgui_draw.cpp
        src/imgui/imgui_tables.cpp
        src/imgui/imgui_widgets.cpp
        src/imgui/imgui_demo.cpp
        src/imgui/ImGuizmo.cpp
        src/imgui/imgui_impl_osx.mm
        src/imgui/imgui_impl_metal.mm
        src/Renderer/Camera.h
        src/Renderer/Camera.cpp
        src/Core/Timer.h
        src/Core/Timer.cpp
        src/Renderer/Texture.h
        src/Renderer/Texture.cpp
        src/Renderer/TextureStreamer.h
        src/Renderer/TextureStreamer.cpp
        src/Physics/PhysicsSystem.h
        src/Physics/PhysicsSystem.cpp
        src/Physics/ClothSystem.h
        src/Physics/ClothSystem.cpp
        src/Physics/HairSystem.h
        src/Physics/HairSystem.cpp
        src/Rain/RainSystem.h
        src/Rain/RainSystem.cpp
        src/Rain/WeatherSystem.h
        src/Rain/WeatherSystem.cpp
        src/Animation/SkinnedMesh.h
        src/Animation/SkinnedMesh.cpp
        src/Storage/SaveSystem.h
        src/Storage/SaveSystem.cpp
        src/Input/DualSense.h
        src/Input/DualSense.cpp
        src/Input/DualShock.h
        src/Input/DualShock.cpp
        src/Audio/DualSenseAudio.h
        src/Audio/DualSenseAudio.cpp
        ${NETWORKING_SOURCES}
    )

    include(cmake/ClientSources.cmake)
    list(APPEND MACOS_CLIENT_SOURCES ${CLIENT_COMMON_SOURCES})
    list(REMOVE_DUPLICATES MACOS_CLIENT_SOURCES)
    add_executable(Idontknow MACOSX_BUNDLE ${MACOS_CLIENT_SOURCES})
    target_compile_definitions(Idontknow PRIVATE ENGINE_RELEASE=1 MYENGINE_MACOS=1 _MYENGINE_MACOS=1)
    target_compile_options(Idontknow PRIVATE -Wno-elaborated-enum-base)
    target_include_directories(Idontknow PRIVATE src/Core src/Renderer src/Input src/Audio src/Storage src/Sequencer ThirdParty/Libraries/EngineDependencies ThirdParty/Libraries/EngineDependencies/lua54/lua-5.4.7/src src/Platform/WindowsCompat ThirdParty/Discord/discord_social_sdk/include)

    find_library(METAL_FRAMEWORK Metal REQUIRED)
    find_library(METALTKIT_FRAMEWORK MetalKit REQUIRED)
    find_library(METALFX_FRAMEWORK MetalFX REQUIRED)
    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
    find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
    find_library(IOKIT_FRAMEWORK IOKit REQUIRED)
    find_library(GAMECONTROLLER_FRAMEWORK GameController REQUIRED)

    target_link_libraries(Idontknow PRIVATE
        ${METAL_FRAMEWORK}
        ${METALTKIT_FRAMEWORK}
        ${METALFX_FRAMEWORK}
        ${FOUNDATION_FRAMEWORK}
        ${COCOA_FRAMEWORK}
        ${QUARTZCORE_FRAMEWORK}
        ${COREGRAPHICS_FRAMEWORK}
        ${IOKIT_FRAMEWORK}
        ${GAMECONTROLLER_FRAMEWORK}
    )

    target_include_directories(Idontknow PRIVATE
        "${CMAKE_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_SOURCE_DIR}/src/imgui"
        "${CMAKE_SOURCE_DIR}/src/Platform"
        "${CMAKE_SOURCE_DIR}/src/Platform/DirectXMath"
        "${CMAKE_SOURCE_DIR}/ThirdParty/NVIDIA/PhysX/physx/include"
        "${CMAKE_SOURCE_DIR}/ThirdParty/Valve/steamworks_sdk_165/sdk/public"
    )

    set_target_properties(Idontknow PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "Idontknow"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.idontknow.client"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0.0"
        MACOSX_BUNDLE_BUNDLE_VERSION "1"
    )

elseif (PS4 OR SONY_PS4 OR ORBIS)
    add_compile_definitions(
        ENGINE_PLATFORM_PS4=1
        __PS4__=1
        __ORBIS__=1
        ENGINE_RELEASE=1
    )

    set(PS4_CLIENT_SOURCES
        src/Core/main.cpp
    src/Core/ProjectBuilder.cpp
        src/Core/Application.cpp
        src/Core/Window.cpp
        src/Core/Timer.cpp
        src/Platform/PS4/PS4Window.cpp
        src/Platform/PlatformUtils.cpp
        src/Renderer/PS4/PS4Device.cpp
        src/Renderer/PS4/PS4RendererBackend.cpp
        src/Renderer/PS4/Renderer_PS4.cpp
        src/Renderer/Camera.cpp
        src/Renderer/CameraSystem.cpp
        src/Renderer/Texture.cpp
        src/Renderer/TextureStreamer.cpp
        src/Renderer/ModelLoader.cpp
        src/Audio/PS4Audio.cpp
        src/Audio/DualSenseAudio.cpp
        src/Audio/AudioSystem.cpp
        src/Input/DualSense.cpp
        src/Input/DualShock.cpp
        src/Input/InputMapping.cpp
        src/Debug/Profiler.cpp
        src/Debug/DebugConsole.cpp
        src/Storage/SaveSystem.cpp
        src/Storage/PakCompression.cpp
        src/Physics/PhysicsSystem.cpp
        src/Physics/CharacterController.cpp
        src/Physics/ClothSystem.cpp
        src/Physics/HairSystem.cpp
        src/UI/WebMenuSystem.cpp
        src/Steam/SteamSystem.cpp
        src/Epic/EpicSystem.cpp
        src/Discord/DiscordSystem.cpp
        src/Security/SecuritySystem.cpp
        src/Security/MBAObfuscator.cpp
        src/Security/VirtualMachine.cpp
        src/Security/HardwareID.cpp
        src/Security/LicenseSystem.cpp
        src/Level/LevelSystem.cpp
        src/Prefab/Prefab.cpp
        src/Prefab/PrefabSystem.cpp
        src/Asset/AssetRegistry.cpp
        src/AI/NavigationSystem.cpp
        src/AI/NavMesh.cpp
        src/AI/NavAgent.cpp
        src/Sequencer/Sequencer.cpp
        src/Rain/RainSystem.cpp
        src/Rain/WeatherSystem.cpp
        src/PostProcess/PostProcessSystem.cpp
        src/Scene/SceneNode.cpp
        src/Scene/SceneGraph.cpp
        src/Script/ScriptSystem.cpp
        ThirdParty/Libraries/EngineDependencies/lua54/lua-5.4.7/src/onelua.c
        ThirdParty/Libraries/EngineDependencies/ufbx.c
        ${NETWORKING_SOURCES}
    )

    add_executable(Idontknow ${PS4_CLIENT_SOURCES})

    if (NOT DEFINED OO_PS4_TOOLCHAIN)
        if (DEFINED ENV{OO_PS4_TOOLCHAIN})
            set(OO_PS4_TOOLCHAIN "$ENV{OO_PS4_TOOLCHAIN}")
        else()
            set(OO_PS4_TOOLCHAIN "${CMAKE_SOURCE_DIR}/ThirdParty/OpenSource/OpenOrbis-PS4-Toolchain")
        endif()
    endif()

    target_include_directories(Idontknow PRIVATE
        "${CMAKE_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_SOURCE_DIR}/src/Core"
        "${CMAKE_SOURCE_DIR}/src/Renderer"
        "${CMAKE_SOURCE_DIR}/src/Input"
        "${CMAKE_SOURCE_DIR}/src/Audio"
        "${CMAKE_SOURCE_DIR}/src/Storage"
        "${CMAKE_SOURCE_DIR}/src/Sequencer"
        "${CMAKE_SOURCE_DIR}/ThirdParty/Libraries/EngineDependencies"
        "${CMAKE_SOURCE_DIR}/src/Platform"
        "${CMAKE_SOURCE_DIR}/src/Platform/DirectXMath"
        "${CMAKE_SOURCE_DIR}/src/Platform/WindowsCompat"
        "${OO_PS4_TOOLCHAIN}/include"
        "${CMAKE_SOURCE_DIR}/ThirdParty/NVIDIA/PhysX/physx/include"
        "${CMAKE_SOURCE_DIR}/ThirdParty/Valve/steamworks_sdk_165/sdk/public"
        "${CMAKE_SOURCE_DIR}/ThirdParty/Discord/discord_social_sdk/include"
    )

    target_link_directories(Idontknow PRIVATE "${OO_PS4_TOOLCHAIN}/lib")

    target_link_libraries(Idontknow PRIVATE
        kernel
        c
        SceVideoOut
        ScePad
        SceAudioOut
        SceUserService
        SceSysmodule
    )

    target_link_options(Idontknow PRIVATE
        "-pie"
        "-Wl,--script=${OO_PS4_TOOLCHAIN}/link.x"
    )

elseif(WIN32)
    include(cmake/WindowsClient.cmake)
endif()
endif()

# Headless server is available independently of the graphical client.
if(NOT (PS4 OR SONY_PS4 OR ORBIS))
    find_package(Threads REQUIRED)
    add_executable(IdontknowServer
        src/Server/ServerMain.cpp src/Server/DedicatedServer.cpp
        src/Level/LevelSystem.cpp src/Core/Timer.cpp
        src/Networking/BitWriter.cpp src/Networking/BitReader.cpp
        src/Networking/Packet.cpp src/Networking/NetworkSocket.cpp
        src/Networking/NetworkConnection.cpp src/Networking/NetworkObject.cpp
        src/Networking/NetworkReplication.cpp src/Networking/ServerAntiCheat.cpp
        src/Networking/NetworkServer.cpp)
    target_compile_definitions(IdontknowServer PRIVATE ENGINE_SERVER=1)
    target_include_directories(IdontknowServer PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(IdontknowServer PRIVATE Threads::Threads)
    if(WIN32)
        target_link_libraries(IdontknowServer PRIVATE ws2_32 advapi32 bcrypt)
        if(NOT EXISTS "${CMAKE_SOURCE_DIR}/ThirdParty/Microsoft/MsQuic-2.6.3-OpenSSL/build/native/include/msquic.h")
            message(FATAL_ERROR "Run tools/Dependencies/FetchMsQuic.ps1 before configuring the Windows server")
        endif()
        add_custom_command(TARGET IdontknowServer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:IdontknowServer>/ThirdParty/Microsoft/MsQuic"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/ThirdParty/Microsoft/MsQuic-2.6.3-OpenSSL/build/native/bin/x64/msquic.dll" "$<TARGET_FILE_DIR:IdontknowServer>/ThirdParty/Microsoft/MsQuic/msquic.dll")
        target_compile_options(IdontknowServer PRIVATE /utf-8 /EHsc)
    endif()
endif()

include(CTest)
if(BUILD_TESTING AND TARGET IdontknowServer)
    get_target_property(REGRESSION_NETWORK_SOURCES IdontknowServer SOURCES)
    list(FILTER REGRESSION_NETWORK_SOURCES INCLUDE REGEX "src/Networking/")
    add_executable(NetworkRegression tests/NetworkRegression.cpp ${REGRESSION_NETWORK_SOURCES}
        src/Networking/NetworkClient.cpp src/Networking/ClientAntiCheat.cpp)
    target_include_directories(NetworkRegression PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_compile_definitions(NetworkRegression PRIVATE ENGINE_SERVER=1 NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(NetworkRegression PRIVATE Threads::Threads)
    if(WIN32)
        add_executable(VehicleRoadAIRegression tests/VehicleRoadAIRegression.cpp)
        target_include_directories(VehicleRoadAIRegression PRIVATE "${CMAKE_SOURCE_DIR}/src")
        target_compile_options(VehicleRoadAIRegression PRIVATE /utf-8 /EHsc)
        add_test(NAME vehicle_road_ai_regression COMMAND VehicleRoadAIRegression)
        add_executable(NavAgentRegression tests/NavAgentRegression.cpp src/AI/NavAgent.cpp src/AI/NavMesh.cpp)
        target_include_directories(NavAgentRegression PRIVATE "${CMAKE_SOURCE_DIR}/src")
        target_compile_options(NavAgentRegression PRIVATE /utf-8 /EHsc)
        add_test(NAME nav_agent_regression COMMAND NavAgentRegression)
        add_executable(ClientIntegrityRegression tests/ClientIntegrityRegression.cpp
            src/Security/SecuritySystem.cpp src/Security/MBAObfuscator.cpp
            src/Security/VirtualMachine.cpp src/Security/HardwareID.cpp
            src/Security/LicenseSystem.cpp src/Storage/PakCompression.cpp)
        target_include_directories(ClientIntegrityRegression PRIVATE "${CMAKE_SOURCE_DIR}/src")
        target_compile_definitions(ClientIntegrityRegression PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        target_compile_options(ClientIntegrityRegression PRIVATE /utf-8 /EHsc)
        target_link_libraries(ClientIntegrityRegression PRIVATE user32)
        add_test(NAME client_integrity_regression COMMAND ClientIntegrityRegression)
        target_link_libraries(NetworkRegression PRIVATE ws2_32 bcrypt)
        add_executable(TlsRegression tests/TlsRegression.cpp ${REGRESSION_NETWORK_SOURCES}
            src/Networking/NetworkClient.cpp src/Networking/ClientAntiCheat.cpp)
        target_include_directories(TlsRegression PRIVATE "${CMAKE_SOURCE_DIR}/src")
        target_compile_definitions(TlsRegression PRIVATE ENGINE_SERVER=1 NOMINMAX WIN32_LEAN_AND_MEAN)
        target_compile_options(TlsRegression PRIVATE /utf-8 /EHsc)
        target_link_libraries(TlsRegression PRIVATE ws2_32 bcrypt Threads::Threads)
        add_dependencies(TlsRegression IdontknowServer)
        add_custom_command(TARGET TlsRegression POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:TlsRegression>/ThirdParty/Microsoft/MsQuic"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/ThirdParty/Microsoft/MsQuic-2.6.3-OpenSSL/build/native/bin/x64/msquic.dll" "$<TARGET_FILE_DIR:TlsRegression>/ThirdParty/Microsoft/MsQuic/msquic.dll")
        find_program(IDK_PWSH pwsh)
        if(IDK_PWSH)
            add_test(NAME tls_fixtures COMMAND ${IDK_PWSH} -NoProfile -File "${CMAKE_SOURCE_DIR}/tests/CreateTlsFixtures.ps1")
            set_tests_properties(tls_fixtures PROPERTIES FIXTURES_SETUP tls_certificates)
            add_test(NAME tls_regression COMMAND TlsRegression)
            set_tests_properties(tls_regression PROPERTIES FIXTURES_REQUIRED tls_certificates WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
        endif()
        target_compile_options(NetworkRegression PRIVATE /utf-8 /EHsc)
        add_test(NAME server_lifecycle COMMAND powershell -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/tests/ServerLifecycle.ps1" -Executable "$<TARGET_FILE:IdontknowServer>")
    endif()
    add_test(NAME network_regression COMMAND NetworkRegression)
    if(WIN32 AND TARGET Idontknow)
        add_executable(DxrTransformRegression tests/DxrTransformRegression.cpp)
        target_include_directories(DxrTransformRegression PRIVATE "${CMAKE_SOURCE_DIR}/src")
        add_test(NAME dxr_transform_regression COMMAND DxrTransformRegression)
        add_executable(MouseInputRegression tests/MouseInputRegression.cpp
            src/Input/InputMapping.cpp src/Platform/PlatformUtils.cpp)
        get_target_property(MOUSE_TEST_INCLUDE_DIRECTORIES Idontknow INCLUDE_DIRECTORIES)
        target_include_directories(MouseInputRegression PRIVATE ${MOUSE_TEST_INCLUDE_DIRECTORIES})
        add_executable(EditorCameraRegression tests/EditorCameraRegression.cpp
            src/Renderer/Camera.cpp src/Renderer/CameraSystem.cpp)
        target_include_directories(EditorCameraRegression PRIVATE ${MOUSE_TEST_INCLUDE_DIRECTORIES})
        target_compile_definitions(EditorCameraRegression PRIVATE ENGINE_EDITOR=1 ENGINE_TEST_HEADLESS=1 NOMINMAX WIN32_LEAN_AND_MEAN)
        target_compile_options(EditorCameraRegression PRIVATE /utf-8 /EHsc /Gy)
        target_link_options(EditorCameraRegression PRIVATE /OPT:REF)
        add_test(NAME editor_camera_regression COMMAND EditorCameraRegression)
        target_compile_definitions(MouseInputRegression PRIVATE ENGINE_RELEASE=1 NOMINMAX WIN32_LEAN_AND_MEAN)
        target_compile_options(MouseInputRegression PRIVATE /utf-8 /EHsc /Gy)
        target_link_options(MouseInputRegression PRIVATE /OPT:REF)
        target_link_libraries(MouseInputRegression PRIVATE user32 shell32)
        add_test(NAME mouse_input_regression COMMAND MouseInputRegression)
        add_executable(CharacterCameraRegression tests/CharacterCameraRegression.cpp
            src/Renderer/Camera.cpp src/Physics/CharacterController.cpp)
        target_include_directories(CharacterCameraRegression PRIVATE ${MOUSE_TEST_INCLUDE_DIRECTORIES})
        target_compile_definitions(CharacterCameraRegression PRIVATE ENGINE_RELEASE=1 NOMINMAX WIN32_LEAN_AND_MEAN)
        target_compile_options(CharacterCameraRegression PRIVATE /utf-8 /EHsc /Gy)
        target_link_options(CharacterCameraRegression PRIVATE /OPT:REF)
        set(CHARACTER_PHYSX_DIR "${CMAKE_SOURCE_DIR}/ThirdParty/NVIDIA/PhysX/Lib/bin/win.x86_64.vc143.md/release")
        foreach(PHYSX_LIB PhysX PhysXCommon PhysXFoundation PhysXExtensions_static PhysXCharacterKinematic_static PhysXPvdSDK_static)
            target_link_libraries(CharacterCameraRegression PRIVATE "${CHARACTER_PHYSX_DIR}/${PHYSX_LIB}.lib")
        endforeach()
        foreach(PHYSX_DLL PhysX PhysXCommon PhysXFoundation)
            add_custom_command(TARGET CharacterCameraRegression POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CHARACTER_PHYSX_DIR}/${PHYSX_DLL}.dll" "$<TARGET_FILE_DIR:CharacterCameraRegression>")
        endforeach()
        add_test(NAME character_camera_regression COMMAND CharacterCameraRegression)
        add_executable(RagdollRegression tests/RagdollRegression.cpp)
        target_include_directories(RagdollRegression PRIVATE ${MOUSE_TEST_INCLUDE_DIRECTORIES})
        target_compile_definitions(RagdollRegression PRIVATE NDEBUG NOMINMAX WIN32_LEAN_AND_MEAN)
        target_compile_options(RagdollRegression PRIVATE /utf-8 /EHsc)
        foreach(PHYSX_LIB PhysX PhysXCommon PhysXFoundation PhysXExtensions_static PhysXPvdSDK_static)
            target_link_libraries(RagdollRegression PRIVATE "${CHARACTER_PHYSX_DIR}/${PHYSX_LIB}.lib")
        endforeach()
        foreach(PHYSX_DLL PhysX PhysXCommon PhysXFoundation)
            add_custom_command(TARGET RagdollRegression POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CHARACTER_PHYSX_DIR}/${PHYSX_DLL}.dll" "$<TARGET_FILE_DIR:RagdollRegression>")
        endforeach()
        add_test(NAME ragdoll_regression COMMAND RagdollRegression)
        add_executable(EditorDataRegression tests/EditorDataRegression.cpp src/Level/LevelSystem.cpp)
        target_include_directories(EditorDataRegression PRIVATE "${CMAKE_SOURCE_DIR}/src")
        target_compile_definitions(EditorDataRegression PRIVATE ENGINE_SERVER=1 NOMINMAX WIN32_LEAN_AND_MEAN)
        target_compile_options(EditorDataRegression PRIVATE /utf-8 /EHsc)
        add_test(NAME editor_data_regression COMMAND EditorDataRegression)
        add_executable(EditorWorkflowRegression tests/EditorWorkflowRegression.cpp src/Prefab/Prefab.cpp)
        target_include_directories(EditorWorkflowRegression PRIVATE "${CMAKE_SOURCE_DIR}/src")
        target_compile_definitions(EditorWorkflowRegression PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        target_compile_options(EditorWorkflowRegression PRIVATE /utf-8 /EHsc)
        add_test(NAME editor_workflow_regression COMMAND EditorWorkflowRegression)
        add_executable(AudioEmitterRegression tests/AudioEmitterRegression.cpp src/Scene/SceneNode.cpp)
        target_include_directories(AudioEmitterRegression PRIVATE ${MOUSE_TEST_INCLUDE_DIRECTORIES})
        target_compile_definitions(AudioEmitterRegression PRIVATE ENGINE_SERVER=1 NOMINMAX WIN32_LEAN_AND_MEAN)
        target_compile_options(AudioEmitterRegression PRIVATE /utf-8 /EHsc)
        add_test(NAME audio_emitter_regression COMMAND AudioEmitterRegression)
        add_executable(SceneStorageRegression tests/SceneStorageRegression.cpp
            src/Scene/SceneNode.cpp src/Scene/SceneGraph.cpp
            src/Audio/AudioSystem.cpp src/Platform/PlatformUtils.cpp
            src/Storage/SaveSystem.cpp src/Storage/PakCompression.cpp)
        get_target_property(CLIENT_INCLUDE_DIRECTORIES Idontknow INCLUDE_DIRECTORIES)
        target_include_directories(SceneStorageRegression PRIVATE ${CLIENT_INCLUDE_DIRECTORIES})
        target_compile_definitions(SceneStorageRegression PRIVATE ENGINE_RELEASE=1 NOMINMAX WIN32_LEAN_AND_MEAN)
        target_compile_options(SceneStorageRegression PRIVATE /utf-8 /EHsc /Gy)
        target_link_options(SceneStorageRegression PRIVATE /OPT:REF)
        target_link_libraries(SceneStorageRegression PRIVATE bcrypt ole32 shell32 user32)
        set_target_properties(SceneStorageRegression PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/regression")
        add_test(NAME content_fixture COMMAND powershell -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/tests/CreateContentFixtures.ps1"
            -OutputDirectory "$<TARGET_FILE_DIR:SceneStorageRegression>")
        set_tests_properties(content_fixture PROPERTIES FIXTURES_SETUP PackagedContent)
        add_test(NAME scene_storage_regression COMMAND SceneStorageRegression)
        set_tests_properties(scene_storage_regression PROPERTIES FIXTURES_REQUIRED PackagedContent)
    endif()
endif()

if(BUILD_TESTING)
    add_executable(ProjectRegression tests/ProjectRegression.cpp)
    target_include_directories(ProjectRegression PRIVATE "${CMAKE_SOURCE_DIR}/src")
    add_test(NAME project_regression COMMAND ProjectRegression)
endif()
