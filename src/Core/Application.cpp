#include "Core/Project.h"
#if defined(ENGINE_EDITOR)
#include "UI/ProjectSettings.h"
#include "UI/SceneGizmos.h"
#endif
#include "PostProcess/PostProcessSerialization.h"
#include "Renderer/Common/SceneLighting.h"
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shlobj.h>
#include <psapi.h>
#elif defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
#include "Platform/PlatformTypes.h"
#include "Platform/PlatformUtils.h"
#include "Renderer/PS4/PS4RendererBackend.h"
#else
#include "Platform/PlatformTypes.h"
#include "Platform/PlatformUtils.h"
#include "Renderer/Metal/MetalRendererBackend.h"
#endif
#include "Platform/PlatformUtils.h"
#include "Application.h"
#include "DiagnosticPaths.h"
#include "UI/AntiAliasingControls.h"
#include "Window.h"
#include "Renderer.h"
#include "Camera.h"
#include "CameraSystem.h"


#include "Timer.h"
#include "TextureStreamer.h"
#include "Texture.h"
#include "Sequencer.h"
#include "Animation/AnimationSystem.h"
#include "Rain/RainSystem.h"
#include "Physics/PhysicsSystem.h"
#include "Physics/ClothSystem.h"
#include "Physics/HairSystem.h"
#include "UI/WebMenuSystem.h"
#include "UI/ContentBrowser.h"
#include "Scene/SceneDocument.h"
#include "Renderer/CameraDocument.h"
#include "Scene/GameplayRuntime.h"
#include "UI/GameplayInspector.h"
#include "UI/PrefabInspector.h"
#include "UI/MaterialPreview.h"
#include "Asset/MaterialAsset.h"
#include "Level/LevelSystem.h"
#include "SaveSystem.h"
#include "Steam/SteamSystem.h"
#include "Epic/EpicSystem.h"
#include "Discord/DiscordSystem.h"
#include "Security/SecuritySystem.h"
#include "DualSense.h"
#include "DualShock.h"
#if defined(_WIN32)
#include "Storage/DirectStorage/DirectStorageSystem.h"
#endif
#include "DualSenseAudio.h"
#include "Audio/AudioSystem.h"
#include "Physics/CharacterController.h"
#include "Networking/MultiplayerManager.h"
#include "Scene/SceneGraph.h"
#include "Script/ScriptSystem.h"
#include "AI/NavigationSystem.h"
#include "VFX/ParticleSystem.h"
#include "Terrain/TerrainSystem.h"
#include "PostProcess/PostProcessSystem.h"
#include "Prefab/PrefabSystem.h"
#include "Asset/AssetRegistry.h"
#include "Asset/AssetImport.h"
#if defined(_WIN32)
#include <commdlg.h>
#endif
#include "Debug/Profiler.h"
#include "Debug/DebugConsole.h"
#include "Input/InputMapping.h"
#ifndef ENGINE_RELEASE
#include <imgui.h>
#include "imgui/ImGuizmo.h"
#endif
#include <fstream>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <type_traits>

Application::Application(int width, int height, const std::wstring& title)
    : m_width(width), m_height(height), m_title(title) {
}

Application::~Application() {
    EngineScene::SceneGraph::Get().Shutdown();
    EngineAudio::AudioSystem::Get().Shutdown();
    EngineNetwork::MultiplayerManager::Get().Shutdown();
    EngineSecurity::SecuritySystem::Get().Shutdown();
    EngineDiscord::DiscordSystem::Get().Shutdown();
#if defined(ENGINE_PLATFORM_EPIC)
    EngineEpic::EpicSystem::Get().Shutdown();
#else
    EngineSteam::SteamSystem::Get().Shutdown();
#endif
    EngineAudio::DualSenseAudioSystem::Get().Shutdown();
    EngineInput::DualShockController::Get().Shutdown();
    EngineInput::DualSenseController::Get().Shutdown();
}

static float s_stickLookSens = 2.5f;
static bool s_invertLookY = false;

static bool RayIntersectBox(const XMFLOAT3& rayOrig, const XMFLOAT3& rayDir,
                            const XMFLOAT3& boxPos, const XMFLOAT3& boxScale, const XMFLOAT3& boxRot,
                            float& outDist)
{
    XMMATRIX S = XMMatrixScaling(boxScale.x, boxScale.y, boxScale.z);
    XMMATRIX R = XMMatrixRotationRollPitchYaw(XMConvertToRadians(boxRot.x), XMConvertToRadians(boxRot.y), XMConvertToRadians(boxRot.z));
    XMMATRIX T = XMMatrixTranslation(boxPos.x, boxPos.y, boxPos.z);
    XMMATRIX world = S * R * T;
    XMMATRIX invWorld = XMMatrixInverse(nullptr, world);

    XMVECTOR orig = XMLoadFloat3(&rayOrig);
    XMVECTOR dir = XMLoadFloat3(&rayDir);

    XMVECTOR localOrig = XMVector3TransformCoord(orig, invWorld);
    XMVECTOR localDir = XMVector3Normalize(XMVector3TransformNormal(dir, invWorld));

    XMFLOAT3 lo, ld;
    XMStoreFloat3(&lo, localOrig);
    XMStoreFloat3(&ld, localDir);

    float tmin = -1e9f;
    float tmax = 1e9f;

    // AABB unit box bounds [-0.5, 0.5]
    float boxMin[3] = { -0.5f, -0.5f, -0.5f };
    float boxMax[3] = {  0.5f,  0.5f,  0.5f };
    float ro[3] = { lo.x, lo.y, lo.z };
    float rd[3] = { ld.x, ld.y, ld.z };

    for (int i = 0; i < 3; ++i) {
        if (fabs(rd[i]) < 1e-6f) {
            if (ro[i] < boxMin[i] || ro[i] > boxMax[i]) return false;
        } else {
            float t1 = (boxMin[i] - ro[i]) / rd[i];
            float t2 = (boxMax[i] - ro[i]) / rd[i];
            if (t1 > t2) std::swap(t1, t2);
            tmin = (std::max)(tmin, t1);
            tmax = (std::min)(tmax, t2);
            if (tmin > tmax || tmax < 0.0f) return false;
        }
    }

    outDist = tmin > 0.0f ? tmin : tmax;
    return true;
}

static std::string GetEngineConfigReadPath(const std::string& filename) {
    std::error_code ec;

    // 0. PS4 / Package app0 root & Contents directory
    std::vector<std::filesystem::path> packagePaths = {
        std::filesystem::path("/app0") / filename,
        std::filesystem::path("/app0/Contents") / filename,
        std::filesystem::path("/app0/Contents/Levels") / filename,
        std::filesystem::path(filename),
        std::filesystem::path("Contents") / filename,
        std::filesystem::path("Contents/Levels") / filename,
        std::filesystem::path("src/Content/Levels") / filename,
        std::filesystem::path("..") / filename,
        std::filesystem::path("../Contents") / filename
    };
    for (const auto& p : packagePaths) {
        if (std::filesystem::exists(p, ec)) return p.string();
    }

    // 1. Check C:/Users/user/Documents/MyEngine/STEAMUSER/
    std::filesystem::path userPath = std::filesystem::path("C:/Users/user/Documents/MyEngine/STEAMUSER") / filename;
    if (std::filesystem::exists(userPath, ec)) return userPath.string();

    std::filesystem::path userPathIskan = std::filesystem::path("C:/Users/iskan/Documents/MyEngine/STEAMUSER") / filename;
    if (std::filesystem::exists(userPathIskan, ec)) return userPathIskan.string();

    // 2. Check USERPROFILE Documents Idontknow STEAMUSER
#if defined(_WIN32)
    wchar_t myDocsPath[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_MYDOCUMENTS, NULL, 0, myDocsPath))) {
        std::filesystem::path p(myDocsPath);
        std::filesystem::path full = p / "MyEngine" / "STEAMUSER" / filename;
        if (std::filesystem::exists(full, ec)) {
            return full.string();
        }
    }
#else
    std::filesystem::path full = std::filesystem::path(EnginePlatform::PlatformUtils::GetEngineUserDirectory()) / filename;
    if (std::filesystem::exists(full, ec)) return full.string();
#endif

    // 3. Check local Config directory
    std::filesystem::path local1 = std::filesystem::path("Config") / filename;
    if (std::filesystem::exists(local1, ec)) return local1.string();
    std::filesystem::path local2 = std::filesystem::path("../Config") / filename;
    if (std::filesystem::exists(local2, ec)) return local2.string();

    return filename;
}

static std::string GetEngineConfigWritePath(const std::string& filename) {
    std::error_code ec;
    // 1. Try Documents/MyEngine/STEAMUSER
#if defined(_WIN32)
    wchar_t myDocsPath[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_MYDOCUMENTS, NULL, 0, myDocsPath))) {
        std::filesystem::path p(myDocsPath);
        std::filesystem::path dir = p / "MyEngine" / "STEAMUSER";
        std::filesystem::create_directories(dir, ec);
        if (!ec) return (dir / filename).string();
    }
#else
    std::filesystem::path dir(EnginePlatform::PlatformUtils::GetEngineUserDirectory());
    std::filesystem::create_directories(dir, ec);
    if (!ec) return (dir / filename).string();
#endif

    // 2. Try C:/Users/user/Documents/MyEngine/STEAMUSER
    std::filesystem::path userDir("C:/Users/user/Documents/MyEngine/STEAMUSER");
    std::filesystem::create_directories(userDir, ec);
    if (!ec && std::filesystem::exists(userDir, ec)) return (userDir / filename).string();

    std::filesystem::create_directories("Config", ec);
    return (std::filesystem::path("Config") / filename).string();
}

std::string Application::CaptureScene() {
    std::ostringstream file;
    file.precision(9);
    file << "WORLD_LAYOUT 4\n";

    auto& lightDir = m_renderer->GetLightDir();
    auto& lightColor = m_renderer->GetLightColor();

    file << "LIGHT_DIR " << lightDir.x << " " << lightDir.y << " " << lightDir.z << "\n";
    file << "LIGHT_COLOR " << lightColor.x << " " << lightColor.y << " " << lightColor.z << "\n";

    XMFLOAT3 skyColor = m_renderer->GetSkyColor();
    file << "SKY_COLOR " << skyColor.x << " " << skyColor.y << " " << skyColor.z << "\n";

    EngineRender::WriteSceneLights(file, m_renderer->GetPointLights(), m_renderer->GetSpotLights());
    auto& post=EnginePostProcess::PostProcessSystem::Get();
    EnginePostProcess::WritePostProcessState(file,post.GetGlobalSettings(),post.GetVolumes());
    file<<"GRAPHICS_PIPELINE "<<m_renderer->m_dxrEnabled<<' '<<m_renderer->m_rtShadowsEnabled<<' '<<m_renderer->m_rtReflectionsEnabled<<' '
        <<m_renderer->IsPathTracingEnabled()<<' '<<static_cast<int>(m_renderer->GetUpscaleTechnique())<<' '<<static_cast<int>(m_renderer->GetUpscaleQuality())<<' '
        <<m_renderer->IsVSyncEnabled()<<' '<<m_renderer->IsTAAEnabled()<<'\n';
    file << "GRAPHICS_FEATURES " << m_renderer->IsSSAOEnabled() << " " << m_renderer->IsSSGIEnabled() << " " << m_renderer->IsVSMEnabled() << " " << m_renderer->IsVolumetricFogEnabled() << " " << m_renderer->GetVolumetricFogDensity() << " " << m_renderer->GetCloudDensity() << " " << m_renderer->GetFOV() << "\n";
    file << "DLSS_RR " << static_cast<int>(m_renderer->GetDLSSRayReconstructionVersion()) << "\n";
    file << "GI_INTENSITY " << m_renderer->GetSSGIIntensity() << "\n";
    auto& models = m_renderer->GetModels();
    for (const auto& model : models) {
        file << "MODEL " << model.name << "\n";
        file << "INDEX_COUNT " << model.indexCount << "\n";
        file << "START_INDEX " << model.startIndexLocation << "\n";
        file << "BASE_VERTEX " << model.baseVertexLocation << "\n";
        file << "GEOMETRY_REF " << std::quoted(model.geometryName) << "\n";
        file << "PREFAB_LINK " << std::quoted(model.prefabSource) << " " << std::quoted(model.prefabNode) << "\n";
        EngineScene::WriteGameplay(file,model.gameplay);
        file << "VISIBLE " << (model.visible ? 1 : 0) << "\n";
        file << "COLLISION " << (model.hasCollision ? 1 : 0) << "\n";
        file << "ALBEDO_INDEX " << model.albedoIndex << "\n";
        file << "NORMAL_INDEX " << model.normalIndex << "\n";
        file << "RMA_INDEX " << model.rmaIndex << "\n";
        file << "POS " << model.position.x << " " << model.position.y << " " << model.position.z << "\n";
        file << "ROT " << model.rotation.x << " " << model.rotation.y << " " << model.rotation.z << "\n";
        file << "SCALE " << model.scale.x << " " << model.scale.y << " " << model.scale.z << "\n";
        file << "PRESET " << static_cast<int>(model.material.preset) << "\n";
        file << "MAT_ALBEDO " << model.material.albedo.x << " " << model.material.albedo.y << " " << model.material.albedo.z << "\n";
        file << "MAT_ROUGHNESS " << model.material.roughness << "\n";
        file << "MAT_METALLIC " << model.material.metallic << "\n";
        file << "MAT_REFLECTANCE " << model.material.reflectance << "\n";
        file << "MAT_EMISSION " << model.material.emission << "\n";
        file << "MAT_IOR " << model.material.ior << "\n";
        file << "UV_TILING " << model.material.uvTiling.x << " " << model.material.uvTiling.y << "\n";
        file<<"TESSELLATION ";EngineRender::WriteTessellation(file,model.material.tessellation);file<<"\n";
        EnginePhysics::WriteCollider(file, model.collider);
        EnginePhysics::WriteRagdoll(file, model.ragdollSettings);
        EnginePhysics::WriteVehicle(file, model.vehicle);
        file << "PHYSICS_BODY " << model.physicsBodyType << "\n";
        file << "PHYSICS_SHAPE " << model.physicsShapeType << "\n";
        file << "PHYSICS_MASS " << model.physicsMass << "\n";
        file << "PHYSICS_DAMPING " << model.physicsLinearDamping << " " << model.physicsAngularDamping << "\n";
        file << "PHYSICS_FRICTION " << model.physicsFriction << "\n";
        file << "PHYSICS_RESTITUTION " << model.physicsRestitution << "\n";
        file << "PHYSICS_GRAVITY " << (model.physicsEnableGravity ? 1 : 0) << "\n";
        file << "PHYSICS_CCD " << (model.physicsEnableCCD ? 1 : 0) << "\n";
        if (!model.material.albedoPath.empty()) file << "ALBEDO_PATH " << model.material.albedoPath << "\n";
        if (!model.material.normalPath.empty()) file << "NORMAL_PATH " << model.material.normalPath << "\n";
        if (!model.material.rmaPath.empty()) file << "RMA_PATH " << model.material.rmaPath << "\n";
        if (!model.material.aoPath.empty()) file << "AO_PATH " << model.material.aoPath << "\n";
        if (!model.material.emissivePath.empty()) file << "EMISSIVE_PATH " << model.material.emissivePath << "\n";
    }

    auto& dynModels = m_renderer->GetDynamicModels();
    for (const auto& model : dynModels) {
        file << "DYN_MODEL " << model.name << "\n";
        file << "FILEPATH " << model.filepath << "\n";
        file << "GEOMETRY_REF " << std::quoted(model.geometryName) << "\n";
        file << "PREFAB_LINK " << std::quoted(model.prefabSource) << " " << std::quoted(model.prefabNode) << "\n";
        EngineScene::WriteGameplay(file,model.gameplay);
        file << "VISIBLE " << (model.visible ? 1 : 0) << "\n";
        file << "COLLISION " << (model.hasCollision ? 1 : 0) << "\n";
        file << "ALBEDO_INDEX " << model.albedoIndex << "\n";
        file << "NORMAL_INDEX " << model.normalIndex << "\n";
        file << "RMA_INDEX " << model.rmaIndex << "\n";
        file << "POS " << model.position.x << " " << model.position.y << " " << model.position.z << "\n";
        file << "ROT " << model.rotation.x << " " << model.rotation.y << " " << model.rotation.z << "\n";
        file << "SCALE " << model.scale.x << " " << model.scale.y << " " << model.scale.z << "\n";
        file << "PRESET " << static_cast<int>(model.material.preset) << "\n";
        file << "MAT_ALBEDO " << model.material.albedo.x << " " << model.material.albedo.y << " " << model.material.albedo.z << "\n";
        file << "MAT_ROUGHNESS " << model.material.roughness << "\n";
        file << "MAT_METALLIC " << model.material.metallic << "\n";
        file << "MAT_REFLECTANCE " << model.material.reflectance << "\n";
        file << "MAT_EMISSION " << model.material.emission << "\n";
        file << "MAT_IOR " << model.material.ior << "\n";
        file << "UV_TILING " << model.material.uvTiling.x << " " << model.material.uvTiling.y << "\n";
        file<<"TESSELLATION ";EngineRender::WriteTessellation(file,model.material.tessellation);file<<"\n";
        EnginePhysics::WriteCollider(file, model.collider);
        EnginePhysics::WriteRagdoll(file, model.ragdollSettings);
        EnginePhysics::WriteVehicle(file, model.vehicle);
        file << "PHYSICS_BODY " << model.physicsBodyType << "\n";
        file << "PHYSICS_SHAPE " << model.physicsShapeType << "\n";
        file << "PHYSICS_MASS " << model.physicsMass << "\n";
        file << "PHYSICS_DAMPING " << model.physicsLinearDamping << " " << model.physicsAngularDamping << "\n";
        file << "PHYSICS_FRICTION " << model.physicsFriction << "\n";
        file << "PHYSICS_RESTITUTION " << model.physicsRestitution << "\n";
        file << "PHYSICS_GRAVITY " << (model.physicsEnableGravity ? 1 : 0) << "\n";
        file << "PHYSICS_CCD " << (model.physicsEnableCCD ? 1 : 0) << "\n";
        if (!model.material.albedoPath.empty()) file << "ALBEDO_PATH " << model.material.albedoPath << "\n";
        if (!model.material.normalPath.empty()) file << "NORMAL_PATH " << model.material.normalPath << "\n";
        if (!model.material.rmaPath.empty()) file << "RMA_PATH " << model.material.rmaPath << "\n";
        if (!model.material.aoPath.empty()) file << "AO_PATH " << model.material.aoPath << "\n";
        if (!model.material.emissivePath.empty()) file << "EMISSIVE_PATH " << model.material.emissivePath << "\n";
    }

    // Save RainSystem Settings
    const auto& rainSettings = EngineRain::RainSystem::Get().GetSettings();
    file << "RAIN_ENABLED " << (rainSettings.enabled ? 1 : 0) << "\n";
    file << "RAIN_INTENSITY " << rainSettings.intensity << "\n";
    file << "RAIN_PARTICLES " << rainSettings.particleCount << "\n";
    file << "RAIN_AREA " << rainSettings.areaRadius << "\n";
    file << "RAIN_DROP_SPEED " << rainSettings.fallSpeed << "\n";
    file << "RAIN_WIND_ENABLED " << (rainSettings.wind.enabled ? 1 : 0) << "\n";
    file << "RAIN_WIND_STRENGTH " << rainSettings.wind.speed << "\n";
    file << "RAIN_WIND_DIR " << rainSettings.wind.direction.x << " " << rainSettings.wind.direction.y << " " << rainSettings.wind.direction.z << "\n";
    file << "RAIN_SURFACE_INTERACTION " << (rainSettings.surface.enabled ? 1 : 0) << "\n";
    file << "RAIN_WETNESS_ENABLED " << (rainSettings.surface.wetnessEnabled ? 1 : 0) << "\n";
    file << "RAIN_WETNESS_STRENGTH " << rainSettings.surface.wetnessStrength << "\n";
    file << "RAIN_PUDDLES_ENABLED " << (rainSettings.surface.puddlesEnabled ? 1 : 0) << "\n";
    file << "RAIN_PUDDLES_ACCUM " << rainSettings.surface.puddleAccumulation << "\n";
    file << "RAIN_SPLASH_ENABLED " << (rainSettings.surface.splashesEnabled ? 1 : 0) << "\n";
    file << "RAIN_SPLASH_INTENSITY " << rainSettings.surface.splashIntensity << "\n";
    file << "RAIN_TRAILS_ENABLED " << (rainSettings.surface.surfaceFlowEnabled ? 1 : 0) << "\n";
    file << "RAIN_TRAIL_STRENGTH " << rainSettings.surface.trailStrength << "\n";
    file << "RAIN_CAMERA_DROPS " << (rainSettings.cameraDrops.enabled ? 1 : 0) << "\n";
    file << "RAIN_CAMERA_INTENSITY " << rainSettings.cameraDrops.intensity << "\n";
    file << "RAIN_QUALITY " << static_cast<int>(rainSettings.quality) << "\n";

    file << WriteCameraDocument();
    file << EngineScene::WriteHierarchy();
    return file.str();
}

bool Application::SaveScene() {
    // Capture ImGui window settings from memory
    std::string imguiSettings = "";
#ifndef ENGINE_RELEASE
    size_t imguiSize = 0;
    const char* imguiData = ImGui::SaveIniSettingsToMemory(&imguiSize);
    if (imguiData && imguiSize > 0) {
        imguiSettings = std::string(imguiData, imguiSize);
    }
#endif

    // Save combined scene + imgui settings into encrypted & compressed Save1.save
    return EngineSave::SaveManager::Get().SaveGame(CaptureScene(), imguiSettings, "Save1.save");
}

void Application::LoadScene() {
    EngineSave::SavePackage pkg;
    std::string sceneData;

    if (EngineSave::SaveManager::Get().LoadGame(pkg, "Save1.save")) {
        sceneData = pkg.sceneData;
#ifndef ENGINE_RELEASE
        if (!pkg.imguiSettings.empty()) {
            ImGui::LoadIniSettingsFromMemory(pkg.imguiSettings.c_str(), pkg.imguiSettings.size());
        }
#endif
    } else {
#if defined(ENGINE_EDITOR)
        if(EngineSave::SaveManager::Get().SaveExists()&&!m_benchmarkFrameLimit)m_showRecovery=true;
#endif
        // Fallback to legacy loose scene_save.txt if Save1.save doesn't exist
        std::string readPath = GetEngineConfigReadPath("scene_save.txt");
        std::ifstream file(readPath);
        if (file.is_open()) {
            std::stringstream ss;
            ss << file.rdbuf();
            sceneData = ss.str();
        }
    }

    ApplyScene(sceneData);
}

void Application::ApplyScene(const std::string& sceneData) {
    if(m_gameplay)m_gameplay->Clear();
    if (sceneData.empty()) return;

    std::string line;
    m_renderer->SynchronizeForUpdate();
    auto& models = m_renderer->GetModels();
    m_renderer->GetPointLights().clear(); m_renderer->GetSpotLights().clear();
    EnginePhysics::PhysicsSystem::Get().ClearAllActors();
    models.clear(); // FIX: Clear existing models to avoid duplicates from Initialize

    auto& dynModels = m_renderer->GetDynamicModels();
    dynModels.clear();

    auto& post=EnginePostProcess::PostProcessSystem::Get();
    post.ClearVolumes();post.ApplyPresetCleanNeutral();
    std::vector<EnginePostProcess::PostProcessVolume> loadedVolumes;
    int loadedWorldRevision=0;
    std::string pendingDynamicName;
    Renderer::RenderModel* currentModel = nullptr;
    Renderer::RenderDynamicModel* currentDynModel = nullptr;

    std::istringstream file(sceneData);
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream iss(line);
        std::string tag;
        iss >> tag;
        
        if(tag.rfind("RAIN_",0)==0){
        if (tag == "RAIN_ENABLED") { int val; iss >> val; EngineRain::RainSystem::Get().GetSettings().enabled = (val != 0); }
        else if (tag == "RAIN_INTENSITY") iss >> EngineRain::RainSystem::Get().GetSettings().intensity;
        else if (tag == "RAIN_PARTICLES") iss >> EngineRain::RainSystem::Get().GetSettings().particleCount;
        else if (tag == "RAIN_AREA") iss >> EngineRain::RainSystem::Get().GetSettings().areaRadius;
        else if (tag == "RAIN_DROP_SPEED") iss >> EngineRain::RainSystem::Get().GetSettings().fallSpeed;
        else if (tag == "RAIN_WIND_ENABLED") { int val; iss >> val; EngineRain::RainSystem::Get().GetSettings().wind.enabled = (val != 0); }
        else if (tag == "RAIN_WIND_STRENGTH") iss >> EngineRain::RainSystem::Get().GetSettings().wind.speed;
        else if (tag == "RAIN_WIND_DIR") iss >> EngineRain::RainSystem::Get().GetSettings().wind.direction.x >> EngineRain::RainSystem::Get().GetSettings().wind.direction.y >> EngineRain::RainSystem::Get().GetSettings().wind.direction.z;
        else if (tag == "RAIN_SURFACE_INTERACTION") { int val; iss >> val; EngineRain::RainSystem::Get().GetSettings().surface.enabled = (val != 0); }
        else if (tag == "RAIN_WETNESS_ENABLED") { int val; iss >> val; EngineRain::RainSystem::Get().GetSettings().surface.wetnessEnabled = (val != 0); }
        else if (tag == "RAIN_WETNESS_STRENGTH") iss >> EngineRain::RainSystem::Get().GetSettings().surface.wetnessStrength;
        else if (tag == "RAIN_PUDDLES_ENABLED") { int val; iss >> val; EngineRain::RainSystem::Get().GetSettings().surface.puddlesEnabled = (val != 0); }
        else if (tag == "RAIN_PUDDLES_ACCUM") iss >> EngineRain::RainSystem::Get().GetSettings().surface.puddleAccumulation;
        else if (tag == "RAIN_SPLASH_ENABLED") { int val; iss >> val; EngineRain::RainSystem::Get().GetSettings().surface.splashesEnabled = (val != 0); }
        else if (tag == "RAIN_SPLASH_INTENSITY") iss >> EngineRain::RainSystem::Get().GetSettings().surface.splashIntensity;
        else if (tag == "RAIN_TRAILS_ENABLED") { int val; iss >> val; EngineRain::RainSystem::Get().GetSettings().surface.surfaceFlowEnabled = (val != 0); }
        else if (tag == "RAIN_TRAIL_STRENGTH") iss >> EngineRain::RainSystem::Get().GetSettings().surface.trailStrength;
        else if (tag == "RAIN_CAMERA_DROPS") { int val; iss >> val; EngineRain::RainSystem::Get().GetSettings().cameraDrops.enabled = (val != 0); }
        else if (tag == "RAIN_CAMERA_INTENSITY") iss >> EngineRain::RainSystem::Get().GetSettings().cameraDrops.intensity;
        else if (tag == "RAIN_QUALITY") { int val; iss >> val; EngineRain::RainSystem::Get().GetSettings().quality = static_cast<EngineRain::RainQuality>(val); }
            continue;
        }
        if (EngineRender::ReadSceneLight(tag, iss, m_renderer->GetPointLights(), m_renderer->GetSpotLights())) continue;
        if(EnginePostProcess::ReadPostProcessState(tag,iss,post.GetGlobalSettings(),loadedVolumes)) continue;
        if(tag=="GRAPHICS_PIPELINE") {
            bool dxr,shadows,reflections,pathTracing,vsync,taa;int technique,quality;
            if(iss>>dxr>>shadows>>reflections>>pathTracing>>technique>>quality>>vsync>>taa) {
                m_renderer->m_dxrEnabled=dxr;m_renderer->m_rtShadowsEnabled=shadows;m_renderer->m_rtReflectionsEnabled=reflections;
                m_renderer->SetPathTracingEnabled(pathTracing);
                m_renderer->SetUpscaleTechnique(static_cast<UpscaleTechnique>((std::clamp)(technique,0,3)));
                m_renderer->SetUpscaleQuality(static_cast<UpscaleQuality>((std::clamp)(quality,0,4)));
                m_renderer->SetVSyncEnabled(vsync);m_renderer->SetTAAEnabled(taa);
            }
            continue;
        }
        if (tag == "GRAPHICS_FEATURES") {
            bool ao, gi, shadows, fog; float density, clouds, fov;
            if (iss >> ao >> gi >> shadows >> fog >> density >> clouds >> fov) {
                m_renderer->SetSSAOEnabled(ao); m_renderer->SetSSGIEnabled(gi); m_renderer->SetVSMEnabled(shadows);
                m_renderer->SetVolumetricFogEnabled(fog); m_renderer->SetVolumetricFogDensity(density);
                m_renderer->SetCloudDensity(clouds); m_renderer->SetFOV(fov);
            }
            continue;
        }
        if(tag=="DLSS_RR"){int preset=0;if(iss>>preset)m_renderer->SetDLSSRayReconstructionVersion(static_cast<Renderer::DLSSRRVersion>(preset));continue;}
        if(tag=="GI_INTENSITY"){float intensity=1;if(iss>>intensity)m_renderer->SetSSGIIntensity(intensity);continue;}
        if (tag == "WORLD_LAYOUT") {
            int revision = 0;
            iss >> revision;
            m_loadedCleanWorld = revision >= 3;
            loadedWorldRevision=revision;
        } else if (tag == "LIGHT_DIR") {
            XMFLOAT3 dir;
            iss >> dir.x >> dir.y >> dir.z;
            m_renderer->SetLightDir(dir);
        } else if (tag == "SKY_COLOR") {
            XMFLOAT3 col;
            iss >> col.x >> col.y >> col.z;
            m_renderer->SetSkyColor(col);
        } else if (tag == "LIGHT_COLOR") {
            XMFLOAT3 col;
            iss >> col.x >> col.y >> col.z;
            m_renderer->SetLightColor(col);
        } else if (tag == "MODEL") {
            std::string name;
            std::getline(iss >> std::ws, name);
            // Remap geometry offsets from runtime values to avoid stale saved indices
            auto geoInfo = m_renderer->GetPrimitiveGeometry(name);
            if (geoInfo.valid) {
                models.emplace_back(name, geoInfo.indexCount, geoInfo.startIndex, geoInfo.baseVertex, geoInfo.vertexCount);
            } else {
                models.emplace_back(name, 0, 0, 0); // Unknown type – offsets will be overridden by file
            }
            currentModel = &models.back();
            currentDynModel = nullptr;
        } else if (tag == "DYN_MODEL") {
            std::string name;
            std::getline(iss >> std::ws, name);
            pendingDynamicName=name;
            currentModel = nullptr;
            currentDynModel = nullptr; // Wait until FILEPATH to spawn
        } else if (tag == "FILEPATH") {
            std::string filepath;
            std::getline(iss >> std::ws, filepath);
            size_t before=dynModels.size();m_renderer->SpawnDynamicRenderModel(filepath.c_str(), XMFLOAT3(0,0,0));
            if (dynModels.size()>before) {
                currentDynModel = &dynModels.back();
                currentDynModel->name=pendingDynamicName;
            }
        } else if (currentModel) {
            // For known primitives (Cube, Sphere, Plane, Triangle, Floor), geometry offsets
            // were already correctly set by GetPrimitiveGeometry() above. Skip stale file values.
            bool isKnownPrimitive = m_renderer->GetPrimitiveGeometry(currentModel->name).valid;
            if (tag == "INDEX_COUNT") { if (!isKnownPrimitive) iss >> currentModel->indexCount; }
            else if (tag == "START_INDEX") { if (!isKnownPrimitive) iss >> currentModel->startIndexLocation; }
            else if (tag == "BASE_VERTEX") { if (!isKnownPrimitive) iss >> currentModel->baseVertexLocation; }
            else if (tag == "VISIBLE") { int v; iss >> v; currentModel->visible = (v != 0); }
            else if (tag == "COLLISION") { int c; iss >> c; currentModel->hasCollision = (c != 0); }
            else if (tag == "ALBEDO_INDEX") iss >> currentModel->albedoIndex;
            else if (tag == "NORMAL_INDEX") iss >> currentModel->normalIndex;
            else if (tag == "RMA_INDEX") iss >> currentModel->rmaIndex;
            else if (tag == "POS") iss >> currentModel->position.x >> currentModel->position.y >> currentModel->position.z;
            else if (tag == "ROT") iss >> currentModel->rotation.x >> currentModel->rotation.y >> currentModel->rotation.z;
            else if (tag == "SCALE") iss >> currentModel->scale.x >> currentModel->scale.y >> currentModel->scale.z;
            else if (tag == "PRESET") { int p; iss >> p; currentModel->material.preset = static_cast<Renderer::MaterialPreset>(p); }
            else if (tag == "MAT_ALBEDO") iss >> currentModel->material.albedo.x >> currentModel->material.albedo.y >> currentModel->material.albedo.z;
            else if (tag == "MAT_ROUGHNESS") iss >> currentModel->material.roughness;
            else if (tag == "MAT_METALLIC") iss >> currentModel->material.metallic;
            else if (tag == "MAT_REFLECTANCE") iss >> currentModel->material.reflectance;
            else if (tag == "MAT_EMISSION") iss >> currentModel->material.emission;
            else if (tag == "MAT_IOR") iss >> currentModel->material.ior;
            else if (tag == "UV_TILING") iss >> currentModel->material.uvTiling.x >> currentModel->material.uvTiling.y;
            else if(tag=="TESSELLATION"){if(!EngineRender::ReadTessellation(iss,currentModel->material.tessellation))throw std::runtime_error("Invalid tessellation settings");}
            else if (tag == "GEOMETRY_REF") { iss >> std::quoted(currentModel->geometryName); }
            else if (tag == "GAMEPLAY") { EngineScene::ReadGameplay(iss,currentModel->gameplay); }
            else if (tag == "PREFAB_LINK") { iss >> std::quoted(currentModel->prefabSource) >> std::quoted(currentModel->prefabNode); }
            else if (EnginePhysics::ReadCollider(tag, iss, currentModel->collider)) {}
            else if (EnginePhysics::ReadRagdoll(tag, iss, currentModel->ragdollSettings)) {}
            else if (EnginePhysics::ReadVehicle(tag, iss, currentModel->vehicle)) {}
            else if (tag == "PHYSICS_BODY") iss >> currentModel->physicsBodyType;
            else if (tag == "PHYSICS_SHAPE") iss >> currentModel->physicsShapeType;
            else if (tag == "PHYSICS_MASS") iss >> currentModel->physicsMass;
            else if (tag == "PHYSICS_DAMPING") iss >> currentModel->physicsLinearDamping >> currentModel->physicsAngularDamping;
            else if (tag == "PHYSICS_FRICTION") iss >> currentModel->physicsFriction;
            else if (tag == "PHYSICS_RESTITUTION") iss >> currentModel->physicsRestitution;
            else if (tag == "PHYSICS_GRAVITY") { int g; iss >> g; currentModel->physicsEnableGravity = (g != 0); }
            else if (tag == "PHYSICS_CCD") { int ccd; iss >> ccd; currentModel->physicsEnableCCD = (ccd != 0); }
            else if (tag == "ALBEDO_PATH") {
                std::getline(iss >> std::ws, currentModel->material.albedoPath);
                currentModel->material.albedoMap = m_renderer->LoadTexture(currentModel->material.albedoPath);
            } else if (tag == "NORMAL_PATH") {
                std::getline(iss >> std::ws, currentModel->material.normalPath);
                currentModel->material.normalMap = m_renderer->LoadTexture(currentModel->material.normalPath);
            } else if (tag == "RMA_PATH") {
                std::getline(iss >> std::ws, currentModel->material.rmaPath);
                currentModel->material.rmaMap = m_renderer->LoadTexture(currentModel->material.rmaPath);
            } else if (tag == "AO_PATH") {
                std::getline(iss >> std::ws, currentModel->material.aoPath);
                currentModel->material.aoMap = m_renderer->LoadTexture(currentModel->material.aoPath);
            } else if (tag == "EMISSIVE_PATH") {
                std::getline(iss >> std::ws, currentModel->material.emissivePath);
                currentModel->material.emissiveMap = m_renderer->LoadTexture(currentModel->material.emissivePath);
            }
        } else if (currentDynModel) {
            if (tag == "VISIBLE") { int v; iss >> v; currentDynModel->visible = (v != 0); }
            else if (tag == "COLLISION") { int c; iss >> c; currentDynModel->hasCollision = (c != 0); }
            else if (tag == "ALBEDO_INDEX") iss >> currentDynModel->albedoIndex;
            else if (tag == "NORMAL_INDEX") iss >> currentDynModel->normalIndex;
            else if (tag == "RMA_INDEX") iss >> currentDynModel->rmaIndex;
            else if (tag == "POS") iss >> currentDynModel->position.x >> currentDynModel->position.y >> currentDynModel->position.z;
            else if (tag == "ROT") iss >> currentDynModel->rotation.x >> currentDynModel->rotation.y >> currentDynModel->rotation.z;
            else if (tag == "SCALE") iss >> currentDynModel->scale.x >> currentDynModel->scale.y >> currentDynModel->scale.z;
            else if (tag == "PRESET") { int p; iss >> p; currentDynModel->material.preset = static_cast<Renderer::MaterialPreset>(p); }
            else if (tag == "MAT_ALBEDO") iss >> currentDynModel->material.albedo.x >> currentDynModel->material.albedo.y >> currentDynModel->material.albedo.z;
            else if (tag == "MAT_ROUGHNESS") iss >> currentDynModel->material.roughness;
            else if (tag == "MAT_METALLIC") iss >> currentDynModel->material.metallic;
            else if (tag == "MAT_REFLECTANCE") iss >> currentDynModel->material.reflectance;
            else if (tag == "MAT_EMISSION") iss >> currentDynModel->material.emission;
            else if (tag == "MAT_IOR") iss >> currentDynModel->material.ior;
            else if (tag == "UV_TILING") iss >> currentDynModel->material.uvTiling.x >> currentDynModel->material.uvTiling.y;
            else if(tag=="TESSELLATION"){if(!EngineRender::ReadTessellation(iss,currentDynModel->material.tessellation))throw std::runtime_error("Invalid tessellation settings");}
            else if (tag == "GEOMETRY_REF") { iss >> std::quoted(currentDynModel->geometryName); }
            else if (tag == "GAMEPLAY") { EngineScene::ReadGameplay(iss,currentDynModel->gameplay); }
            else if (tag == "PREFAB_LINK") { iss >> std::quoted(currentDynModel->prefabSource) >> std::quoted(currentDynModel->prefabNode); }
            else if (EnginePhysics::ReadCollider(tag, iss, currentDynModel->collider)) {}
            else if (EnginePhysics::ReadRagdoll(tag, iss, currentDynModel->ragdollSettings)) {}
            else if (EnginePhysics::ReadVehicle(tag, iss, currentDynModel->vehicle)) {}
            else if (tag == "PHYSICS_BODY") iss >> currentDynModel->physicsBodyType;
            else if (tag == "PHYSICS_SHAPE") iss >> currentDynModel->physicsShapeType;
            else if (tag == "PHYSICS_MASS") iss >> currentDynModel->physicsMass;
            else if (tag == "PHYSICS_DAMPING") iss >> currentDynModel->physicsLinearDamping >> currentDynModel->physicsAngularDamping;
            else if (tag == "PHYSICS_FRICTION") iss >> currentDynModel->physicsFriction;
            else if (tag == "PHYSICS_RESTITUTION") iss >> currentDynModel->physicsRestitution;
            else if (tag == "PHYSICS_GRAVITY") { int g; iss >> g; currentDynModel->physicsEnableGravity = (g != 0); }
            else if (tag == "PHYSICS_CCD") { int ccd; iss >> ccd; currentDynModel->physicsEnableCCD = (ccd != 0); }
            else if (tag == "ALBEDO_PATH") {
                std::getline(iss >> std::ws, currentDynModel->material.albedoPath);
                currentDynModel->material.albedoMap = m_renderer->LoadTexture(currentDynModel->material.albedoPath);
            } else if (tag == "NORMAL_PATH") {
                std::getline(iss >> std::ws, currentDynModel->material.normalPath);
                currentDynModel->material.normalMap = m_renderer->LoadTexture(currentDynModel->material.normalPath);
            } else if (tag == "RMA_PATH") {
                std::getline(iss >> std::ws, currentDynModel->material.rmaPath);
                currentDynModel->material.rmaMap = m_renderer->LoadTexture(currentDynModel->material.rmaPath);
            } else if (tag == "AO_PATH") {
                std::getline(iss >> std::ws, currentDynModel->material.aoPath);
                currentDynModel->material.aoMap = m_renderer->LoadTexture(currentDynModel->material.aoPath);
            } else if (tag == "EMISSIVE_PATH") {
                std::getline(iss >> std::ws, currentDynModel->material.emissivePath);
                currentDynModel->material.emissiveMap = m_renderer->LoadTexture(currentDynModel->material.emissivePath);
            }
        }

    }

    // Remap primitive geometry offsets to the renderer's actual runtime values.
    // The INDEX_COUNT/START_INDEX/BASE_VERTEX in the file may be stale (from a previous
    // build with different geometry layout). Always use the runtime-computed offsets.
    for (auto& model : models) {
        auto geom = m_renderer->GetPrimitiveGeometry(model.geometryName.empty()?model.name:model.geometryName);
        if (geom.valid) {
            model.indexCount          = geom.indexCount;
            model.startIndexLocation  = geom.startIndex;
            model.baseVertexLocation  = geom.baseVertex;
            model.vertexCount         = geom.vertexCount;
            model.numLODs=geom.numLODs;for(UINT i=0;i<geom.numLODs;++i)model.lods[i]=geom.lods[i];
        }
    }

    for(const auto& volume:loadedVolumes) post.AddVolume(volume);
    // Migrate the old built-in roof's implicit pyramid collider into explicit asset data once.
    // All normal collider creation uses mesh data and the saved shape, never the model name.
    if(loadedWorldRevision<4) for(auto& model:models)
        if(model.name=="House Roof" && model.physicsShapeType==0) model.physicsShapeType=4;
    m_renderer->UpdateModelMeshletRanges();
    m_renderer->InvalidateRaytracingScene();
    EngineScene::ReadHierarchy(sceneData,*m_renderer);
    ReadCameraDocument(sceneData);
    EnginePhysics::PhysicsSystem::Get().SyncAllActorsFromScene(m_renderer.get());
}

void Application::AddVehicleToProject(){
    for(auto& m:m_renderer->GetModels())if(m.vehicle.enabled){m.vehicle.Sanitize();m.physicsAngularDamping=.05f;EnginePhysics::PhysicsSystem::Get().ApplyModelPhysics(m_renderer.get(),m.modelId,false);SaveScene();return;}
    EnginePhysics::PhysicsSystem::Get().SpawnVehicle(m_renderer.get(),{6,1.2f,3});SaveScene();
}
#include "EditorWorkflowChecks.inl"

bool Application::Initialize() {
    m_gameplay=std::make_unique<EngineScene::GameplayRuntime>();
    LOG("App::Init DualSense controller (PS5 HID)");
    EngineInput::DualSenseController::Get().Initialize();

    LOG("App::Init DualShock 4 controller (PS4 HID)");
    EngineInput::DualShockController::Get().Initialize();

    LOG("App::Init DualSense audio (WASAPI & Bluetooth Speaker)");
    EngineAudio::DualSenseAudioSystem::Get().Initialize();

#if defined(ENGINE_PLATFORM_EPIC)
    LOG("App::Init Epic Games Online Services (EOS) SDK v1.19.1.2");
    EngineEpic::EpicSystem::Get().Initialize(EngineProject::current.epicProductId, EngineProject::current.epicSandboxId, EngineProject::current.epicDeploymentId, EngineProject::current.epicClientId, EngineProject::current.epicClientSecret);
#elif defined(ENGINE_PLATFORM_STEAM)
    LOG("App::Init Steamworks SDK v1.65");
#if defined(ENGINE_RELEASE)
    EngineSteam::SteamSystem::Get().Initialize(EngineProject::current.steamAppId, EngineProject::current.steamRestart);
#else
    EngineSteam::SteamSystem::Get().Initialize(EngineProject::current.steamAppId, false);
#endif
#endif

    LOG("App::Init Discord Social SDK (AppID: 1545464334577041498)");
    EngineDiscord::DiscordSystem::Get().Initialize(1545464334577041498ULL, "864fa07d1ae93e986301163995d678293ee19220378a9b2cab1d2102be950431");

    LOG("App::Init window");

    m_window = std::make_unique<Window>(m_width, m_height, m_title);
    if (!m_window->Initialize()) {
        LOG("Window::Init failed");
        return false;
    }

    LOG("App::Init camera");
    m_camera = std::make_unique<Camera>();

    LOG("App::Init renderer");
    m_renderer = std::make_unique<Renderer>(m_width, m_height);

    if (!m_renderer->Initialize(m_window->GetHWND(), m_camera.get())) {
        LOG("Renderer::Init failed");
        return false;
    }

#if defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
    if (m_window && m_renderer && m_renderer->GetPS4Backend()) {
        m_renderer->GetPS4Backend()->SetPS4Window(m_window->GetPS4Window());
    }
#endif

    CameraSystem::Get().Initialize(m_renderer.get());

    LOG("App::Init textures");
#if defined(ENGINE_EDITOR)
    m_window->SetCursorEnabled(!m_benchmarkFrameLimit || m_benchmarkEditorPreview);
#else
    m_window->SetCursorEnabled(false);
#endif
    
    // Preload a texture to use across models
    auto streamer = m_renderer->GetTextureStreamer();
    std::shared_ptr<Texture> demoTexture = nullptr;
    if (streamer) {
        demoTexture = streamer->LoadTexture("Content/DemoTexture/demo_texture.jpg");
    }

    LOG("App::Init loads scene");
    auto starterModels = m_renderer->GetModels();
    LoadScene();
    if (!m_loadedCleanWorld) {
        // One-time migration of the old demo world. Subsequent editor additions
        // are preserved in saves carrying WORLD_LAYOUT 3.
        m_renderer->GetModels() = std::move(starterModels);
        m_renderer->GetDynamicModels().clear();
        m_renderer->GetPointLights().clear();
        m_renderer->GetSpotLights().clear();
        m_renderer->UpdateModelMeshletRanges();
        m_renderer->InvalidateRaytracingScene();
        m_camera->SetPosition(XMFLOAT3(0.0f, 1.6f, -5.0f));
        auto& rain = EngineRain::RainSystem::Get();
        rain.GetWeatherSystem().SetPreset(EngineRain::WeatherPreset::Clear, rain.GetSettings());
        m_renderer->SetSunAzimuth(45.0f);
        m_renderer->SetSunElevation(45.0f);
        m_renderer->SetCloudDensity(0.20f);
        m_loadedCleanWorld = true;
        SaveScene();
    }

    // Default scene: Floor is created in Renderer::Initialize if scene empty
    if (m_renderer->GetModels().empty() && m_renderer->GetDynamicModels().empty()) {
        auto geomFloor = m_renderer->GetPrimitiveGeometry("Floor");
        if (geomFloor.valid) m_renderer->GetModels().emplace_back("Floor", geomFloor.indexCount, geomFloor.startIndex, geomFloor.baseVertex, geomFloor.vertexCount);
        m_renderer->UpdateModelMeshletRanges();
    }


    // Set default texture on renderer (used as fallback when textures are explicitly requested)
    if (demoTexture) {
        m_renderer->SetDefaultTexture(demoTexture);
    }

    LOG("App::Init sequencer");
    m_sequencer = std::make_unique<Sequencer>();
    m_sequencer->Load("sequence.txt");

    LOG("App::Init animation system");
    EngineAnim::AnimationSystem::Get().Initialize(m_renderer.get());

    LOG("App::Init physics system (PhysX)");
    EnginePhysics::PhysicsSystem::Get().Initialize(m_renderer.get());
    if(EngineProject::active){
        const auto& t=EngineProject::current.tuning;
        auto& physics=EnginePhysics::PhysicsSystem::Get();physics.SetGravity({t.gravityX,t.gravityY,t.gravityZ});physics.ConfigureSimulation(t.physicsHz,(int)t.maxSubSteps);
        auto& player=EnginePhysics::CharacterController::Get().GetConfig();
        player.walkSpeed=t.walkSpeed;player.runSpeed=t.runSpeed;player.crouchSpeed=t.crouchSpeed;player.jumpForce=t.jumpSpeed;player.pushForce=t.pushForce;player.gravity=t.playerGravity;
        m_renderer->SetFOV(t.fov);
    }
    LOG("App::Init Level & Map System (.level)");
    EngineLevel::LevelSystem::Get().Initialize();
    if(EngineProject::active && !EngineProject::current.startupLevel.empty()){EngineLevel::LevelSystem::Get().RequestLoadLevel(EngineProject::Utf8(EngineProject::current.Resolve(EngineProject::current.startupLevel)));if(!EngineLevel::LevelSystem::Get().ProcessPendingLoad(m_renderer.get(),m_camera.get()))return false;}

    auto& playerController = EnginePhysics::CharacterController::Get();
    if (playerController.IsEnabled()) {
        const auto& config = playerController.GetConfig();
        auto eye = m_camera->GetPosition();
        float floorY = 0.0f;
        for (const auto& model : m_renderer->GetModels())
            if (model.name == "Floor") floorY = model.position.y;
        float halfHeight = config.height * 0.5f + config.radius;
        float centerY = (std::max)(eye.y - (halfHeight - 0.15f), floorY + halfHeight + 0.05f);
        playerController.SetPosition(eye.x, centerY, eye.z);
        m_camera->SetControllerPose(playerController.GetEyePosition(), false);
    }

#if defined(ENGINE_EDITOR)
    m_camera->SetFreecamMode(!m_benchmarkFrameLimit || m_benchmarkEditorPreview);
    EnginePhysics::PhysicsSystem::Get().SetPaused(m_camera->IsFreecamMode());
#endif
    LOG("App::Init cloth physics system (XPBD)");
    EnginePhysics::ClothSystem::Get().Initialize(m_renderer.get());

    LOG("App::Init hair physics system (XPBD Cosserat)");
    EnginePhysics::HairSystem::Get().Initialize(m_renderer.get());

    LOG("App::Init 3D Spatial Audio Engine (miniaudio)");
    EngineAudio::AudioSystem::Get().Initialize();

    LOG("App::Init Multiplayer & Networking Manager");
    EngineNetwork::MultiplayerManager::Get().Initialize();

#ifndef ENGINE_RELEASE
    LOG("App::Init Content Browser (src/Content)");
    EngineUI::ContentBrowser::Get().Initialize(m_renderer.get());
#endif

    LOG("App::Init Gameplay Script System (Lua 5.4)");
    EngineScript::ScriptSystem::Get().Initialize();
#ifndef ENGINE_RELEASE
    // Auto-load demo script if it exists
    {
        const std::string demoScript = EnginePlatform::PlatformUtils::ResolveContentPath("scripts/demo_game.lua");
        {
            uint32_t scriptId = EngineScript::ScriptSystem::Get().LoadScript(demoScript);
            EngineScript::ScriptSystem::Get().CallOnStart(scriptId);
        }
    }
#endif

    std::cout << "[Init Progress] NavMesh..." << std::endl;
    EngineAI::NavigationSystem::Get().Initialize();

    std::cout << "[Init Progress] PostProcess..." << std::endl;
    EnginePostProcess::PostProcessSystem::Get().Initialize();

    std::cout << "[Init Progress] PrefabSystem..." << std::endl;
    EnginePrefab::PrefabSystem::Get().Initialize();

    std::cout << "[Init Progress] Profiler & DebugConsole..." << std::endl;
    EngineDebug::Profiler::Get().Initialize();
    EngineDebug::DebugConsole::Get().Initialize(m_renderer.get(), m_camera.get());

    std::cout << "[Init Progress] InputMapping..." << std::endl;
    EngineInput::InputMapping::Get().Initialize();

    std::cout << "[Init Progress] Window Show..." << std::endl;
    m_window->Show();

    std::cout << "[Init Progress] WebMenu..." << std::endl;
    EngineUI::WebMenuSystem::Get().Initialize(m_window->GetHWND(), m_renderer.get(), m_window.get());
    
    std::cout << "[Init Progress] Application::Initialize COMPLETED SUCCESSFULLY!" << std::endl;
    return true;
}

int Application::Run() {
    std::cout << "[Idontknow PS4] Application::Run entered! Starting main render loop..." << std::endl;
    Timer timer;
    unsigned benchmarkFrames = 0;
#if defined(_WIN32)
    if (m_benchmarkFrameLimit && GetEnvironmentVariableW(L"IDK_BENCHMARK_DYNAMIC_GEOMETRY", nullptr, 0) > 0) {
        const std::vector<Renderer::Vertex> vertices = {
            {{-5.f,1.f,0.f},{0.f,0.f,-1.f},{1.f,1.f,0.f,0.f}},
            {{-3.f,1.f,0.f},{0.f,0.f,-1.f},{1.f,1.f,1.f,0.f}},
            {{-3.f,3.f,0.f},{0.f,0.f,-1.f},{1.f,1.f,1.f,1.f}},
            {{-5.f,3.f,0.f},{0.f,0.f,-1.f},{1.f,1.f,0.f,1.f}}
        };
        Renderer::PBRMaterial material;material.albedo={0.15f,0.9f,0.2f};material.roughness=0.7f;
        m_renderer->CreateDynamicClothModel("DXR dynamic geometry test",vertices,{0,1,2,0,2,3},material);
    }
#endif
    std::ofstream benchmark;
    if (m_benchmarkFrameLimit) {
#if defined(_WIN32)
        Renderer::SetInteractiveErrors(false);
#endif
        std::filesystem::path output = "benchmark.csv";
#if defined(_WIN32)
        wchar_t executable[32768] {};
        GetModuleFileNameW(nullptr, executable, 32768);
        output = EngineDiagnostics::Directory() / L"benchmark.csv";
#endif
        benchmark.open(output);
        benchmark << "frame,frame_ms,update_ms,render_ms,gpu_wait_ms,gpu_ms,render_width,render_height,raytracing\n";
        if (m_benchmarkRain) EngineRain::RainSystem::Get().SetWeatherPreset(EngineRain::WeatherPreset::Rain);
        if (m_benchmarkDisableVSync) m_renderer->SetVSyncEnabled(false);
        m_renderer->m_dxrEnabled = !m_benchmarkDisableRayTracing;
        if (m_benchmarkNativeResolution) {
            m_renderer->SetUpscaleTechnique(UpscaleTechnique::None);
            m_renderer->SetTAAEnabled(false);
        }
        if (m_benchmarkAntialiasing >= 0 && !EngineUI::ApplyAntiAliasing(*m_renderer, m_benchmarkAntialiasing)) return 4;
        if (m_benchmarkUpscaler >= 0) {
            if (m_benchmarkUpscaler > 3 || m_benchmarkQuality < 0 || m_benchmarkQuality > 4) return 4;
            m_renderer->SetUpscaleTechnique(static_cast<UpscaleTechnique>(m_benchmarkUpscaler));
            m_renderer->SetUpscaleQuality(static_cast<UpscaleQuality>(m_benchmarkQuality));
            m_renderer->SetTAAEnabled(false);
        }
        if (m_benchmarkPathTracing) m_renderer->SetPathTracingEnabled(true);
        if(m_benchmarkFSRFG)m_renderer->SetFSRFrameGenEnabled(true);
        if(m_benchmarkXeFG)m_renderer->SetXeSSFrameGenEnabled(true);
        if(m_benchmarkRR){EngineUI::ApplyAntiAliasing(*m_renderer,2);m_renderer->SetDLSSRayReconstructionActive(true);}
        if(m_benchmarkLatency==0){m_renderer->SetReflexMode(ReflexMode::Off);m_renderer->SetXeLLEnabled(false);}
        if(m_benchmarkLatency==1)m_renderer->SetReflexMode(ReflexMode::LowLatency);
        if(m_benchmarkLatency==2)m_renderer->SetXeLLEnabled(true);
        if(m_benchmarkLightingFixture) {
            m_renderer->GetPointLights().clear(); m_renderer->GetSpotLights().clear();
            Renderer::PointLight point;point.name="Editor lighting test";point.position={-3,3,2};
            point.color={.2f,.5f,1};point.intensity=80;point.range=20;m_renderer->GetPointLights().push_back(point);
            Renderer::SpotLight spot;spot.name="Editor spot test";spot.position={3,6,1};spot.direction={-.2f,-.7f,1};
            spot.color={1,.35f,.1f};spot.intensity=120;spot.range=25;spot.innerAngle=20;spot.outerAngle=50;
            m_renderer->GetSpotLights().push_back(spot);
            m_renderer->SetLightColor({.03f,.03f,.03f});m_renderer->SetCloudDensity(0);
            if(m_benchmarkEditorPreview){
                auto& camera=CameraSystem::Get().CreateCamera("Editor gizmo test",CameraMode::StaticFixed,{0,2,0},0,25,65);
                camera.isWindowOpen=false;
            }
        }
    }
    if(m_benchmarkFrameLimit&&m_benchmarkTessellation){
        for(auto& model:m_renderer->GetModels()){model.material.tessellation.enabled=true;model.material.tessellation.factor=8;model.material.tessellation.smoothing=1;}
        for(auto& model:m_renderer->GetDynamicModels()){model.material.tessellation.enabled=true;model.material.tessellation.factor=8;model.material.tessellation.smoothing=1;}
    }
    if(m_benchmarkFrameLimit && m_benchmarkGI>=0){m_renderer->SetSSGIEnabled(m_benchmarkGI>0);m_renderer->SetSSGIIntensity(m_benchmarkGI);}
#if defined(ENGINE_EDITOR)
    if(m_benchmarkEditorWorkflow&&m_benchmarkFrameLimit&&!RunEditorWorkflowChecks())return 8;
#endif
    bool prevF11 = false;
    bool prevTab = false;
    bool prevEscape = false;

    LOG("App::Run starting loop");
#if defined(_WIN32)
    MSG msg = {};
    while (msg.message != WM_QUIT && !EngineLog::closeRequested.load()) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) break;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (msg.message == WM_QUIT) break;
        {
#else
    while (m_window && m_window->IsOpen()) {
        m_window->ProcessEvents();
        {
#endif
            timer.Tick();
            const float realFrameTime = timer.GetDeltaTime();
            const auto frameStart = std::chrono::steady_clock::now();
            if (m_renderer->IsDeviceRemoved()) return 3;
            EngineDebug::Profiler::Get().BeginFrame();
            // Simulation systems update shared GPU upload buffers. Protect those
            // writes before Update(), not only later inside Renderer::Render().
            m_renderer->SynchronizeForUpdate();
            m_renderer->BeginSimulationFrame();
            float dt = (std::min)(timer.GetDeltaTime(), 0.1f) * EngineDebug::DebugConsole::Get().GetTimeScale();

            static bool s_cursorUnlockedForConsole = false;
#if defined(_WIN32)
            static bool s_prevTilde = false;
            bool currentTilde = m_window->IsKeyDown(VK_OEM_3);
            if (currentTilde && !s_prevTilde) { // ~ key
                EngineDebug::DebugConsole::Get().ToggleConsole();
                if (EngineDebug::DebugConsole::Get().IsOpen()) {
                    if (!m_window->IsCursorEnabled()) {
                        s_cursorUnlockedForConsole = true;
                        m_window->SetCursorEnabled(true);
                        m_window->ResetMouseDelta();
                    }
                } else if (s_cursorUnlockedForConsole) {
                    s_cursorUnlockedForConsole = false;
                    m_window->SetCursorEnabled(false);
                    m_window->ResetMouseDelta();
                }
            }
            s_prevTilde = currentTilde;

            static bool s_prevF3 = false;
            bool currentF3 = m_window->IsKeyDown(VK_F3) && ((GetKeyState(VK_SHIFT) & 0x8000) == 0);
            if (currentF3 && !s_prevF3) { // F3 key: toggle FPS & Telemetry HUD
                EngineDebug::DebugConsole::Get().SetFpsOverlayEnabled(!EngineDebug::DebugConsole::Get().IsFpsOverlayEnabled());
            }
            s_prevF3 = currentF3;
#endif
            if (s_cursorUnlockedForConsole && !EngineDebug::DebugConsole::Get().IsOpen() && !EnginePhysics::PhysicsSystem::Get().IsDriving()) {
                s_cursorUnlockedForConsole = false;
                m_window->SetCursorEnabled(false);
                m_window->ResetMouseDelta();
            }

            // Safe In-Game Runtime Map Loading (Executed before command list recording)
            if (EngineLevel::LevelSystem::Get().HasPendingLoad()) {
                EngineLevel::LevelSystem::Get().ProcessPendingLoad(m_renderer.get(), m_camera.get());
            }

            // FPS Calculation
            static float frameCount = 0;
            static float timeAccumulator = 0.0f;
            frameCount++;
            timeAccumulator += realFrameTime;
            if (timeAccumulator >= 0.5f) {
                m_fps = frameCount / timeAccumulator;
                frameCount = 0;
                timeAccumulator = 0.0f;
            }

            // Update DualSense & DualShock 4 Controllers
            auto& dualSense = EngineInput::DualSenseController::Get();
            dualSense.Update(dt);

            auto& dualShock = EngineInput::DualShockController::Get();
            dualShock.Update(dt);

            // Update Action-Based Input Mapping System (Unified Keyboard/Mouse + DualSense/DualShock)
            EngineInput::InputMapping::Get().Update(dt, m_window.get(), &dualSense, &dualShock);

            // Auto-detect DualSense audio endpoint (physical USB audio):
            static float s_audioHotplugTimer = 0.0f;
            s_audioHotplugTimer += dt;
            if (s_audioHotplugTimer >= 1.0f) {
                s_audioHotplugTimer = 0.0f;
                auto& audioSys = EngineAudio::DualSenseAudioSystem::Get();
                if (dualSense.IsConnected()) {
                    audioSys.RefreshAudioDevices();
                    const auto& devs = audioSys.GetAudioDevices();
                    int dsIndex = -1;
                    for (size_t i = 0; i < devs.size(); ++i) {
                        if (devs[i].isDualSense) {
                            dsIndex = static_cast<int>(i);
                            break;
                        }
                    }

                    if (dsIndex >= 0) {
                        // DualSense physical USB audio device is present!
                        if (!audioSys.IsDualSenseDevice() || !audioSys.IsDeviceOpen()) {
                            audioSys.SetSelectedDevice(dsIndex);
                        }
                    } else {
                        // DualSense connected via Bluetooth (standard Bluetooth audio endpoint or system default).
                        // If previous DualSense device disappeared or audio is closed, switch to default
                        if (audioSys.IsDualSenseDevice() || !audioSys.IsDeviceOpen()) {
                            if (audioSys.IsBluetoothAudioEnabled()) {
                                audioSys.SelectBluetoothDevice();
                            } else {
                                audioSys.SelectDefaultDevice();
                            }
                        }
                    }
                } else if (!audioSys.IsDeviceOpen()) {
                    static bool s_triedDefaultAudio = false;
                    if (!s_triedDefaultAudio) { s_triedDefaultAudio = true; audioSys.SelectDefaultDevice(); }
                }
            }

            // Update Platform Online Services (Callbacks, Rich Presence, Overlay)
#if defined(ENGINE_PLATFORM_EPIC)
            EngineEpic::EpicSystem::Get().Update(dt);
            
            // Shift + F3 hotkey to open Epic Overlay / Launcher
            static bool prevShiftF3 = false;
            bool currentShiftF3 = ((GetKeyState(VK_SHIFT) & 0x8000) != 0) && m_window->IsKeyDown(VK_F3);
            if (currentShiftF3 && !prevShiftF3) {
                EngineEpic::EpicSystem::Get().OpenOverlay("Friends");
            }
            prevShiftF3 = currentShiftF3;
#else
            EngineSteam::SteamSystem::Get().Update(dt);

            // Shift + Tab hotkey to open Steam Overlay
            static bool prevShiftTab = false;
            bool currentShiftTab = ((GetKeyState(VK_SHIFT) & 0x8000) != 0) && m_window->IsKeyDown(VK_TAB);
            if (currentShiftTab && !prevShiftTab) {
                EngineSteam::SteamSystem::Get().OpenOverlay();
            }
            prevShiftTab = currentShiftTab;
#endif

            // Update Discord Social SDK (Callbacks & Presence)
            EngineDiscord::DiscordSystem::Get().Update(dt);

#ifndef ENGINE_RELEASE
            // Toggle ImGui UI on TAB or DualSense/DualShock controller buttons:
            // - Touchpad click
            // - Options button
            // - L3 + R3 combination
            bool padToggleEditor = dualSense.IsButtonPressed(EngineInput::DualSenseButton::Options) ||
                                   dualSense.IsButtonPressed(EngineInput::DualSenseButton::Touchpad) ||
                                   (dualSense.IsButtonDown(EngineInput::DualSenseButton::L3) && dualSense.IsButtonPressed(EngineInput::DualSenseButton::R3)) ||
                                   (dualSense.IsButtonDown(EngineInput::DualSenseButton::R3) && dualSense.IsButtonPressed(EngineInput::DualSenseButton::L3)) ||
                                   (dualSense.IsButtonDown(EngineInput::DualSenseButton::DPadUp) && dualSense.IsButtonPressed(EngineInput::DualSenseButton::Triangle)) ||
                                   dualShock.IsButtonPressed(EngineInput::DualShockButton::DS_Options) ||
                                   dualShock.IsButtonPressed(EngineInput::DualShockButton::DS_Touchpad) ||
                                   (dualShock.IsButtonDown(EngineInput::DualShockButton::DS_L3) && dualShock.IsButtonPressed(EngineInput::DualShockButton::DS_R3)) ||
                                   (dualShock.IsButtonDown(EngineInput::DualShockButton::DS_R3) && dualShock.IsButtonPressed(EngineInput::DualShockButton::DS_L3)) ||
                                   (dualShock.IsButtonDown(EngineInput::DualShockButton::DS_DPadUp) && dualShock.IsButtonPressed(EngineInput::DualShockButton::DS_Triangle));

            bool currentTab = m_window->IsKeyDown(VK_TAB) || padToggleEditor;
            if (currentTab && !prevTab) {
                bool newState = s_cursorUnlockedForConsole ? true : !m_window->IsCursorEnabled();
                s_cursorUnlockedForConsole = false;
                m_window->SetCursorEnabled(newState);
                m_camera->SetFreecamMode(newState);
                m_window->ResetMouseDelta();
            }
            prevTab = currentTab;

            // Circle button closes ImGui editor if open
            if (m_window->IsCursorEnabled() && (dualSense.IsButtonPressed(EngineInput::DualSenseButton::Circle) || dualShock.IsButtonPressed(EngineInput::DualShockButton::DS_Circle))) {
                s_cursorUnlockedForConsole = false;
                m_window->SetCursorEnabled(false);
                m_camera->SetFreecamMode(false);
                m_window->ResetMouseDelta();
            }
#endif

#if !defined(ENGINE_PLATFORM_PS4) && !defined(__PS4__) && !defined(__ORBIS__)
            // Close Developer Console on ESC if open, otherwise toggle HTML5/JS Main Menu (PC only)
            bool currentEscape = m_window->IsKeyDown(VK_ESCAPE);
            if (currentEscape && !prevEscape) {
                if (EngineDebug::DebugConsole::Get().IsOpen()) {
                    EngineDebug::DebugConsole::Get().SetOpen(false);
                    if (s_cursorUnlockedForConsole) {
                        s_cursorUnlockedForConsole = false;
                        m_window->SetCursorEnabled(false);
                        m_window->ResetMouseDelta();
                    }
                } else {
                    EngineUI::WebMenuSystem::Get().ToggleMenu();
                }
            }
            prevEscape = currentEscape;
#endif

            // F7 Hotkey: Cycle to next available map during active gameplay
            static bool s_prevF7 = false;
            bool currentF7 = m_window->IsKeyDown(VK_F7);
            if (currentF7 && !s_prevF7) {
                EngineLevel::LevelSystem::Get().CycleToNextLevel(m_renderer.get(), m_camera.get());
            }
            s_prevF7 = currentF7;

            // Hotkey 'C': Cycle cameras (Player FPS, Freecam, CCTV surveillance feeds, Orbit)
            static bool s_prevKeyC = false;
            bool currentKeyC = m_window->IsKeyDown('C');
#ifndef ENGINE_RELEASE
            if (ImGui::GetIO().WantTextInput) currentKeyC = false;
#endif
            if (currentKeyC && !s_prevKeyC) {
                CameraSystem::Get().CycleNextCamera(m_camera.get(), m_renderer.get());
            }
            s_prevKeyC = currentKeyC;

            // Toggle Freecam mode on DualSense Create button or DualShock Share button
            if (dualSense.IsButtonPressed(EngineInput::DualSenseButton::Create) || dualShock.IsButtonPressed(EngineInput::DualShockButton::DS_Share)) {
                bool newFreecam = !m_camera->IsFreecamMode();
                m_camera->SetFreecamMode(newFreecam);
            }
            
            // Allow turning cursor ON/OFF with RMB in freecam only when clicking the 3D viewport (outside ImGui)
            static bool s_rmbViewportActive = false;
            if (m_camera->IsFreecamMode()) {
                bool isRmbDown = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
                if (isRmbDown && !s_rmbViewportActive) {
                    // Only start RMB camera rotation if mouse is NOT hovering over any ImGui window
#ifndef ENGINE_RELEASE
                    if (!ImGui::GetIO().WantCaptureMouse)
#endif
                    {
                        s_rmbViewportActive = true;
                    m_window->SetCursorEnabled(false);
                        m_window->ResetMouseDelta();
                    }
                } else if (!isRmbDown && s_rmbViewportActive) {
                    s_rmbViewportActive = false;
                    m_window->SetCursorEnabled(true);
                }
            } else {
                s_rmbViewportActive = false;
            }
            
            // Sync collision state from all scene models before input handling
            auto& models = m_renderer->GetModels();
            auto& dynModels = m_renderer->GetDynamicModels();
            bool floorEnabled = false;
            std::vector<Camera::CollisionBox> colBoxes;

            for (const auto& model : models) {
                bool isFloor = (model.name == "Floor" ||
                                model.name == "Underworld_Chunk_Platform" ||
                                model.name == "Underworld_Continent" ||
                                model.name.find("Platform") != std::string::npos ||
                                model.name.find("Floor") != std::string::npos ||
                                model.name.find("Continent") != std::string::npos);
                if (isFloor && model.hasCollision && model.visible) {
                    floorEnabled = true;
                } else if (model.visible && model.hasCollision) {
                    Camera::CollisionBox box;
                    float hx = (std::max)(model.scale.x * 0.5f, 0.1f);
                    float hy = (std::max)(model.scale.y * 0.5f, 0.1f);
                    float hz = (std::max)(model.scale.z * 0.5f, 0.1f);

                    box.minPoint = XMFLOAT3(model.position.x - hx, model.position.y - hy, model.position.z - hz);
                    box.maxPoint = XMFLOAT3(model.position.x + hx, model.position.y + hy, model.position.z + hz);
                    EnginePhysics::PhysicsSystem::Get().GetColliderBounds(model.modelId,box.minPoint,box.maxPoint);
                    colBoxes.push_back(box);
                }
            }

            for (const auto& dm : dynModels) {
                if (dm.visible && dm.hasCollision) {
                    Camera::CollisionBox box;
                    float hx = (std::max)(dm.scale.x * 0.5f, 0.2f);
                    float hy = (std::max)(dm.scale.y * 0.5f, 0.5f);
                    float hz = (std::max)(dm.scale.z * 0.5f, 0.2f);
                    box.minPoint = XMFLOAT3(dm.position.x - hx, dm.position.y - hy, dm.position.z - hz);
                    box.maxPoint = XMFLOAT3(dm.position.x + hx, dm.position.y + hy, dm.position.z + hz);
                    EnginePhysics::PhysicsSystem::Get().GetColliderBounds(dm.modelId,box.minPoint,box.maxPoint);
                    colBoxes.push_back(box);
                }
            }

            auto& rainSys = EngineRain::RainSystem::Get();
            rainSys.Update(dt, m_camera->GetPosition(), colBoxes);
            if (rainSys.GetSettings().enabled || rainSys.GetSettings().weather != EngineRain::WeatherPreset::Clear) {
                const auto& weather = rainSys.GetWeatherSystem();
                m_renderer->SetCloudDensity(weather.GetCloudDensity());
                m_renderer->SetVolumetricFogDensity(weather.GetFogDensity());
                m_renderer->SetVolumetricFogEnabled(true);
                m_renderer->SetSkyColor(weather.GetSkyColor());
                m_renderer->SetLightColor(weather.GetLightColor());
                m_renderer->SetSunElevation(weather.GetSunElevation());
                float rainIntensity = rainSys.GetSettings().enabled ? rainSys.GetSettings().intensity : 0.0f;
                m_renderer->SetRainIntensity(rainIntensity);
            }
            m_camera->SetCollisionBoxes(std::move(colBoxes));
            m_camera->SetCollisionEnabled(true);
#if defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
            floorEnabled = true;
#endif
            m_camera->SetFloorEnabled(floorEnabled);

            if ((!m_window->IsCursorEnabled() || (m_camera->IsFreecamMode() && s_rmbViewportActive)) && !EngineDebug::DebugConsole::Get().IsOpen()) {
                // Update Camera Input
                float baseSpeed = m_camera->IsFreecamMode() ? 10.0f : 2.5f; 
                
                // Crouch: Ctrl, C, Circle, or R3 (Right Stick Click)
                static bool s_gamepadCrouchToggled = false;
                if (dualSense.IsConnected()) {
                    if (dualSense.IsButtonPressed(EngineInput::DualSenseButton::R3)) {
                        s_gamepadCrouchToggled = !s_gamepadCrouchToggled;
                    }
                    if (dualSense.IsButtonPressed(EngineInput::DualSenseButton::Cross) || dualSense.IsButtonPressed(EngineInput::DualSenseButton::L3)) {
                        s_gamepadCrouchToggled = false; // Jumping or sprinting stands up
                    }
                }
                if (m_window->IsKeyDown(VK_SPACE)) {
                    s_gamepadCrouchToggled = false;
                }

                bool isCrouching = m_window->IsKeyDown(VK_CONTROL) || m_window->IsKeyDown('C') ||
                                   dualSense.IsButtonDown(EngineInput::DualSenseButton::Circle) ||
                                   dualSense.IsButtonDown(EngineInput::DualSenseButton::R3) ||
                                   dualShock.IsButtonDown(EngineInput::DualShockButton::DS_Circle) ||
                                   dualShock.IsButtonDown(EngineInput::DualShockButton::DS_R3) ||
                                   s_gamepadCrouchToggled;
                if (!m_camera->IsFreecamMode()) m_camera->SetCrouching(isCrouching);
                
                // DualSense / DualShock Left Analog Stick Movement
                float padLX = dualSense.GetLeftStickX();
                float padLY = dualSense.GetLeftStickY();
                if (std::abs(padLX) <= 0.001f && std::abs(padLY) <= 0.001f && dualShock.IsConnected()) {
                    padLX = dualShock.GetLeftStickX();
                    padLY = dualShock.GetLeftStickY();
                }

                // Click-to-Sprint on L3 (Left Stick Click): click once to start sprinting immediately without holding
                static bool s_gamepadSprintToggled = false;
                if (dualSense.IsConnected() || dualShock.IsConnected()) {
                    if (dualSense.IsButtonPressed(EngineInput::DualSenseButton::L3) || dualShock.IsButtonPressed(EngineInput::DualShockButton::DS_L3)) {
                        s_gamepadSprintToggled = !s_gamepadSprintToggled;
                        if (s_gamepadSprintToggled) {
                            s_gamepadCrouchToggled = false;
                        }
                    }
                }

                bool hasMoveInput = (std::abs(padLX) > 0.05f || std::abs(padLY) > 0.05f) ||
                                    m_window->IsKeyDown('W') || m_window->IsKeyDown('S') ||
                                    m_window->IsKeyDown('A') || m_window->IsKeyDown('D') ||
                                    dualSense.IsButtonDown(EngineInput::DualSenseButton::DPadUp) ||
                                    dualSense.IsButtonDown(EngineInput::DualSenseButton::DPadDown) ||
                                    dualSense.IsButtonDown(EngineInput::DualSenseButton::DPadLeft) ||
                                    dualSense.IsButtonDown(EngineInput::DualSenseButton::DPadRight) ||
                                    dualShock.IsButtonDown(EngineInput::DualShockButton::DS_DPadUp) ||
                                    dualShock.IsButtonDown(EngineInput::DualShockButton::DS_DPadDown) ||
                                    dualShock.IsButtonDown(EngineInput::DualShockButton::DS_DPadLeft) ||
                                    dualShock.IsButtonDown(EngineInput::DualShockButton::DS_DPadRight);

                if (!hasMoveInput || isCrouching) {
                    s_gamepadSprintToggled = false;
                }

                bool isSprinting = (s_gamepadSprintToggled || m_window->IsKeyDown(VK_SHIFT) ||
                                    dualSense.IsButtonDown(EngineInput::DualSenseButton::L3) ||
                                    dualShock.IsButtonDown(EngineInput::DualShockButton::DS_L3)) && !isCrouching;

                float speedMultiplier = 1.0f;
                if (isCrouching && !m_camera->IsFreecamMode()) speedMultiplier = 0.4f;
                else if (isSprinting) speedMultiplier = 2.2f;

                float speed = baseSpeed * speedMultiplier * dt;
                
                auto& cct = EnginePhysics::CharacterController::Get();
                if (!m_camera->IsFreecamMode() && cct.IsEnabled()) {
                    DirectX::XMFLOAT3 fwd = m_camera->GetForwardVector();
                    fwd.y = 0.0f;
                    float fwdLen = std::sqrt(fwd.x * fwd.x + fwd.z * fwd.z);
                    if (fwdLen > 0.001f) { fwd.x /= fwdLen; fwd.z /= fwdLen; }

                    DirectX::XMFLOAT3 rgt = m_camera->GetRightVector();
                    rgt.y = 0.0f;
                    float rgtLen = std::sqrt(rgt.x * rgt.x + rgt.z * rgt.z);
                    if (rgtLen > 0.001f) { rgt.x /= rgtLen; rgt.z /= rgtLen; }

                    DirectX::XMFLOAT3 moveDir = { 0.0f, 0.0f, 0.0f };
                    if (m_window->IsKeyDown('W')) { moveDir.x += fwd.x; moveDir.z += fwd.z; }
                    if (m_window->IsKeyDown('S')) { moveDir.x -= fwd.x; moveDir.z -= fwd.z; }
                    if (m_window->IsKeyDown('D')) { moveDir.x += rgt.x; moveDir.z += rgt.z; }
                    if (m_window->IsKeyDown('A')) { moveDir.x -= rgt.x; moveDir.z -= rgt.z; }

                    if (std::abs(padLY) > 0.05f) { moveDir.x += fwd.x * padLY; moveDir.z += fwd.z * padLY; }
                    if (std::abs(padLX) > 0.05f) { moveDir.x += rgt.x * padLX; moveDir.z += rgt.z * padLX; }

                    if (dualSense.IsButtonDown(EngineInput::DualSenseButton::DPadUp)) { moveDir.x += fwd.x; moveDir.z += fwd.z; }
                    if (dualSense.IsButtonDown(EngineInput::DualSenseButton::DPadDown)) { moveDir.x -= fwd.x; moveDir.z -= fwd.z; }
                    if (dualSense.IsButtonDown(EngineInput::DualSenseButton::DPadRight)) { moveDir.x += rgt.x; moveDir.z += rgt.z; }
                    if (dualSense.IsButtonDown(EngineInput::DualSenseButton::DPadLeft)) { moveDir.x -= rgt.x; moveDir.z -= rgt.z; }

                    float moveLen = std::sqrt(moveDir.x * moveDir.x + moveDir.z * moveDir.z);
                    if (moveLen > 0.001f) { moveDir.x /= moveLen; moveDir.z /= moveLen; }

                    bool wantJump = m_window->IsKeyDown(VK_SPACE) || dualSense.IsButtonPressed(EngineInput::DualSenseButton::Cross);
                    cct.Update(dt, moveDir, wantJump, isSprinting, isCrouching);

                    DirectX::XMFLOAT3 eyePos = cct.GetEyePosition();
                    m_camera->SetControllerPose(eyePos, cct.IsGrounded());

                    // Procedural footstep audio synthesis while walking on ground
                    static float s_stepTimer = 0.0f;
                    if (cct.IsGrounded() && cct.GetCurrentSpeed() > 0.8f) {
                        s_stepTimer += dt * (isSprinting ? 2.0f : 1.2f);
                        if (s_stepTimer >= 0.5f) {
                            s_stepTimer = 0.0f;
                            EngineAudio::AudioSystem::Get().PlayProceduralStep(0.2f, isSprinting ? 1.25f : 0.95f);
                        }
                    } else {
                        s_stepTimer = 0.25f;
                    }
                } else {
                    float moveFwd = 0.0f;
                    float moveRight = 0.0f;

                    if (std::abs(padLY) > 0.15f) moveFwd += padLY;
                    if (std::abs(padLX) > 0.15f) moveRight += padLX;

                    if (m_window->IsKeyDown('W')) moveFwd += 1.0f;
                    if (m_window->IsKeyDown('S')) moveFwd -= 1.0f;
                    if (m_window->IsKeyDown('D')) moveRight += 1.0f;
                    if (m_window->IsKeyDown('A')) moveRight -= 1.0f;

                    // DualSense D-Pad Movement
                    if (dualSense.IsButtonDown(EngineInput::DualSenseButton::DPadUp)) moveFwd += 1.0f;
                    if (dualSense.IsButtonDown(EngineInput::DualSenseButton::DPadDown)) moveFwd -= 1.0f;
                    if (dualSense.IsButtonDown(EngineInput::DualSenseButton::DPadRight)) moveRight += 1.0f;
                    if (dualSense.IsButtonDown(EngineInput::DualSenseButton::DPadLeft)) moveRight -= 1.0f;

                    if (std::abs(moveFwd) > 0.001f) m_camera->MoveForward(moveFwd * speed);
                    if (std::abs(moveRight) > 0.001f) m_camera->MoveRight(moveRight * speed);

                    if (m_camera->IsFreecamMode()) {
                        if (m_window->IsKeyDown('E') || m_window->IsKeyDown(VK_SPACE) ||
                            dualSense.IsButtonDown(EngineInput::DualSenseButton::Cross) || dualSense.IsButtonDown(EngineInput::DualSenseButton::R1) ||
                            dualShock.IsButtonDown(EngineInput::DualShockButton::DS_Cross) || dualShock.IsButtonDown(EngineInput::DualShockButton::DS_R1)) m_camera->MoveUp(speed);
                        if (m_window->IsKeyDown('Q') || m_window->IsKeyDown(VK_CONTROL) ||
                            dualSense.IsButtonDown(EngineInput::DualSenseButton::Square) || dualSense.IsButtonDown(EngineInput::DualSenseButton::L1) ||
                            dualShock.IsButtonDown(EngineInput::DualShockButton::DS_Square) || dualShock.IsButtonDown(EngineInput::DualShockButton::DS_L1)) m_camera->MoveUp(-speed);
                    } else {
                        if (m_window->IsKeyDown(VK_SPACE) ||
                            dualSense.IsButtonPressed(EngineInput::DualSenseButton::Cross) ||
                            dualShock.IsButtonPressed(EngineInput::DualShockButton::DS_Cross)) m_camera->Jump(6.0f);
                    }
                }

                // Press 'G' or DualSense/DualShock R2 to shoot physics projectile
                static float s_prevPadR2 = 0.0f;
                float currentPadR2 = dualSense.IsConnected() ? dualSense.GetRightTrigger() : dualShock.GetRightTrigger();
                bool padR2Triggered = (!m_window->IsCursorEnabled() || m_camera->IsFreecamMode()) &&
                    ((currentPadR2 > 0.85f && s_prevPadR2 <= 0.85f) ||
                     dualSense.IsButtonPressed(EngineInput::DualSenseButton::R2Button) ||
                     dualShock.IsButtonPressed(EngineInput::DualShockButton::DS_R2Button));
                s_prevPadR2 = currentPadR2;

                static float shootCooldown = 0.0f;
                if (shootCooldown > 0.0f) {
                    shootCooldown -= dt;
                }

                bool currentG = (!m_window->IsCursorEnabled() || m_camera->IsFreecamMode()) && (m_window->IsKeyDown('G') || padR2Triggered);
                static bool prevG = false;
                if (currentG && !prevG && shootCooldown <= 0.0f) {
                    shootCooldown = 0.2f;
                    EnginePhysics::PhysicsSystem::Get().ShootProjectile(m_renderer.get(), m_camera->GetPosition(), m_camera->GetForwardVector());
                    // Tactile recoil rumble kick
                    if (dualSense.IsConnected()) {
                        dualSense.PlayRumble(0.75f, 0.4f, 0.12f);
                        dualSense.PlayTriggerVibration(false, true, 255, 45, 0.18f);
                    } else if (dualShock.IsConnected()) {
                        dualShock.PlayRumble(0.75f, 0.4f, 0.12f);
                    }
                    // Play gunshot sound directly through DualSense speaker
                    if (EngineAudio::DualSenseAudioSystem::Get().IsGameplaySoundsEnabled()) {
                        EngineAudio::DualSenseAudioSystem::Get().PlayShootSound(1.0f);
                    }
                }
                prevG = currentG;
            }

            // Update Physical Melting & Liquefaction Simulation (Floor Liquefaction)
            bool anyMeltingActive = false;
            for (auto& model : m_renderer->GetModels()) {
                if (model.isMelting && model.meltProgress < 1.0f) {
                    anyMeltingActive = true;
                    model.meltProgress = (std::min)(1.0f, model.meltProgress + dt * model.meltSpeed);
                    float t = model.meltProgress * model.meltProgress * (3.0f - 2.0f * model.meltProgress);

                    float bottomY = model.initialPosition.y - model.initialScale.y * 0.5f;
                    float targetHeight = 0.035f;
                    float currentHeight = (std::lerp)(model.initialScale.y, targetHeight, t);

                    model.scale.y = currentHeight;
                    model.position.y = bottomY + currentHeight * 0.5f;

                    // Conservation of volume: horizontal radial puddle spreading
                    float spreadFactor = (std::sqrt)(model.initialScale.y / (std::max)(currentHeight, 0.005f));
                    spreadFactor = (std::min)(spreadFactor, 4.0f);
                    model.scale.x = model.initialScale.x * (std::lerp)(1.0f, spreadFactor, t);
                    model.scale.z = model.initialScale.z * (std::lerp)(1.0f, spreadFactor, t);

                    // Material transitions into high-gloss liquid puddle
                    model.material.roughness = (std::lerp)(model.initialRoughness, 0.03f, t);
                    model.material.reflectance = (std::lerp)(model.material.reflectance, 0.95f, t);

                    XMMATRIX S = XMMatrixScaling(model.scale.x, model.scale.y, model.scale.z);
                    XMMATRIX R = XMMatrixRotationRollPitchYaw(XMConvertToRadians(model.rotation.x), XMConvertToRadians(model.rotation.y), XMConvertToRadians(model.rotation.z));
                    XMMATRIX T = XMMatrixTranslation(model.position.x, model.position.y, model.position.z);
                    XMStoreFloat4x4(&model.worldMatrix, S * R * T);
                }
            }

            for (auto& dm : m_renderer->GetDynamicModels()) {
                if (dm.isMelting && dm.meltProgress < 1.0f) {
                    anyMeltingActive = true;
                    dm.meltProgress = (std::min)(1.0f, dm.meltProgress + dt * dm.meltSpeed);
                    float t = dm.meltProgress * dm.meltProgress * (3.0f - 2.0f * dm.meltProgress);

                    float bottomY = dm.initialPosition.y - dm.initialScale.y * 0.5f;
                    float targetHeight = 0.035f;
                    float currentHeight = (std::lerp)(dm.initialScale.y, targetHeight, t);

                    dm.scale.y = currentHeight;
                    dm.position.y = bottomY + currentHeight * 0.5f;

                    float spreadFactor = (std::sqrt)(dm.initialScale.y / (std::max)(currentHeight, 0.005f));
                    spreadFactor = (std::min)(spreadFactor, 4.0f);
                    dm.scale.x = dm.initialScale.x * (std::lerp)(1.0f, spreadFactor, t);
                    dm.scale.z = dm.initialScale.z * (std::lerp)(1.0f, spreadFactor, t);

                    dm.material.roughness = (std::lerp)(dm.initialRoughness, 0.03f, t);
                    dm.material.reflectance = (std::lerp)(dm.material.reflectance, 0.95f, t);

                    XMMATRIX S = XMMatrixScaling(dm.scale.x, dm.scale.y, dm.scale.z);
                    XMMATRIX R = XMMatrixRotationRollPitchYaw(XMConvertToRadians(dm.rotation.x), XMConvertToRadians(dm.rotation.y), XMConvertToRadians(dm.rotation.z));
                    XMMATRIX T = XMMatrixTranslation(dm.position.x, dm.position.y, dm.position.z);
                    XMStoreFloat4x4(&dm.worldMatrix, S * R * T);
                }
            }

            if (anyMeltingActive) {
                m_renderer->InvalidateRaytracingScene(false);
            }

            // Fullscreen Logic (F11)
            bool currentF11 = m_window->IsKeyDown(VK_F11);
            if (currentF11 && !prevF11) {
                m_window->ToggleFullscreen();
                m_window->ResetMouseDelta(); // Prevent sudden camera snaps when toggling
            }
            prevF11 = currentF11;

            // Handle Dynamic Resizing
            int cw = m_window->GetClientWidth();
            int ch = m_window->GetClientHeight();
            if (cw > 0 && ch > 0 && (cw != m_width || ch != m_height)) {
                m_width = cw;
                m_height = ch;
                m_renderer->RequestResize(cw, ch);
                EngineUI::WebMenuSystem::Get().Resize(cw, ch);
            }

            // Allow looking with right stick and gyro whenever in gameplay or freecam
            bool allowPadLook = (!m_window->IsCursorEnabled() || m_camera->IsFreecamMode());
            if (allowPadLook) {
                // DualSense / DualShock Right Analog Stick Camera Look (Push stick UP -> Look UP)
                float padRX = dualSense.GetRightStickX();
                float padRY = dualSense.GetRightStickY();
                if (std::abs(padRX) <= 0.001f && std::abs(padRY) <= 0.001f && dualShock.IsConnected()) {
                    padRX = dualShock.GetRightStickX();
                    padRY = dualShock.GetRightStickY();
                }

                if (std::abs(padRX) > 0.001f || std::abs(padRY) > 0.001f) {
                    float pitchSign = s_invertLookY ? -1.0f : 1.0f;
                    m_camera->AddYaw(padRX * s_stickLookSens * dt);
                    m_camera->AddPitch(padRY * pitchSign * s_stickLookSens * dt);
                }

                // DualSense Gyroscope Motion Aiming (Active when gyro aiming enabled or when aiming with L2 trigger)
                static bool s_prevAiming = false;
                bool isAiming = (dualSense.GetLeftTrigger() > 0.25f) || (dualShock.GetLeftTrigger() > 0.25f);
                if (isAiming && !s_prevAiming) {
                    if (dualSense.IsConnected()) {
                        dualSense.PlayTriggerVibration(true, false, 200, 38, 0.08f);
                    }
                }
                s_prevAiming = isAiming;

                if (dualSense.IsConnected() && (dualSense.IsGyroAimingEnabled() || isAiming)) {
                    const auto& motion = dualSense.GetMotion();
                    if (std::abs(motion.gyroYaw) > 0.05f || std::abs(motion.gyroPitch) > 0.05f) {
                        float gyroFactor = 0.015f * dt;
                        float pitchSign = s_invertLookY ? -1.0f : 1.0f;
                        m_camera->AddYaw(motion.gyroYaw * gyroFactor * dualSense.GetGyroYawSensitivity());
                        m_camera->AddPitch(motion.gyroPitch * pitchSign * gyroFactor * dualSense.GetGyroPitchSensitivity());
                    }
                } else if (dualShock.IsConnected() && (dualShock.IsGyroAimingEnabled() || isAiming)) {
                    const auto& motion = dualShock.GetMotion();
                    if (std::abs(motion.gyroYaw) > 0.05f || std::abs(motion.gyroPitch) > 0.05f) {
                        float gyroFactor = 0.015f * dt;
                        float pitchSign = s_invertLookY ? -1.0f : 1.0f;
                        m_camera->AddYaw(motion.gyroYaw * gyroFactor * dualShock.GetGyroYawSensitivity());
                        m_camera->AddPitch(motion.gyroPitch * pitchSign * gyroFactor * dualShock.GetGyroPitchSensitivity());
                    }
                }
            }

            if (!m_window->IsCursorEnabled()) {
                const auto& mapping = EngineInput::InputMapping::Get();
                const auto mouse = mapping.GetFrameMouseDelta();
                const float dx = mouse.x, dy = mouse.y;
                if (dx != 0 || dy != 0) {
                    const float sensitivity = mapping.GetMouseSensitivity() * 0.001f;
                    m_camera->AddYaw(dx * sensitivity);
                    m_camera->AddPitch(dy * sensitivity * (mapping.IsInvertY() ? 1.0f : -1.0f));
                }

                // Keyboard camera look fallback (Arrow Keys & I/J/K/L for emulators or testing)
                float kbYaw = 0.0f;
                float kbPitch = 0.0f;
                if (m_window->IsKeyDown(0x25)) kbYaw -= 1.0f; // VK_LEFT
                if (m_window->IsKeyDown(0x27)) kbYaw += 1.0f; // VK_RIGHT
                if (m_window->IsKeyDown(0x26)) kbPitch += 1.0f; // VK_UP
                if (m_window->IsKeyDown(0x28)) kbPitch -= 1.0f; // VK_DOWN
                if (m_window->IsKeyDown('J')) kbYaw -= 1.0f;  // RightStickLeft in shadPS4
                if (m_window->IsKeyDown('L')) kbYaw += 1.0f;  // RightStickRight in shadPS4
                if (m_window->IsKeyDown('I')) kbPitch += 1.0f; // RightStickUp in shadPS4
                if (m_window->IsKeyDown('K')) kbPitch -= 1.0f; // RightStickDown in shadPS4
                if (kbYaw != 0.0f || kbPitch != 0.0f) {
                    m_camera->AddYaw(kbYaw * s_stickLookSens * dt);
                    m_camera->AddPitch(kbPitch * s_stickLookSens * dt);
                }
            }

            m_camera->Update(dt, !EnginePhysics::CharacterController::Get().IsEnabled());

            // Update 3D Spatial Audio Listener
            DirectX::XMFLOAT3 camPos = m_camera->GetPosition();
            DirectX::XMFLOAT3 camFwd = m_camera->GetForwardVector();
            DirectX::XMFLOAT3 camUp = m_camera->GetUpVector();
            EngineAudio::AudioSystem::Get().UpdateListener(camPos.x, camPos.y, camPos.z,
                                                           camFwd.x, camFwd.y, camFwd.z,
                                                           camUp.x, camUp.y, camUp.z);
            EngineAudio::AudioSystem::Get().Update(dt);

            // Update Multi-Camera System
            CameraSystem::Get().Update(dt, m_camera.get(), m_renderer.get());

            EnginePhysics::PhysicsSystem::Get().SyncAllActorsFromScene(m_renderer.get());
#if defined(ENGINE_EDITOR)
            if (!m_benchmarkFrameLimit) {
                bool editMode=m_camera->IsFreecamMode();
                if(m_wasEditing&&!editMode)m_playSnapshot=CaptureScene();
                if(editMode!=m_wasEditing)EnginePhysics::PhysicsSystem::Get().SetPaused(editMode);
                if(editMode&&!m_wasEditing)m_historyChanged=true;
                m_wasEditing=editMode;
            }
#endif
            // Update PhysX Simulation
            { EngineDebug::ScopedTimer profile(EngineDebug::Subsystem::Physics);EnginePhysics::PhysicsSystem::Get().Update(dt, m_renderer.get(), m_window.get(), m_camera.get()); }

            // Update XPBD Cloth Physics System
            { EngineDebug::ScopedTimer profile(EngineDebug::Subsystem::Physics);EnginePhysics::ClothSystem::Get().Update(dt, m_renderer.get(), m_window.get(), m_camera.get()); }

            // Update XPBD Hair Physics System
            { EngineDebug::ScopedTimer profile(EngineDebug::Subsystem::Physics);EnginePhysics::HairSystem::Get().Update(dt, m_renderer.get(), m_window.get(), m_camera.get()); }

            // Update HTML5/JS WebUI Main Menu
            EngineUI::WebMenuSystem::Get().Update(dt, m_fps, m_renderer.get(), m_window.get());

            // Update Animation System & Motion Matching
            EngineAnim::AnimationSystem::Get().Update(dt, m_window.get(), m_camera.get());

            // Update Multiplayer Networking & Client Prediction
            EngineNetwork::MultiplayerManager::Get().Update(dt, m_renderer.get(), m_camera.get(), m_window.get());

            auto syncOwned=[&](auto& models){for(auto& model:models)if(model.physicsBodyType==2||model.gameplay.door)EngineScene::SceneGraph::Get().SyncNodeFromModel(m_renderer.get(),model.modelId);};
            syncOwned(m_renderer->GetModels());syncOwned(m_renderer->GetDynamicModels());
            // Update Scene Graph Hierarchy & Sync Lights/Meshes to Renderer
            { EngineDebug::ScopedTimer profile(EngineDebug::Subsystem::SceneGraph);EngineScene::SceneGraph::Get().Update(dt); }
            { EngineDebug::ScopedTimer profile(EngineDebug::Subsystem::SceneGraph);EngineScene::SceneGraph::Get().SyncWithRenderer(m_renderer.get()); }

            static bool previousInteract=false;
            bool interact=m_window->IsKeyDown('E');
            if(m_gameplay&&!m_camera->IsFreecamMode()&&!EnginePhysics::PhysicsSystem::Get().IsPaused())m_gameplay->Update(*m_renderer,m_camera->GetPosition(),dt,interact&&!previousInteract);
            previousInteract=interact;

            // Update Lua Script System (calls OnUpdate on all loaded scripts + hot-reload check)
#if defined(ENGINE_EDITOR)
            if(!m_camera->IsFreecamMode())
#endif
            { EngineDebug::ScopedTimer profile(EngineDebug::Subsystem::Script);EngineScript::ScriptSystem::Get().Update(dt); }

            // Update NavMesh & AI Navigation System (pathfinding, agent behaviors, and sync to renderer)
            { EngineDebug::ScopedTimer profile(EngineDebug::Subsystem::Navigation);EngineAI::NavigationSystem::Get().Update(dt, m_camera->GetPosition()); }
            EngineAI::NavigationSystem::Get().SyncWithRenderer(m_renderer.get());

            // Update Universal Particle / VFX System
            { EngineDebug::ScopedTimer profile(EngineDebug::Subsystem::VFX);EngineVFX::ParticleSystem::Get().Update(dt, m_camera->GetPosition(), m_camera->GetUpVector()); }

            // Update Terrain & Foliage System
            { EngineDebug::ScopedTimer profile(EngineDebug::Subsystem::Terrain);EngineTerrain::TerrainSystem::Get().Update(dt, m_camera->GetPosition()); }
            if (EngineTerrain::TerrainSystem::Get().GetConfig().enabled &&
                EngineTerrain::TerrainSystem::Get().IsOnTerrain(m_camera->GetPosition().x, m_camera->GetPosition().z) &&
                !m_camera->IsFreecamMode()) {
                float terrainH = EngineTerrain::TerrainSystem::Get().GetHeightAt(m_camera->GetPosition().x, m_camera->GetPosition().z);
                m_camera->SetFloorY((std::max)(0.0f, terrainH));
            }

            // Update Cinematic Post-Processing System (Volumes & Blending)
            EnginePostProcess::PostProcessSystem::Get().Update(dt, m_camera->GetPosition());

            if (!m_renderer->IsDeviceRemoved()) {
#ifndef ENGINE_RELEASE
                m_renderer->BeginUI();
                if (dualSense.IsConnected() && m_window->IsCursorEnabled()) {
                    ImGuiIO& io = ImGui::GetIO();
                    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
                    io.AddKeyEvent(ImGuiKey_GamepadDpadUp, dualSense.IsButtonDown(EngineInput::DualSenseButton::DPadUp));
                    io.AddKeyEvent(ImGuiKey_GamepadDpadDown, dualSense.IsButtonDown(EngineInput::DualSenseButton::DPadDown));
                    io.AddKeyEvent(ImGuiKey_GamepadDpadLeft, dualSense.IsButtonDown(EngineInput::DualSenseButton::DPadLeft));
                    io.AddKeyEvent(ImGuiKey_GamepadDpadRight, dualSense.IsButtonDown(EngineInput::DualSenseButton::DPadRight));
                    io.AddKeyEvent(ImGuiKey_GamepadFaceDown, dualSense.IsButtonDown(EngineInput::DualSenseButton::Cross));
                    io.AddKeyEvent(ImGuiKey_GamepadFaceRight, dualSense.IsButtonDown(EngineInput::DualSenseButton::Circle));
                    io.AddKeyEvent(ImGuiKey_GamepadFaceLeft, dualSense.IsButtonDown(EngineInput::DualSenseButton::Square));
                    io.AddKeyEvent(ImGuiKey_GamepadFaceUp, dualSense.IsButtonDown(EngineInput::DualSenseButton::Triangle));
                    io.AddKeyEvent(ImGuiKey_GamepadL1, dualSense.IsButtonDown(EngineInput::DualSenseButton::L1));
                    io.AddKeyEvent(ImGuiKey_GamepadR1, dualSense.IsButtonDown(EngineInput::DualSenseButton::R1));
                    io.AddKeyEvent(ImGuiKey_GamepadL2, dualSense.GetLeftTrigger() > 0.3f);
                    io.AddKeyEvent(ImGuiKey_GamepadR2, dualSense.GetRightTrigger() > 0.3f);
                    io.AddKeyEvent(ImGuiKey_GamepadL3, dualSense.IsButtonDown(EngineInput::DualSenseButton::L3));
                    io.AddKeyEvent(ImGuiKey_GamepadR3, dualSense.IsButtonDown(EngineInput::DualSenseButton::R3));
                    io.AddKeyEvent(ImGuiKey_GamepadStart, dualSense.IsButtonDown(EngineInput::DualSenseButton::Options));
                    io.AddKeyEvent(ImGuiKey_GamepadBack, dualSense.IsButtonDown(EngineInput::DualSenseButton::Create));
                    
                    float lx = dualSense.GetLeftStickX();
                    float ly = dualSense.GetLeftStickY();
                    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickLeft, lx < -0.3f, lx < -0.3f ? -lx : 0.0f);
                    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickRight, lx > 0.3f, lx > 0.3f ? lx : 0.0f);
                    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickUp, ly > 0.3f, ly > 0.3f ? ly : 0.0f);
                    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickDown, ly < -0.3f, ly < -0.3f ? -ly : 0.0f);
                }

#if defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
                if (m_window->IsCursorEnabled()) {
                    auto* ps4Win = m_window->GetPS4Window();
                    if (ps4Win) {
                        ImGuiIO& io = ImGui::GetIO();
                        io.AddMousePosEvent(ps4Win->GetVirtualCursorX(), ps4Win->GetVirtualCursorY());
                        bool isLeftClick = dualSense.IsButtonDown(EngineInput::DualSenseButton::Cross) ||
                                           (dualSense.GetRightTrigger() > 0.3f);
                        bool isRightClick = dualSense.IsButtonDown(EngineInput::DualSenseButton::Circle);
                        io.AddMouseButtonEvent(0, isLeftClick);
                        io.AddMouseButtonEvent(1, isRightClick);
                    }
                }
#endif
                auto editorUIStart=std::chrono::steady_clock::now();
                bool sceneDirty = false;
                if(!m_editHistory.Count()){m_editHistory.Reset(CaptureScene());if(m_camera->IsFreecamMode())EnginePhysics::PhysicsSystem::Get().SetPaused(true);}
                auto restoreHistory=[&](bool redo){
                    if(auto state=redo?m_editHistory.Redo():m_editHistory.Undo()){
                        ApplyScene(*state);m_historyChanged=false;
                        EnginePhysics::PhysicsSystem::Get().SetPaused(true);
                    }
                };
                auto editorState = [&]() {
                    std::ostringstream state;
                    state<<m_renderer->GetModels().size()<<"/"<<m_renderer->GetDynamicModels().size()<<"/";
                    EngineRender::WriteSceneLights(state,m_renderer->GetPointLights(),m_renderer->GetSpotLights());
                    auto& post=EnginePostProcess::PostProcessSystem::Get();
                    EnginePostProcess::WritePostProcessState(state,post.GetGlobalSettings(),post.GetVolumes());
                    state<<m_renderer->m_dxrEnabled<<m_renderer->m_rtShadowsEnabled<<m_renderer->m_rtReflectionsEnabled<<m_renderer->IsPathTracingEnabled()
                        <<static_cast<int>(m_renderer->GetUpscaleTechnique())<<static_cast<int>(m_renderer->GetUpscaleQuality())<<m_renderer->IsVSyncEnabled()<<m_renderer->IsTAAEnabled();
                    const auto dir=m_renderer->GetLightDir(), color=m_renderer->GetLightColor(), sky=m_renderer->GetSkyColor();
                    state<<dir.x<<' '<<dir.y<<' '<<dir.z<<' '<<color.x<<' '<<color.y<<' '<<color.z<<' '<<sky.x<<' '<<sky.y<<' '<<sky.z<<' '
                        <<m_renderer->IsSSAOEnabled()<<m_renderer->IsSSGIEnabled()<<m_renderer->IsVSMEnabled()<<m_renderer->IsVolumetricFogEnabled()<<' '
                        <<m_renderer->GetVolumetricFogDensity()<<' '<<m_renderer->GetCloudDensity()<<' '<<m_renderer->GetFOV();
                    return state.str();
                };
                static std::string s_lastEditorState;
                if (s_lastEditorState.empty()) s_lastEditorState = editorState();

                // 3D Transform Gizmo State (Unreal / Unity style)
                static int s_selectedModelType = 0; // 0 = None, 1 = Static RenderModel, 2 = RenderDynamicModel
                static int s_selectedModelIndex = -1;
                if(m_benchmarkEditorPreview && s_selectedModelType==0) { s_selectedModelType=1; s_selectedModelIndex=1; }
                static bool s_showSceneGizmos=true;
                static bool s_showLightsUI=false, s_showSceneGraph=false, s_showAdvancedTools=false;
                static bool s_showSpawner=false, s_showEnvironment=false, s_showGraphics=false, s_showSequencer=false, s_showPhysicsTools=false;
                if (m_benchmarkAntialiasing >= 0) s_showGraphics=true;
                static bool showRainWindow=false, showDualSenseWindow=false, showProjectSettings=false;
                static bool s_showCCTUI=false, s_showClothUI=false, s_showHairUI=false, s_showAnimUI=false, s_showMultiplayerUI=false;
                static bool s_showRenderDebugUI=false, s_showMemoryDebugUI=false, s_showSecurityDebugUI=false, s_showInputDebugUI=false;
                static bool s_showTimeSimDebugUI=false, s_showImGuiMetrics=false, s_showImGuiStyleEditor=false;
                static bool s_showSceneEntityDebugUI=false, s_showPhysicsSandboxDebugUI=false, s_showAtmosphereDebugUI=false;
                static bool s_showSubsystemsDebugUI=false, s_showBenchmarkDebugUI=false;
                static bool s_debugShowAllColliders=false, s_debugShowWorldAxes=false, s_debugShowLightGizmos=false, s_debugShowModelBounds=false;
                static bool s_debugShowObjectLabels=false, s_debugShowWorldGrid=false, s_debugShowCrosshairRay=false, s_debugShowSunWindVectors=false;
                static ImGuizmo::OPERATION s_currentGizmoOp = ImGuizmo::TRANSLATE;
                static ImGuizmo::MODE s_currentGizmoMode = ImGuizmo::WORLD;
                static bool s_useGizmoSnap = false;
                static float s_snapTranslate[3] = { 0.5f, 0.5f, 0.5f };
                static float s_snapRotate = 15.0f;
                static float s_snapScale = 0.1f;
                EngineProject::DrawSettings(showProjectSettings,m_window->GetHWND(),[&]{return SaveScene();});
                EngineProject::DrawProjectBuild(m_window->GetHWND(),[&]{return SaveScene();});
#if defined(_WIN32)
                static bool showPipeline=false;
                if(ImGui::IsKeyPressed(ImGuiKey_F8,false))showPipeline=!showPipeline;
                m_renderer->DrawPipelineDiagnostics(showPipeline);
#endif
                const bool showEditorUI = (m_window->IsCursorEnabled() && !s_cursorUnlockedForConsole) || s_rmbViewportActive;
                if (showEditorUI) {
                    if(ImGui::BeginMainMenuBar()) {
                        if(ImGui::BeginMenu("Project")) {
                            ImGui::MenuItem("Project Settings...",nullptr,&showProjectSettings);
                            if(ImGui::MenuItem("Build game..."))EngineProject::ShowProjectBuild(EngineProject::current);
                            ImGui::EndMenu();
                        }
                        if(ImGui::BeginMenu("File")) {
                            if(ImGui::MenuItem("Save scene", "Ctrl+S")) SaveScene();
                            if(ImGui::MenuItem("Reload saved scene")) { LoadScene(); s_selectedModelType=0;s_selectedModelIndex=-1; }
                            if(ImGui::MenuItem("Scene recovery..."))m_showRecovery=true;
                            ImGui::SliderInt("Autosave interval (seconds)",&m_autosaveSeconds,15,300);
                            if(ImGui::MenuItem("Level files...")) EngineLevel::LevelSystem::Get().ToggleOpen();
                            ImGui::EndMenu();
                        }
                        if(ImGui::BeginMenu("Edit")) {
                            if(ImGui::MenuItem("Undo","Ctrl+Z",false,m_editHistory.CanUndo()))restoreHistory(false);
                            if(ImGui::MenuItem("Redo","Ctrl+Y",false,m_editHistory.CanRedo()))restoreHistory(true);
                            ImGui::EndMenu();
                        }
                        if(ImGui::BeginMenu("Create")) {
                            XMFLOAT3 p=m_camera->GetPosition(), f=m_camera->GetForwardVector();p.x+=f.x*5;p.z+=f.z*5;p.y=1;
                            if(ImGui::MenuItem("Vehicle (car)")) { EnginePhysics::PhysicsSystem::Get().SpawnVehicle(m_renderer.get(),p);s_selectedModelType=1;s_selectedModelIndex=(int)m_renderer->GetModels().size()-14;sceneDirty=true; }
                            if(ImGui::MenuItem("Cube")) { m_renderer->SpawnCube(p);s_selectedModelType=1;s_selectedModelIndex=(int)m_renderer->GetModels().size()-1;sceneDirty=true; }
                            if(ImGui::MenuItem("Sphere")) { m_renderer->SpawnSphere(p);s_selectedModelType=1;s_selectedModelIndex=(int)m_renderer->GetModels().size()-1;sceneDirty=true; }
                            if(ImGui::MenuItem("Plane")) { p.y=0;m_renderer->SpawnPlane(p);s_selectedModelType=1;s_selectedModelIndex=(int)m_renderer->GetModels().size()-1;sceneDirty=true; }
                            if(ImGui::MenuItem("Point light")) {
                                Renderer::PointLight light;light.name="Point Light";light.position=p;light.intensity=8;light.range=15;
                                auto& lights=m_renderer->GetPointLights();if(lights.size()<EngineRender::MAX_POINT_LIGHTS){lights.push_back(light);s_selectedModelType=3;s_selectedModelIndex=(int)lights.size()-1;sceneDirty=true;}
                            }
                            if(ImGui::MenuItem("Spot light")) {
                                Renderer::SpotLight light;light.name="Spot Light";light.position=p;light.direction=m_camera->GetForwardVector();light.intensity=12;light.range=20;
                                auto& lights=m_renderer->GetSpotLights();if(lights.size()<EngineRender::MAX_SPOT_LIGHTS){lights.push_back(light);s_selectedModelType=4;s_selectedModelIndex=(int)lights.size()-1;sceneDirty=true;}
                            }
                            if(ImGui::MenuItem("Import model...") && EngineUI::ContentBrowser::Get().ImportModelFileDialog(m_renderer.get(),m_camera.get())) {sceneDirty=true;s_selectedModelType=2;s_selectedModelIndex=(int)m_renderer->GetDynamicModels().size()-1;}
                            ImGui::EndMenu();
                        }
                        if(ImGui::BeginMenu("Window")) {
                            ImGui::MenuItem("Scene gizmos (editor only)",nullptr,&s_showSceneGizmos);
                            ImGui::MenuItem("Lighting",nullptr,&s_showLightsUI);
                            ImGui::MenuItem("Environment",nullptr,&s_showEnvironment);
                            ImGui::MenuItem("Graphics",nullptr,&s_showGraphics);
                            ImGui::MenuItem("Weather",nullptr,&showRainWindow);
                            if(ImGui::MenuItem("Post-processing", nullptr, EnginePostProcess::PostProcessSystem::Get().IsUIOpen())) EnginePostProcess::PostProcessSystem::Get().ToggleUI();
                            ImGui::Separator();
                            ImGui::MenuItem("Create / import objects",nullptr,&s_showSpawner);
                            ImGui::MenuItem("Sequencer",nullptr,&s_showSequencer);
                            ImGui::MenuItem("Physics tools",nullptr,&s_showPhysicsTools);
                            ImGui::MenuItem("Scene graph",nullptr,&s_showSceneGraph);
                            ImGui::MenuItem("Advanced tools",nullptr,&s_showAdvancedTools);
                            if(ImGui::MenuItem("Content Browser")) EngineUI::ContentBrowser::Get().SetOpen(true);
                            if(ImGui::MenuItem("Prefabs", nullptr, EnginePrefab::PrefabSystem::Get().IsUIOpen())) EnginePrefab::PrefabSystem::Get().ToggleUI();
                            if(ImGui::MenuItem("Asset Registry", nullptr, EngineAsset::AssetRegistry::Get().IsUIOpen())) EngineAsset::AssetRegistry::Get().ToggleUI();
                            if(ImGui::MenuItem("Cameras & CCTV Studio", nullptr, CameraSystem::Get().IsUIOpen())) CameraSystem::Get().ToggleUI();
                            ImGui::EndMenu();
                        }
                        if(ImGui::BeginMenu("Simulation")) {
                            ImGui::MenuItem("PhysX Rigidbodies & Gun", nullptr, &s_showPhysicsTools);
                            ImGui::MenuItem("Character Controller (CCT)", nullptr, &s_showCCTUI);
                            ImGui::MenuItem("XPBD Cloth Physics Studio", nullptr, &s_showClothUI);
                            ImGui::MenuItem("XPBD Hair Physics Studio", nullptr, &s_showHairUI);
                            ImGui::Separator();
                            ImGui::MenuItem("Animation & Motion Matching", nullptr, &s_showAnimUI);
                            if(ImGui::MenuItem("AI & NavMesh Studio", nullptr, EngineAI::NavigationSystem::Get().IsUIOpen())) EngineAI::NavigationSystem::Get().ToggleUI();
                            if(ImGui::MenuItem("Particle / VFX Studio", nullptr, EngineVFX::ParticleSystem::Get().IsUIOpen())) EngineVFX::ParticleSystem::Get().ToggleUI();
                            if(ImGui::MenuItem("Terrain & Foliage Studio", nullptr, EngineTerrain::TerrainSystem::Get().IsUIOpen())) EngineTerrain::TerrainSystem::Get().ToggleUI();
                            if(ImGui::MenuItem("3D Spatial Audio Studio", nullptr, EngineAudio::AudioSystem::Get().IsUIOpen())) EngineAudio::AudioSystem::Get().ToggleUI();
                            if(ImGui::MenuItem("Lua 5.4 Scripting Studio", nullptr, EngineScript::ScriptSystem::Get().IsUIOpen())) EngineScript::ScriptSystem::Get().ToggleUI();
                            ImGui::EndMenu();
                        }
                        if(ImGui::BeginMenu("Debug")) {
#if defined(_WIN32)
                            ImGui::MenuItem("Render Pipeline State","F8",&showPipeline);
#endif
                            if(ImGui::MenuItem("Engine Profiler & Telemetry", nullptr, EngineDebug::Profiler::Get().IsUIOpen())) EngineDebug::Profiler::Get().ToggleUI();
                            if(ImGui::MenuItem("Developer Console (~)", "~", EngineDebug::DebugConsole::Get().IsOpen())) EngineDebug::DebugConsole::Get().ToggleConsole();
                            bool fpsHud = EngineDebug::DebugConsole::Get().IsFpsOverlayEnabled();
                            if(ImGui::MenuItem("In-Game Telemetry & FPS HUD", "F3", &fpsHud)) EngineDebug::DebugConsole::Get().SetFpsOverlayEnabled(fpsHud);
                            ImGui::Separator();
                            ImGui::MenuItem("DX12 & Render Pipeline Inspector", nullptr, &s_showRenderDebugUI);
                            ImGui::MenuItem("Memory, VRAM & Streaming Debug", nullptr, &s_showMemoryDebugUI);
                            ImGui::MenuItem("Scene & Entity Batch Inspector", nullptr, &s_showSceneEntityDebugUI);
                            ImGui::MenuItem("Physics, Force Field & Destruction Sandbox", nullptr, &s_showPhysicsSandboxDebugUI);
                            ImGui::MenuItem("Atmosphere, Time-of-Day & Weather Tweaker", nullptr, &s_showAtmosphereDebugUI);
                            ImGui::MenuItem("Audio, VFX, AI & Lua Subsystem Debugger", nullptr, &s_showSubsystemsDebugUI);
                            ImGui::MenuItem("Stress Test & Benchmark Studio", nullptr, &s_showBenchmarkDebugUI);
                            ImGui::MenuItem("Time Scale & Simulation Control", nullptr, &s_showTimeSimDebugUI);
                            ImGui::MenuItem("Camera, Input & Raycast Inspector", nullptr, &s_showInputDebugUI);
                            ImGui::MenuItem("Security & DLL Integrity Audit", nullptr, &s_showSecurityDebugUI);
                            ImGui::Separator();
                            if(ImGui::BeginMenu("Viewport Overlays & Visualizations")) {
                                ImGui::MenuItem("Show 3D Object Names, IDs & Distances", nullptr, &s_debugShowObjectLabels);
                                ImGui::MenuItem("Show All Physics Colliders (Wireframe)", nullptr, &s_debugShowAllColliders);
                                ImGui::MenuItem("Show Model Bounding Boxes (AABB)", nullptr, &s_debugShowModelBounds);
                                ImGui::MenuItem("Show Point/Spot Light Range Rings", nullptr, &s_debugShowLightGizmos);
                                ImGui::MenuItem("Show World Origin & XYZ Axes", nullptr, &s_debugShowWorldAxes);
                                ImGui::MenuItem("Show 3D World Ground Grid (20x20m)", nullptr, &s_debugShowWorldGrid);
                                ImGui::MenuItem("Show Center Crosshair & Live Raycast HUD", nullptr, &s_debugShowCrosshairRay);
                                ImGui::MenuItem("Show Sun Direction & Wind Compass", nullptr, &s_debugShowSunWindVectors);
                                bool lodDebug = m_renderer->IsDebugLODColor();
                                if(ImGui::MenuItem("LOD Color Tinting (Green/Yellow/Red)", nullptr, &lodDebug)) m_renderer->SetDebugLODColor(lodDebug);
                                ImGui::Separator();
                                if(ImGui::MenuItem("Enable All 3D Viewport Overlays")) {
                                    s_debugShowObjectLabels = s_debugShowAllColliders = s_debugShowModelBounds =
                                    s_debugShowLightGizmos = s_debugShowWorldAxes = s_debugShowWorldGrid =
                                    s_debugShowCrosshairRay = s_debugShowSunWindVectors = true;
                                }
                                if(ImGui::MenuItem("Disable All 3D Viewport Overlays")) {
                                    s_debugShowObjectLabels = s_debugShowAllColliders = s_debugShowModelBounds =
                                    s_debugShowLightGizmos = s_debugShowWorldAxes = s_debugShowWorldGrid =
                                    s_debugShowCrosshairRay = s_debugShowSunWindVectors = false;
                                    m_renderer->SetDebugLODColor(false);
                                }
                                ImGui::EndMenu();
                            }
                            if(ImGui::BeginMenu("Graphics & Upscaler Presets (DLSS / FSR / XeSS)")) {
                                ImGui::SeparatorText("Overall Graphics Presets");
                                if(ImGui::MenuItem("Preset: Low (Competitive / Max FPS)")) EngineUI::ApplyGraphicsPreset(*m_renderer, 0);
                                if(ImGui::MenuItem("Preset: Medium (Raster + SSAO + Shadows)")) EngineUI::ApplyGraphicsPreset(*m_renderer, 1);
                                if(ImGui::MenuItem("Preset: High (DXR Hybrid RT)")) EngineUI::ApplyGraphicsPreset(*m_renderer, 2);
                                if(ImGui::MenuItem("Preset: Ultra (RTX + Quality Upscaling)")) EngineUI::ApplyGraphicsPreset(*m_renderer, 3);
                                if(ImGui::MenuItem("Preset: Path Tracing Overdrive (Cinema)")) EngineUI::ApplyGraphicsPreset(*m_renderer, 4);
                                ImGui::SeparatorText("NVIDIA DLSS 4.5 Presets");
                                ImGui::BeginDisabled(!m_renderer->IsDLSSSupported());
                                if(ImGui::MenuItem("NVIDIA DLAA (Native 100%)")) { m_renderer->SetUpscaleTechnique(UpscaleTechnique::DLSS); m_renderer->SetUpscaleQuality(UpscaleQuality::Native); }
                                if(ImGui::MenuItem("DLSS Quality (67% Scale)")) { m_renderer->SetUpscaleTechnique(UpscaleTechnique::DLSS); m_renderer->SetUpscaleQuality(UpscaleQuality::Quality); }
                                if(ImGui::MenuItem("DLSS Balanced (58% Scale)")) { m_renderer->SetUpscaleTechnique(UpscaleTechnique::DLSS); m_renderer->SetUpscaleQuality(UpscaleQuality::Balanced); }
                                if(ImGui::MenuItem("DLSS Performance (50% Scale)")) { m_renderer->SetUpscaleTechnique(UpscaleTechnique::DLSS); m_renderer->SetUpscaleQuality(UpscaleQuality::Performance); }
                                if(ImGui::MenuItem("DLSS Ultra Performance (33% Scale)")) { m_renderer->SetUpscaleTechnique(UpscaleTechnique::DLSS); m_renderer->SetUpscaleQuality(UpscaleQuality::UltraPerformance); }
                                ImGui::EndDisabled();
                                ImGui::SeparatorText("AMD FSR 3.1 Presets");
                                if(ImGui::MenuItem("FSR Native AA (100%)")) { m_renderer->SetUpscaleTechnique(UpscaleTechnique::FSR); m_renderer->SetUpscaleQuality(UpscaleQuality::Native); }
                                if(ImGui::MenuItem("FSR Quality (67% Scale)")) { m_renderer->SetUpscaleTechnique(UpscaleTechnique::FSR); m_renderer->SetUpscaleQuality(UpscaleQuality::Quality); }
                                if(ImGui::MenuItem("FSR Balanced (58% Scale)")) { m_renderer->SetUpscaleTechnique(UpscaleTechnique::FSR); m_renderer->SetUpscaleQuality(UpscaleQuality::Balanced); }
                                if(ImGui::MenuItem("FSR Performance (50% Scale)")) { m_renderer->SetUpscaleTechnique(UpscaleTechnique::FSR); m_renderer->SetUpscaleQuality(UpscaleQuality::Performance); }
                                if(ImGui::MenuItem("FSR Ultra Performance (33% Scale)")) { m_renderer->SetUpscaleTechnique(UpscaleTechnique::FSR); m_renderer->SetUpscaleQuality(UpscaleQuality::UltraPerformance); }
                                ImGui::SeparatorText("Intel XeSS 2.0 Presets");
                                ImGui::BeginDisabled(!m_renderer->IsXeSSSupported());
                                if(ImGui::MenuItem("XeSS Native AA (100%)")) { m_renderer->SetUpscaleTechnique(UpscaleTechnique::XeSS); m_renderer->SetUpscaleQuality(UpscaleQuality::Native); }
                                if(ImGui::MenuItem("XeSS Quality")) { m_renderer->SetUpscaleTechnique(UpscaleTechnique::XeSS); m_renderer->SetUpscaleQuality(UpscaleQuality::Quality); }
                                if(ImGui::MenuItem("XeSS Balanced")) { m_renderer->SetUpscaleTechnique(UpscaleTechnique::XeSS); m_renderer->SetUpscaleQuality(UpscaleQuality::Balanced); }
                                if(ImGui::MenuItem("XeSS Performance")) { m_renderer->SetUpscaleTechnique(UpscaleTechnique::XeSS); m_renderer->SetUpscaleQuality(UpscaleQuality::Performance); }
                                if(ImGui::MenuItem("XeSS Ultra Performance")) { m_renderer->SetUpscaleTechnique(UpscaleTechnique::XeSS); m_renderer->SetUpscaleQuality(UpscaleQuality::UltraPerformance); }
                                ImGui::EndDisabled();
                                ImGui::Separator();
                                if(ImGui::MenuItem("Disable Upscaler (Native + TAA)")) { m_renderer->SetUpscaleTechnique(UpscaleTechnique::None); m_renderer->SetTAAEnabled(true); }
                                ImGui::EndMenu();
                            }
                            if(ImGui::BeginMenu("Quick Debug Actions & Cheats")) {
                                if(ImGui::MenuItem("Toggle DXR Ray Tracing (F5)")) {
                                    m_renderer->m_dxrEnabled = !m_renderer->m_dxrEnabled;
                                    m_renderer->InvalidateRaytracingScene();
                                }
                                if(ImGui::MenuItem("Toggle Full Path Tracing GI (F6)")) {
                                    bool pt = !m_renderer->IsPathTracingEnabled();
                                    if (pt) m_renderer->m_dxrEnabled = true;
                                    m_renderer->SetPathTracingEnabled(pt);
                                    m_renderer->InvalidateRaytracingScene();
                                }
                                if(ImGui::MenuItem("Force Rebuild DXR BLAS/TLAS")) m_renderer->InvalidateRaytracingScene(true);
                                ImGui::Separator();
                                if(ImGui::MenuItem("Spawn Destructible PhysX Tower")) EngineDebug::DebugConsole::Get().Execute("tower 3 6 3");
                                if(ImGui::MenuItem("Detonate Radial PhysX Explosion")) EngineDebug::DebugConsole::Get().Execute("explosion 2500 18");
                                if(ImGui::MenuItem("Melt All Scene Models")) EngineDebug::DebugConsole::Get().Execute("melt all 0.8");
                                if(ImGui::MenuItem("Restore / Solidify All Models")) EngineDebug::DebugConsole::Get().Execute("melt reset");
                                ImGui::Separator();
                                if(ImGui::MenuItem("Save BMP Screenshot")) EngineDebug::DebugConsole::Get().Execute("screenshot");
                                if(ImGui::MenuItem("Run Full Security & DLL Integrity Audit")) EngineDebug::DebugConsole::Get().Execute("security_audit");
                                ImGui::EndMenu();
                            }
                            if(ImGui::BeginMenu("Online & Platform Diagnostics")) {
                                ImGui::MenuItem("Multiplayer & Dedicated Server", nullptr, &s_showMultiplayerUI);
                                if(ImGui::MenuItem("Input Mapping & Bindings", nullptr, EngineInput::InputMapping::Get().IsUIOpen())) EngineInput::InputMapping::Get().ToggleUI();
                                ImGui::MenuItem("DualSense / DualShock Controller Center", nullptr, &showDualSenseWindow);
                                if(ImGui::MenuItem("Discord Social & Rich Presence", nullptr, EngineDiscord::DiscordSystem::Get().IsUIOpen())) EngineDiscord::DiscordSystem::Get().ToggleUI();
#if defined(ENGINE_PLATFORM_EPIC)
                                if(ImGui::MenuItem("Epic Online Services (EOS) Debug", nullptr, EngineEpic::EpicSystem::Get().IsUIOpen())) EngineEpic::EpicSystem::Get().ToggleUI();
#elif defined(ENGINE_PLATFORM_STEAM)
                                if(ImGui::MenuItem("Steamworks & DRM Debug", nullptr, EngineSteam::SteamSystem::Get().IsUIOpen())) EngineSteam::SteamSystem::Get().ToggleUI();
#endif
                                ImGui::EndMenu();
                            }
                            ImGui::Separator();
                            ImGui::MenuItem("Dear ImGui Metrics / Debugger", nullptr, &s_showImGuiMetrics);
                            ImGui::MenuItem("Dear ImGui Style Editor", nullptr, &s_showImGuiStyleEditor);
                            ImGui::EndMenu();
                        }
                        if(ImGui::BeginMenu("Help")) { ImGui::MenuItem("Controllers",nullptr,&showDualSenseWindow);ImGui::EndMenu(); }
                        if(ImGui::BeginMenu("Playback")){
                            auto& physics=EnginePhysics::PhysicsSystem::Get();
                            if(ImGui::MenuItem(physics.IsPaused()?"Resume physics":"Pause physics"))physics.SetPaused(!physics.IsPaused());
                            if(ImGui::MenuItem("Step physics")){physics.SetPaused(true);physics.SingleStep();}
                            if(ImGui::MenuItem("Stop and keep transforms")){m_camera->SetFreecamMode(true);m_window->SetCursorEnabled(true);physics.SetPaused(true);sceneDirty=true;}
                            if(ImGui::MenuItem("Stop and restore pre-play scene",nullptr,false,!m_playSnapshot.empty())){
                                ApplyScene(m_playSnapshot);m_camera->SetFreecamMode(true);m_window->SetCursorEnabled(true);physics.SetPaused(true);sceneDirty=true;
                                s_selectedModelType=0;s_selectedModelIndex=-1;
                            }
                            ImGui::EndMenu();
                        }
                        if(ImGui::MenuItem("Play", "TAB")) { m_camera->SetFreecamMode(false);m_window->SetCursorEnabled(false);m_window->ResetMouseDelta(); }
                        ImGui::EndMainMenuBar();
                    }
                    if(ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S,false)) SaveScene();
                    if(!ImGui::GetIO().WantTextInput&&ImGui::GetIO().KeyCtrl){
                        if(ImGui::IsKeyPressed(ImGuiKey_Z,false)){restoreHistory(ImGui::GetIO().KeyShift);s_selectedModelType=0;s_selectedModelIndex=-1;}
                        if(ImGui::IsKeyPressed(ImGuiKey_Y,false)){restoreHistory(true);s_selectedModelType=0;s_selectedModelIndex=-1;}
                    }
                    ImGuizmo::BeginFrame();
                    ImGuizmo::SetRect(0.0f, 0.0f, (float)m_width, (float)m_height);

                    auto& models = m_renderer->GetModels();
                    auto& dynModels = m_renderer->GetDynamicModels();

                    // Bounds check selected model index in case scene was reloaded
                    if (s_selectedModelType == 1 && (s_selectedModelIndex < 0 || s_selectedModelIndex >= (int)models.size())) {
                        s_selectedModelType = 0;
                        s_selectedModelIndex = -1;
                    } else if (s_selectedModelType == 2 && (s_selectedModelIndex < 0 || s_selectedModelIndex >= (int)dynModels.size())) {
                        s_selectedModelType = 0;
                        s_selectedModelIndex = -1;
                    }

                    // Hotkeys for Gizmo manipulation when not typing in ImGui input
                    if (!ImGui::GetIO().WantTextInput) {
                        if (m_window->IsKeyDown('W')) s_currentGizmoOp = ImGuizmo::TRANSLATE;
                        if (m_window->IsKeyDown('E')) s_currentGizmoOp = ImGuizmo::ROTATE;
                        if (m_window->IsKeyDown('R')) s_currentGizmoOp = ImGuizmo::SCALE;
                        if (m_window->IsKeyDown('Q')) { s_selectedModelType = 0; s_selectedModelIndex = -1; }
                    }

                    // 3D Viewport Raycast Object Picking on Left Click
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver() && !ImGui::GetIO().WantCaptureMouse) {
                        ImVec2 mousePos = ImGui::GetMousePos();
                        XMMATRIX viewMat = m_camera->GetViewMatrix();
                        XMMATRIX projMat = m_camera->GetCleanProjectionMatrix(XM_PIDIV4, (float)m_width / (float)m_height, 0.1f, 1000.0f);
                        XMMATRIX viewProj = viewMat * projMat;
                        XMMATRIX invViewProj = XMMatrixInverse(nullptr, viewProj);

                        float ndcX = (2.0f * mousePos.x) / (float)m_width - 1.0f;
                        float ndcY = 1.0f - (2.0f * mousePos.y) / (float)m_height;

                        XMVECTOR pNear = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 0.0f, 1.0f), invViewProj);
                        XMVECTOR pFar  = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 1.0f, 1.0f), invViewProj);
                        XMVECTOR vDir  = XMVector3Normalize(pFar - pNear);

                        XMFLOAT3 rayOrig, rayDir;
                        XMStoreFloat3(&rayOrig, pNear);
                        XMStoreFloat3(&rayDir, vDir);

                        auto pickObject=[&](const auto& model,float& distance) {
                            XMFLOAT3 lo,hi;
                            if(EnginePhysics::PhysicsSystem::Get().GetColliderBounds(model.modelId,lo,hi)) {
                                XMFLOAT3 center={(lo.x+hi.x)*.5f,(lo.y+hi.y)*.5f,(lo.z+hi.z)*.5f};
                                XMFLOAT3 size={hi.x-lo.x,hi.y-lo.y,hi.z-lo.z};
                                return RayIntersectBox(rayOrig,rayDir,center,size,{0,0,0},distance);
                            }
                            return RayIntersectBox(rayOrig,rayDir,model.position,model.scale,model.rotation,distance);
                        };
                        float closestDist = 1e9f;
                        int hitType = 0;
                        int hitIdx = -1;

                        for (size_t i = 0; i < models.size(); ++i) {
                            if (models[i].name == "Floor") continue;
                            float dist = 0.0f;
                            if (pickObject(models[i],dist)) {
                                if (dist < closestDist) {
                                    closestDist = dist;
                                    hitType = 1;
                                    hitIdx = (int)i;
                                }
                            }
                        }

                        for (size_t i = 0; i < dynModels.size(); ++i) {
                            float dist = 0.0f;
                            if (pickObject(dynModels[i],dist)) {
                                if (dist < closestDist) {
                                    closestDist = dist;
                                    hitType = 2;
                                    hitIdx = (int)i;
                                }
                            }
                        }

                        if (hitType != 0) {
                            s_selectedModelType = hitType;
                            s_selectedModelIndex = hitIdx;
                        } else {
                            s_selectedModelType = 0;
                            s_selectedModelIndex = -1;
                        }
                    }

                    // Floating Unreal/Unity Style Transform Toolbar
                    ImGui::SetNextWindowPos(ImVec2(280.0f, 30.0f), ImGuiCond_Always);
                    ImGui::SetNextWindowBgAlpha(0.85f);
                    if (ImGui::Begin("##GizmoToolbar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
                        bool isT = (s_currentGizmoOp == ImGuizmo::TRANSLATE);
                        bool isR = (s_currentGizmoOp == ImGuizmo::ROTATE);
                        bool isS = (s_currentGizmoOp == ImGuizmo::SCALE);

                        if (isT) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.95f, 1.0f));
                        if (ImGui::Button(" Move [W]")) s_currentGizmoOp = ImGuizmo::TRANSLATE;
                        if (isT) ImGui::PopStyleColor();
                        ImGui::SameLine();

                        if (isR) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.95f, 1.0f));
                        if (ImGui::Button(" Rotate [E]")) s_currentGizmoOp = ImGuizmo::ROTATE;
                        if (isR) ImGui::PopStyleColor();
                        ImGui::SameLine();

                        if (isS) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.95f, 1.0f));
                        if (ImGui::Button(" Scale [R]")) s_currentGizmoOp = ImGuizmo::SCALE;
                        if (isS) ImGui::PopStyleColor();
                        ImGui::SameLine();

                        ImGui::TextDisabled("|");
                        ImGui::SameLine();

                        if (s_currentGizmoMode == ImGuizmo::WORLD) {
                            if (ImGui::Button(" World")) s_currentGizmoMode = ImGuizmo::LOCAL;
                        } else {
                            if (ImGui::Button(" Local")) s_currentGizmoMode = ImGuizmo::WORLD;
                        }
                        ImGui::SameLine();

                        ImGui::TextDisabled("|");
                        ImGui::SameLine();

                        ImGui::Checkbox(" Snap", &s_useGizmoSnap);
                        if (s_useGizmoSnap) {
                            ImGui::SameLine();
                            ImGui::SetNextItemWidth(65.0f);
                            if (s_currentGizmoOp == ImGuizmo::TRANSLATE) {
                                ImGui::DragFloat("##SnapT", &s_snapTranslate[0], 0.1f, 0.05f, 10.0f, "%.2fm");
                                s_snapTranslate[1] = s_snapTranslate[0];
                                s_snapTranslate[2] = s_snapTranslate[0];
                            } else if (s_currentGizmoOp == ImGuizmo::ROTATE) {
                                ImGui::DragFloat("##SnapR", &s_snapRotate, 1.0f, 1.0f, 90.0f, "%.0f°");
                            } else if (s_currentGizmoOp == ImGuizmo::SCALE) {
                                ImGui::DragFloat("##SnapS", &s_snapScale, 0.05f, 0.01f, 5.0f, "%.2f");
                            }
                        }

                        ImGui::SameLine();
                        ImGui::TextDisabled("|");
                        ImGui::SameLine();

                        if (s_selectedModelType == 1 && s_selectedModelIndex >= 0 && s_selectedModelIndex < (int)models.size()) {
                            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), " %s (Static)", models[s_selectedModelIndex].name.c_str());
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Deselect [Q]")) { s_selectedModelType = 0; s_selectedModelIndex = -1; }
                        } else if (s_selectedModelType == 2 && s_selectedModelIndex >= 0 && s_selectedModelIndex < (int)dynModels.size()) {
                            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.8f, 1.0f), " %s (Dynamic)", dynModels[s_selectedModelIndex].name.c_str());
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Deselect [Q]")) { s_selectedModelType = 0; s_selectedModelIndex = -1; }
                        } else {
                            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Click any object in viewport to select | [TAB] toggle mode");
                        }

                        ImGui::End();
                    }

                    // Execute 3D Gizmo Manipulation
                    if (s_selectedModelType != 0 && s_selectedModelIndex >= 0) {
                        XMMATRIX viewMat = m_camera->GetViewMatrix();
                        XMMATRIX projMat = m_camera->GetCleanProjectionMatrix(XM_PIDIV4, (float)m_width / (float)m_height, 0.1f, 1000.0f);

                        XMFLOAT4X4 viewFloat, projFloat;
                        XMStoreFloat4x4(&viewFloat, viewMat);
                        XMStoreFloat4x4(&projFloat, projMat);

                        XMFLOAT3* pPos = nullptr;
                        XMFLOAT3* pRot = nullptr;
                        XMFLOAT3* pScale = nullptr;
                        uint32_t selectedModelId = 0;

                        if (s_selectedModelType == 1 && s_selectedModelIndex < (int)models.size()) {
                            pPos = &models[s_selectedModelIndex].position;
                            pRot = &models[s_selectedModelIndex].rotation;
                            pScale = &models[s_selectedModelIndex].scale;
                            selectedModelId = models[s_selectedModelIndex].modelId;
                        } else if (s_selectedModelType == 2 && s_selectedModelIndex < (int)dynModels.size()) {
                            pPos = &dynModels[s_selectedModelIndex].position;
                            pRot = &dynModels[s_selectedModelIndex].rotation;
                            pScale = &dynModels[s_selectedModelIndex].scale;
                            selectedModelId = dynModels[s_selectedModelIndex].modelId;
                        }

                        if (pPos && pRot && pScale) {
                            float tr[3] = { pPos->x, pPos->y, pPos->z };
                            float rotDeg[3] = {
                                pRot->x,
                                pRot->y,
                                pRot->z
                            };
                            float sc[3] = { pScale->x, pScale->y, pScale->z };

                            float objMatrix[16];
                            ImGuizmo::RecomposeMatrixFromComponents(tr, rotDeg, sc, objMatrix);

                            float snapValues[3] = { 0.0f, 0.0f, 0.0f };
                            if (s_useGizmoSnap) {
                                if (s_currentGizmoOp == ImGuizmo::TRANSLATE) {
                                    snapValues[0] = s_snapTranslate[0];
                                    snapValues[1] = s_snapTranslate[1];
                                    snapValues[2] = s_snapTranslate[2];
                                } else if (s_currentGizmoOp == ImGuizmo::ROTATE) {
                                    snapValues[0] = s_snapRotate;
                                } else if (s_currentGizmoOp == ImGuizmo::SCALE) {
                                    snapValues[0] = s_snapScale;
                                }
                            }

                            ImGuizmo::Manipulate((const float*)&viewFloat, (const float*)&projFloat,
                                                 s_currentGizmoOp, s_currentGizmoMode, objMatrix,
                                                 nullptr, s_useGizmoSnap ? snapValues : nullptr);

                            if (ImGuizmo::IsUsing()) {
                                ImGuizmo::DecomposeMatrixToComponents(objMatrix, tr, rotDeg, sc);
                                *pPos = XMFLOAT3(tr[0], tr[1], tr[2]);
                                *pRot = XMFLOAT3(
                                    rotDeg[0],
                                    rotDeg[1],
                                    rotDeg[2]
                                );
                                *pScale = XMFLOAT3(sc[0], sc[1], sc[2]);

                                if (selectedModelId != 0) {
                                    EngineScene::SceneGraph::Get().SyncNodeFromModel(m_renderer.get(),selectedModelId);
                                    EnginePhysics::PhysicsSystem::Get().ApplyModelPhysics(m_renderer.get(), selectedModelId, s_selectedModelType==2);
                                }

                                m_renderer->InvalidateRaytracingScene();
                                sceneDirty = true;
                            }
                        }
                    }

                    if(m_benchmarkEditorWorkflow){
                        ImGui::SetNextWindowPos(ImVec2(290,90),ImGuiCond_Always);ImGui::SetNextWindowSize(ImVec2(390,510),ImGuiCond_Always);
                        if(ImGui::Begin("Material preview verification")){
                            static Renderer::PBRMaterial preview;preview.albedo={.65f,.3f,.12f};preview.roughness=.25f;preview.metallic=.6f;
                            if(!preview.albedoMap)preview.albedoMap=m_renderer->LoadTexture("Content/DemoTexture/demo_texture.jpg");
                            ImGui::SetNextItemOpen(true,ImGuiCond_Always);EngineUI::DrawMaterialAsset(preview,*m_renderer);
                        }ImGui::End();
                    }
                    // Draw PhysX UI
                    if(s_showPhysicsTools) EnginePhysics::PhysicsSystem::Get().DrawUI(m_renderer.get(), m_camera.get());

                    // Draw Cloth Physics UI
                    if(s_showClothUI || s_showAdvancedTools) EnginePhysics::ClothSystem::Get().DrawUI(m_renderer.get(), m_camera.get());

                    // Draw Hair Physics UI
                    if(s_showHairUI || s_showAdvancedTools) EnginePhysics::HairSystem::Get().DrawUI(m_renderer.get(), m_camera.get());

                    // Draw 3D Spatial Audio Studio UI
                    EngineAudio::AudioSystem::Get().DrawUI();

                    if (s_showCCTUI) {
                        EnginePhysics::CharacterController::Get().DrawUI();
                    }

                    // Draw WebUI Fallback Main Menu (if WebView2 loading or active)
                    EngineUI::WebMenuSystem::Get().DrawFallbackUI(m_renderer.get(), m_window.get(), m_camera.get());

                    // Draw Animation System & Motion Matching UI
                    if(s_showAnimUI || s_showAdvancedTools) EngineAnim::AnimationSystem::Get().DrawUI(m_renderer.get(), m_camera.get());

                    // Draw Content Browser (src/Content)
                    EngineUI::ContentBrowser::Get().DrawUI(m_renderer.get(), m_camera.get());
                    // A drop on unoccupied viewport space imports an asset; panels keep their own targets.
                    if(const auto* payload=ImGui::GetDragDropPayload();payload&&payload->IsDataType("IDK_ASSET")&&ImGui::IsMouseReleased(0)&&!ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow|ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)){
                        std::filesystem::path path=std::filesystem::u8path(static_cast<const char*>(payload->Data));auto ext=path.extension().string();std::transform(ext.begin(),ext.end(),ext.begin(),::tolower);
                        auto p=m_camera->GetPosition(),forward=m_camera->GetForwardVector();p.x+=forward.x*4;p.y+=forward.y*4;p.z+=forward.z*4;
                        if(ext==".obj"||ext==".fbx"||ext==".gltf"||ext==".glb"){m_renderer->SpawnDynamicRenderModel(path.generic_string().c_str(),p);sceneDirty=true;}
                        if(ext==".prefab"){auto prefab=EnginePrefab::PrefabSystem::Get().LoadPrefab(path.generic_string());if(prefab){EnginePrefab::PrefabSystem::Get().InstantiateWithRenderer(prefab->GetName(),m_renderer.get(),nullptr,p);sceneDirty=true;}}
                    }


                    // Draw Map / Level Editor UI (.level)
                    EngineLevel::LevelSystem::Get().DrawUI(m_renderer.get(), m_camera.get());

                    // Draw Multiplayer & Dedicated Server Manager UI
                    if(s_showMultiplayerUI || s_showAdvancedTools) EngineNetwork::MultiplayerManager::Get().DrawImGuiMultiplayer(m_renderer.get(), m_camera.get());

                    ImGui::SetNextWindowPos(ImVec2(10,55),ImGuiCond_FirstUseEver);
                    ImGui::SetNextWindowSize(ImVec2(265,(float)m_height-80),ImGuiCond_FirstUseEver);
                    ImGui::Begin("Scene hierarchy");
                    ImGui::Text("Press TAB to return to Camera mode.");
                    ImGui::Separator();

                    if(s_showAdvancedTools) {
                    if (ImGui::Button(" Audio Studio")) {
                        EngineAudio::AudioSystem::Get().ToggleUI();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(" Character Controller")) {
                        s_showCCTUI = !s_showCCTUI;
                    }
                    ImGui::SameLine();

                    if (ImGui::Button(" Lights Studio")) {
                        s_showLightsUI = !s_showLightsUI;
                    }
                    ImGui::SameLine();

                    if (ImGui::Button(" Scene Graph")) {
                        s_showSceneGraph = !s_showSceneGraph;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(" Scripts")) {
                        EngineScript::ScriptSystem::Get().ToggleUI();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(" AI & NavMesh")) {
                        EngineAI::NavigationSystem::Get().ToggleUI();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(" VFX Studio")) {
                        EngineVFX::ParticleSystem::Get().ToggleUI();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(" Terrain Studio")) {
                        EngineTerrain::TerrainSystem::Get().ToggleUI();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(" Post-Process")) {
                        EnginePostProcess::PostProcessSystem::Get().ToggleUI();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(" Prefabs & Assets")) {
                        EnginePrefab::PrefabSystem::Get().ToggleUI();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(" Profiler")) {
                        EngineDebug::Profiler::Get().ToggleUI();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(" Console (~)")) {
                        EngineDebug::DebugConsole::Get().ToggleConsole();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(" Input Mapper")) {
                        EngineInput::InputMapping::Get().ToggleUI();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(" Cameras")) {
                        CameraSystem::Get().ToggleUI();
                    }
                    ImGui::Separator();

                    if (ImGui::Button(" Melt All to Floor")) {
                        for (auto& m : models) if (m.name != "Floor") m.StartMelting(0.85f);
                        for (auto& dm : dynModels) dm.StartMelting(0.85f);
                        m_renderer->InvalidateRaytracingScene();
                        sceneDirty = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(" Restore Solid")) {
                        for (auto& m : models) if (m.name != "Floor") m.ResetMelting();
                        for (auto& dm : dynModels) dm.ResetMelting();
                        m_renderer->InvalidateRaytracingScene();
                        sceneDirty = true;
                    }
                    ImGui::Separator();

                    } // Advanced tool buttons
                    auto DrawMaterialEditor = [&](Renderer::PBRMaterial& mat, int& albedoIdx, int& normalIdx, int& rmaIdx, int& aoIdx, int& emissiveIdx) {
                        if(EngineUI::DrawMaterialAsset(mat,*m_renderer)){albedoIdx=normalIdx=rmaIdx=aoIdx=emissiveIdx=-1;sceneDirty=true;}
                        if (ImGui::TreeNode("PBR Material & Textures")) {
                            const char* presets[] = {
                                "Custom",
                                "Water",
                                "Mirror / Chrome",
                                "Gold",
                                "Copper",
                                "Silver",
                                "Glass",
                                "Plastic",
                                "Wood",
                                "Concrete / Stone",
                                "Rubber",
                                "Car Paint"
                            };
                            int currentPreset = static_cast<int>(mat.preset);
                            if (ImGui::Combo("Preset", &currentPreset, presets, IM_ARRAYSIZE(presets))) {
                                mat.ApplyPreset(static_cast<Renderer::MaterialPreset>(currentPreset));
                                sceneDirty = true;
                            }

                            ImGui::Separator();
                            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "PBR Parameters:");

                            float col[3] = { mat.albedo.x, mat.albedo.y, mat.albedo.z };
                            if (ImGui::ColorEdit3("Albedo Color", col)) {
                                mat.albedo = XMFLOAT3(col[0], col[1], col[2]);
                                mat.preset = Renderer::MaterialPreset::Custom;
                                sceneDirty = true;
                            }

                            if (ImGui::SliderFloat("Roughness", &mat.roughness, 0.0f, 1.0f, "%.3f (0=Glossy/Mirror, 1=Rough)")) {
                                mat.preset = Renderer::MaterialPreset::Custom;
                                sceneDirty = true;
                            }
                            if (ImGui::SliderFloat("Metallic", &mat.metallic, 0.0f, 1.0f, "%.3f (0=Dielectric, 1=Metallic)")) {
                                mat.preset = Renderer::MaterialPreset::Custom;
                                sceneDirty = true;
                            }
                            if (ImGui::SliderFloat("Reflectance", &mat.reflectance, 0.0f, 1.0f, "%.2f")) {
                                mat.preset = Renderer::MaterialPreset::Custom;
                                sceneDirty = true;
                            }
                            if (ImGui::SliderFloat("Emission", &mat.emission, 0.0f, 10.0f, "%.2f")) {
                                sceneDirty = true;
                            }
                            if (ImGui::SliderFloat("IOR", &mat.ior, 1.0f, 3.0f, "%.3f")) {
                                sceneDirty = true;
                            }
                            float tiling[2] = { mat.uvTiling.x, mat.uvTiling.y };
                            if (ImGui::DragFloat2("UV Tiling", tiling, 0.1f, 0.01f, 50.0f, "%.2f")) {
                                mat.uvTiling = XMFLOAT2(tiling[0], tiling[1]);
                                sceneDirty = true;
                            }

                            ImGui::Separator();
                            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f), "PBR Texture Slots:");

                            auto DrawTextureSlot = [&](const char* slotName, std::string& path, std::shared_ptr<Texture>& tex, int& idx) {
                                ImGui::PushID(slotName);
                                ImGui::Text("%s:", slotName);
                                char pathBuf[260];
                                strncpy_s(pathBuf, sizeof(pathBuf), path.c_str(), _TRUNCATE);
                                ImGui::SetNextItemWidth(200);
                                if (ImGui::InputText("##Path", pathBuf, sizeof(pathBuf))) {
                                    path = pathBuf;
                                }
                                if(ImGui::BeginDragDropTarget()){
                                    if(auto payload=ImGui::AcceptDragDropPayload("IDK_ASSET")){
                                        std::filesystem::path dropped=std::filesystem::u8path(static_cast<const char*>(payload->Data));
                                        auto ext=dropped.extension().string();std::transform(ext.begin(),ext.end(),ext.begin(),::tolower);
                                        if(ext==".png"||ext==".jpg"||ext==".jpeg"||ext==".bmp"||ext==".tga"||ext==".dds"){path=dropped.generic_string();tex=m_renderer->LoadTexture(path);idx=-1;sceneDirty=true;}
                                    }
                                    ImGui::EndDragDropTarget();
                                }
                                ImGui::SameLine();
                                if (ImGui::Button("Load")) {
                                    if (!path.empty()) {
                                        tex = m_renderer->ReloadTexture(path);
                                        sceneDirty = true;
                                    }
                                }
#if defined(_WIN32)
                                ImGui::SameLine();
                                if(ImGui::Button("Browse...")) {
                                    wchar_t selected[32768] = {};
                                    OPENFILENAMEW dialog = {};
                                    dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=m_window->GetHWND();
                                    dialog.lpstrFile=selected;dialog.nMaxFile=32768;
                                    dialog.lpstrFilter=L"Textures\0*.png;*.jpg;*.jpeg;*.bmp;*.tga\0\0";
                                    dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
                                    if(GetOpenFileNameW(&dialog)) {
                                        try {
                                            path=EngineAsset::ImportTextureAsset(std::filesystem::path(selected),
                                                std::filesystem::path(EnginePlatform::PlatformUtils::ResolveContentPath("Textures")));
                                            tex=m_renderer->LoadTexture(path);sceneDirty=true;
                                        } catch(const std::exception& error) { EnginePlatform::PlatformUtils::ShowAlert("Texture import",error.what(),true); }
                                    }
                                }
#endif
                                ImGui::SameLine();
                                if (ImGui::Button("Clear")) {
                                    path.clear();
                                    tex.reset();
                                    idx = -1;
                                    sceneDirty = true;
                                }
                                if (tex) {
                                    if (tex->GetState() == TextureState::Ready) {
                                        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "  [Ready: SRV #%u (%dx%d)]", tex->GetSrvIndex(), tex->GetWidth(), tex->GetHeight());
                                    } else if (tex->GetState() == TextureState::Loading || tex->GetState() == TextureState::Uploading) {
                                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "  [Loading...]");
                                    } else if (tex->GetState() == TextureState::Error) {
                                        ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "  [Error loading file]");
                                    }
                                }
                                ImGui::PopID();
                            };

                            DrawTextureSlot("1. Albedo / Base Color Map", mat.albedoPath, mat.albedoMap, albedoIdx);
                            DrawTextureSlot("2. Normal Map", mat.normalPath, mat.normalMap, normalIdx);
                            DrawTextureSlot("3. Roughness / RMA Map", mat.rmaPath, mat.rmaMap, rmaIdx);
                            DrawTextureSlot("4. Ambient Occlusion (AO)", mat.aoPath, mat.aoMap, aoIdx);
                            DrawTextureSlot("5. Emissive Map", mat.emissivePath, mat.emissiveMap, emissiveIdx);

                            if (ImGui::Button("Apply Demo Texture (Albedo)")) {
                                mat.albedoPath = "Content/DemoTexture/demo_texture.jpg";
                                mat.albedoMap = m_renderer->LoadTexture(mat.albedoPath);
                                sceneDirty = true;
                            }

                            ImGui::TreePop();
                        }
                    };

                    auto DrawPhysicsEditor = [&](auto& model, bool isDyn) {
                        if (!ImGui::CollapsingHeader("Collider & Rigidbody", ImGuiTreeNodeFlags_DefaultOpen)) return;
                        auto character=EngineAnim::AnimationSystem::Get().FindCharacterForModel(m_renderer.get(),model.modelId);
                        if(character&&character->ragdoll.Active()){ImGui::TextWrapped("Ragdoll controls this object. Disable ragdoll to edit the mesh Rigidbody.");return;}
                        bool changed = false;
                        const bool simulating=model.hasCollision&&model.physicsBodyType==2;
                        if(ImGui::Button(simulating?"Disable physics (make static)":"Enable physics")){
                            model.hasCollision=true;model.physicsBodyType=simulating?1:2;
                            if(!simulating){model.physicsEnableGravity=true;model.physicsEnableCCD=true;if(model.physicsShapeType==5)model.physicsShapeType=4;}
                            changed=true;
                        }
                        auto& physics=EnginePhysics::PhysicsSystem::Get();
                        if(model.hasCollision&&model.physicsBodyType==2){
                            ImGui::TextUnformatted(physics.IsPaused()?"Simulation is paused":"Simulation is running");
                            if(ImGui::SmallButton(physics.IsPaused()?"Run physics":"Pause physics"))physics.SetPaused(!physics.IsPaused());
                            ImGui::SameLine();if(ImGui::SmallButton("Simulate one step"))physics.SingleStep();
                        }
                        changed |= ImGui::Checkbox("Enable collision", &model.hasCollision);
                        if (model.hasCollision) {
                            const char* bodies[] = {"Static obstacle", "Dynamic body", "Kinematic body"};
                            int body = (std::clamp)(model.physicsBodyType - 1, 0, 2);
                            if (ImGui::Combo("Motion", &body, bodies, 3)) { model.physicsBodyType = body + 1; changed = true; }
                            const char* shapes[] = {"Box (fit mesh)", "Sphere", "Capsule", "Convex mesh", "Triangle mesh (Static)"};
                            const int values[] = {0,1,2,4,5};
                            int shape = 0;
                            for(int i=0;i<5;++i) if(model.physicsShapeType==values[i]) shape=i;
                            if (ImGui::Combo("Shape", &shape, shapes, 5)) { model.physicsShapeType=values[shape]; changed=true; }
                            if (model.physicsBodyType > 1 && model.physicsShapeType == 5) {
                                model.physicsShapeType = 4; changed = true;
                                ImGui::TextWrapped("Moving bodies use a convex collider. Triangle meshes are for static scenery.");
                            }
                            changed |= ImGui::DragFloat3("Center offset", &model.collider.offset.x, .02f);
                            changed |= ImGui::DragFloat3("Size multiplier", &model.collider.size.x, .02f, .01f, 100.0f);
                            if (ImGui::Button("Fit to mesh")) { model.collider = {}; changed = true; }
                            ImGui::SameLine();
                            if (ImGui::Button("Rebuild collider")) changed = true;
                            changed |= ImGui::SliderFloat("Friction", &model.physicsFriction, 0, 1);
                            changed |= ImGui::SliderFloat("Bounciness", &model.physicsRestitution, 0, 1);
                            if (model.physicsBodyType == 2) {
                                changed |= ImGui::DragFloat("Mass (kg)", &model.physicsMass, .1f, .1f, 10000);
                                changed |= ImGui::Checkbox("Gravity", &model.physicsEnableGravity);
                                changed |= ImGui::Checkbox("Continuous collision", &model.physicsEnableCCD);
                                changed |= ImGui::DragFloat("Linear damping", &model.physicsLinearDamping,.01f,0,20,"%.2f",ImGuiSliderFlags_AlwaysClamp);
                                changed |= ImGui::DragFloat("Angular damping", &model.physicsAngularDamping,.01f,0,20,"%.2f",ImGuiSliderFlags_AlwaysClamp);
                                static XMFLOAT3 force={0,100,0};
                                ImGui::DragFloat3("Force / impulse XYZ",&force.x,1);
                                if(ImGui::Button("Apply force (N)"))EnginePhysics::PhysicsSystem::Get().AddForce(model.modelId,force);
                                ImGui::SameLine();if(ImGui::Button("Apply impulse (N s)"))EnginePhysics::PhysicsSystem::Get().AddImpulse(model.modelId,force);
                            }
                        }
                        if (changed) {
                            EnginePhysics::PhysicsSystem::Get().ApplyModelPhysics(m_renderer.get(), model.modelId, isDyn);
                            sceneDirty = true;
                        }
                        const auto status = EnginePhysics::PhysicsSystem::Get().GetColliderStatus(model.modelId);
                        ImGui::TextWrapped("%s", status.c_str());
                    };

                    ImGui::SeparatorText("Scene hierarchy");
                    static ImGuiTextFilter objectFilter;
                    objectFilter.Draw("Search");
                    if (ImGui::BeginListBox("##Hierarchy", ImVec2(-1, 190))) {
                        for(size_t i=0;i<models.size();++i) if(objectFilter.PassFilter(models[i].name.c_str())) {
                            ImGui::PushID(models[i].modelId);
                            if(ImGui::Selectable(models[i].name.c_str(),s_selectedModelType==1 && s_selectedModelIndex==(int)i)) {
                                s_selectedModelType=1;s_selectedModelIndex=(int)i;
                            }
                            ImGui::PopID();
                        }
                        for(size_t i=0;i<dynModels.size();++i) if(objectFilter.PassFilter(dynModels[i].name.c_str())) {
                            ImGui::PushID(dynModels[i].modelId);
                            if(ImGui::Selectable(dynModels[i].name.c_str(),s_selectedModelType==2 && s_selectedModelIndex==(int)i)) {
                                s_selectedModelType=2;s_selectedModelIndex=(int)i;
                            }
                            ImGui::PopID();
                        }
                        ImGui::EndListBox();
                    }
                    if (ImGui::Button("Save scene")) SaveScene();
                    ImGui::SameLine();
                    if (ImGui::Button("Reload scene")) { LoadScene(); s_selectedModelType=0; s_selectedModelIndex=-1; }
                    ImGui::TextWrapped("Select an object here or in the viewport. Edit it in Inspector. TAB switches between editing and playing.");

                    static bool showColliderPreview = true;
                    ImGui::SetNextWindowPos(ImVec2((float)m_width-380, 55), ImGuiCond_FirstUseEver);
                    ImGui::SetNextWindowSize(ImVec2(365,(float)m_height-80),ImGuiCond_FirstUseEver);
                    if (ImGui::Begin("Inspector")) {
                        // Loaded layouts may come from a larger monitor; keep the entire Inspector reachable.
                        auto size=ImGui::GetWindowSize();size.x=(std::min)(size.x,float(m_width));size.y=(std::min)(size.y,float(m_height)-30);
                        auto pos=ImGui::GetWindowPos();pos.x=(std::clamp)(pos.x,0.f,(std::max)(0.f,float(m_width)-size.x));pos.y=(std::clamp)(pos.y,25.f,(std::max)(25.f,float(m_height)-size.y));
                        ImGui::SetWindowPos(pos);ImGui::SetWindowSize(size);
                        ImGui::PushItemWidth(-140);
                        auto drawLight=[&](auto& light) {
                            char name[128];strncpy_s(name,sizeof(name),light.name.c_str(),_TRUNCATE);
                            if(ImGui::InputText("Name",name,sizeof(name)))light.name=name;
                            ImGui::Checkbox("Enabled",&light.enabled);
                            ImGui::DragFloat3("Position",&light.position.x,.05f);
                            ImGui::ColorEdit3("Light color",&light.color.x);
                            ImGui::DragFloat("Intensity",&light.intensity,.1f,0,10000);
                            ImGui::DragFloat("Range (m)",&light.range,.1f,.01f,10000);
                            if constexpr(std::is_same_v<std::decay_t<decltype(light)>,Renderer::SpotLight>) {
                                ImGui::DragFloat3("Direction",&light.direction.x,.01f,-1,1);
                                float length=light.direction.x*light.direction.x+light.direction.y*light.direction.y+light.direction.z*light.direction.z;
                                if(length<.00001f)light.direction={0,-1,0};
                                ImGui::SliderFloat("Inner cone",&light.innerAngle,0,light.outerAngle);
                                ImGui::SliderFloat("Outer cone",&light.outerAngle,light.innerAngle,89);
                                if(ImGui::Button("Aim from viewport")){light.position=m_camera->GetPosition();light.direction=m_camera->GetForwardVector();}
                            }
                            if(ImGui::Button("Delete light")) {
                                if constexpr(std::is_same_v<std::decay_t<decltype(light)>,Renderer::SpotLight>) m_renderer->GetSpotLights().erase(m_renderer->GetSpotLights().begin()+s_selectedModelIndex);
                                else m_renderer->GetPointLights().erase(m_renderer->GetPointLights().begin()+s_selectedModelIndex);
                                s_selectedModelType=0;s_selectedModelIndex=-1;
                            }
                        };
                        bool deleteInspectorObject = false;
                        auto drawSelected = [&](auto& model, bool dynamic) {
                            ImGui::PushID(model.modelId);
                            ImGui::TextUnformatted(model.name.c_str());
                            if(ImGui::Button("Focus object")) {
                                XMFLOAT3 lo=model.position,hi=model.position;
                                EnginePhysics::PhysicsSystem::Get().GetColliderBounds(model.modelId,lo,hi);
                                XMFLOAT3 center={(lo.x+hi.x)*.5f,(lo.y+hi.y)*.5f,(lo.z+hi.z)*.5f};
                                float distance=(std::clamp)((std::max)({hi.x-lo.x,hi.y-lo.y,hi.z-lo.z})*1.5f,3.0f,500.0f);
                                m_camera->SetPosition(center.x-distance*.7f,center.y+distance*.5f,center.z-distance);
                                m_camera->SetYaw(std::atan2(.7f,1.0f));m_camera->SetPitch(std::atan2(-.5f,std::sqrt(1.49f)));
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Delete object")) deleteInspectorObject = true;
                            bool transform = false;
                            bool physics = ImGui::Checkbox("Visible", &model.visible);
                            auto selectedCharacter=EngineAnim::AnimationSystem::Get().FindCharacterForModel(m_renderer.get(),model.modelId);
                            bool ragdollActive=selectedCharacter&&selectedCharacter->ragdoll.Active();
                            ImGui::BeginDisabled(ragdollActive);
                            if(ImGui::CollapsingHeader("Transform",ImGuiTreeNodeFlags_DefaultOpen)) {
                                transform |= ImGui::DragFloat3("Position",&model.position.x,.05f);
                                transform |= ImGui::DragFloat3("Rotation (degrees)",&model.rotation.x,.5f);
                                transform |= ImGui::DragFloat3("Scale",&model.scale.x,.02f,.001f,1000);
                            }
                            ImGui::EndDisabled();
                            if (transform || physics) {
                                if(transform)EngineScene::SceneGraph::Get().SyncNodeFromModel(m_renderer.get(),model.modelId);
                                EnginePhysics::PhysicsSystem::Get().ApplyModelPhysics(m_renderer.get(),model.modelId,dynamic);
                                m_renderer->InvalidateRaytracingScene(false); sceneDirty=true;
                            }
                            ImGui::Checkbox("Show collider", &showColliderPreview);
                            if(EngineAnim::AnimationSystem::Get().DrawRagdollInspector(m_renderer.get(),model.modelId))sceneDirty=true;
                            DrawPhysicsEditor(model,dynamic);
                            if(model.vehicle.part==0&&ImGui::CollapsingHeader("Vehicle")){
                                bool changed=ImGui::Checkbox("Vehicle suspension and tires",&model.vehicle.enabled);
                                auto& v=model.vehicle;
                                changed|=ImGui::DragFloat("Wheelbase (m)",&v.wheelbase,.02f,.5f,10);
                                changed|=ImGui::DragFloat("Track width (m)",&v.track,.02f,.5f,6);
                                changed|=ImGui::DragFloat("Wheel radius (m)",&v.radius,.01f,.1f,2);
                                changed|=ImGui::DragFloat("Suspension rest (m)",&v.restLength,.01f,.1f,2);
                                changed|=ImGui::DragFloat("Suspension travel (m)",&v.travel,.01f,.02f,.8f);
                                changed|=ImGui::DragFloat("Spring (N/m)",&v.spring,100,1000,200000);
                                changed|=ImGui::DragFloat("Damper (Ns/m)",&v.damping,20,100,20000);
                                changed|=ImGui::DragFloat("Tire grip",&v.grip,.02f,.1f,3);

                                changed|=ImGui::DragFloat("Brakes (N)",&v.brakeForce,100,0,100000);
                                changed|=ImGui::DragFloat("Maximum speed (m/s)",&v.maxSpeed,.5f,1,100);
                                changed|=ImGui::DragFloat("Steering limit (degrees)",&v.steerDegrees,1,1,60);
                                if(ImGui::TreeNode("Engine and transmission")){
                                    changed|=ImGui::DragFloat("Peak torque (Nm)",&v.peakTorque,5,20,2000);
                                    changed|=ImGui::DragFloat("Idle RPM",&v.idleRPM,25,400,2000);
                                    changed|=ImGui::DragFloat("Redline RPM",&v.redlineRPM,50,3000,12000);
                                    changed|=ImGui::DragFloat("Engine inertia (kg m2)",&v.engineInertia,.01f,.05f,3);
                                    changed|=ImGui::DragFloat("Clutch strength",&v.clutchStrength,.25f,1,100);
                                    changed|=ImGui::Checkbox("Automatic gearbox",&v.automaticGears);
                                    changed|=ImGui::DragFloat("Shift time (s)",&v.shiftTime,.01f,.05f,2);
                                    changed|=ImGui::DragFloat("Final drive ratio",&v.finalDrive,.05f,1,8);
                                    for(int g=0;g<6;++g){ImGui::PushID(g);changed|=ImGui::DragFloat(("Gear "+std::to_string(g+1)).c_str(),&v.gears[g],.02f,.2f,8);ImGui::PopID();}
                                    changed|=ImGui::DragFloat("Reverse ratio",&v.reverseRatio,.05f,1,6);
                                    changed|=ImGui::SliderFloat("Front drive split",&v.frontDrive,0,1);
                                    changed|=ImGui::DragFloat("Limited slip bias",&v.differentialBias,.05f,1.1f,8);
                                    ImGui::TextWrapped("Front split 0 = RWD, 1 = FWD, between = AWD. Manual: Page Up / Page Down; gear 0 is neutral.");ImGui::TreePop();
                                }
                                if(ImGui::TreeNode("Tires, brakes and chassis")){
                                    changed|=ImGui::DragFloat("Wheel width (m)",&v.wheelWidth,.01f,.1f,.8f);
                                    changed|=ImGui::DragFloat("Wheel mass (kg)",&v.wheelMass,.5f,5,100);
                                    changed|=ImGui::DragFloat("Longitudinal tire stiffness",&v.longitudinalStiffness,500,1000,100000);
                                    changed|=ImGui::DragFloat("Lateral tire stiffness",&v.lateralStiffness,500,5000,150000);
                                    changed|=ImGui::SliderFloat("Surface wetness",&v.wetness,0,1);
                                    changed|=ImGui::Checkbox("Weather affects tire grip",&v.weatherGrip);
                                    changed|=ImGui::Checkbox("ABS",&v.abs);changed|=ImGui::Checkbox("Traction control",&v.tractionControl);
                                    changed|=ImGui::SliderFloat("Front brake bias",&v.frontBrakeBias,.3f,.8f);
                                    changed|=ImGui::DragFloat("Front anti-roll (N/m)",&v.antiRollFront,100,0,50000);
                                    changed|=ImGui::DragFloat("Rear anti-roll (N/m)",&v.antiRollRear,100,0,50000);
                                    changed|=ImGui::DragFloat("Center of mass Y (m)",&v.comHeight,.01f,-.5f,.5f);
                                    changed|=ImGui::DragFloat("Center of mass Z (m)",&v.comForward,.01f,-.5f,.5f);
                                    changed|=ImGui::SliderFloat("Ackermann blend",&v.ackermann,0,1);
                                    changed|=ImGui::DragFloat("Steering rate (rad/s)",&v.steerRate,.1f,.2f,10);
                                    changed|=ImGui::DragFloat("Rolling resistance",&v.rollingResistance,.001f,0,.1f);
                                    changed|=ImGui::DragFloat("Drag CdA (m2)",&v.dragArea,.01f,.1f,3);
                                    changed|=ImGui::DragFloat("Downforce ClA (m2)",&v.downforceArea,.01f,0,5);ImGui::TreePop();
                                }
                                auto& vehiclePhysics=EnginePhysics::PhysicsSystem::Get();
                                ImGui::Text("%.1f km/h | %.0f RPM | gear %d | contacts %d | slip %.2f",vehiclePhysics.VehicleSpeed(model.modelId)*3.6f,vehiclePhysics.VehicleRPM(model.modelId),vehiclePhysics.VehicleGear(model.modelId),vehiclePhysics.VehicleContacts(model.modelId),vehiclePhysics.VehicleSlip(model.modelId));

                                if(changed){v.Sanitize();if(v.enabled){model.hasCollision=true;model.physicsBodyType=2;model.physicsShapeType=0;}EnginePhysics::PhysicsSystem::Get().ApplyModelPhysics(m_renderer.get(),model.modelId,dynamic);sceneDirty=true;}
                                ImGui::TextWrapped("Play: V near a car to enter/exit. W/S accelerate, brake and reverse; A/D steer; Space handbrake. TAB exits driving and preserves the pose. Chassis mass is in Physics.");
                            }
                            if(EngineUI::DrawGameplayInspector(model.gameplay))sceneDirty=true;
                            if(EngineUI::DrawPrefabInspector(model,*m_renderer)){EnginePhysics::PhysicsSystem::Get().ApplyModelPhysics(m_renderer.get(),model.modelId,dynamic);sceneDirty=true;}
                            ImGui::SetNextItemOpen(true,ImGuiCond_Once);
                            DrawMaterialEditor(model.material,model.albedoIndex,model.normalIndex,model.rmaIndex,model.aoIndex,model.emissiveIndex);
                            if(showColliderPreview && model.hasCollision) {
                                auto lines=EnginePhysics::PhysicsSystem::Get().GetColliderWireframe(model.modelId);
                                XMMATRIX vp=m_camera->GetViewMatrix()*m_camera->GetCleanProjectionMatrix(XMConvertToRadians(m_renderer->GetFOV()),(float)m_width/m_height,.1f,2000);
                                for(size_t i=0;i+1<lines.size();i+=2) {
                                    XMVECTOR a=XMVector4Transform(XMVectorSet(lines[i].x,lines[i].y,lines[i].z,1),vp);
                                    XMVECTOR b=XMVector4Transform(XMVectorSet(lines[i+1].x,lines[i+1].y,lines[i+1].z,1),vp);
                                    if(XMVectorGetW(a)<=.1f || XMVectorGetW(b)<=.1f)continue;
                                    a=XMVectorScale(a,1/XMVectorGetW(a)); b=XMVectorScale(b,1/XMVectorGetW(b));
                                    ImGui::GetForegroundDrawList()->AddLine(ImVec2((XMVectorGetX(a)*.5f+.5f)*m_width,(-XMVectorGetY(a)*.5f+.5f)*m_height),
                                        ImVec2((XMVectorGetX(b)*.5f+.5f)*m_width,(-XMVectorGetY(b)*.5f+.5f)*m_height),IM_COL32(70,240,130,220),1.5f);
                                }
                            }
                            ImGui::PopID();
                        };
                        if(s_selectedModelType==1 && s_selectedModelIndex>=0 && s_selectedModelIndex<(int)models.size()) drawSelected(models[s_selectedModelIndex],false);
                        else if(s_selectedModelType==2 && s_selectedModelIndex>=0 && s_selectedModelIndex<(int)dynModels.size()) drawSelected(dynModels[s_selectedModelIndex],true);
                        else if(s_selectedModelType==3 && s_selectedModelIndex>=0 && s_selectedModelIndex<(int)m_renderer->GetPointLights().size()) drawLight(m_renderer->GetPointLights()[s_selectedModelIndex]);
                        else if(s_selectedModelType==4 && s_selectedModelIndex>=0 && s_selectedModelIndex<(int)m_renderer->GetSpotLights().size()) drawLight(m_renderer->GetSpotLights()[s_selectedModelIndex]);
                        else ImGui::TextWrapped("Select an object in Scene hierarchy or click it in the viewport.");
                        if (deleteInspectorObject) {
                            if (s_selectedModelType == 1 && s_selectedModelIndex >= 0 && s_selectedModelIndex < (int)models.size()) {
                                auto assembly=models[s_selectedModelIndex].vehicle.part==0?models[s_selectedModelIndex].vehicle.assembly:std::string();
                                auto rootName=models[s_selectedModelIndex].name;
                                EnginePhysics::PhysicsSystem::Get().RemoveActorForModel(models[s_selectedModelIndex].modelId);
                                models.erase(models.begin() + s_selectedModelIndex);
                                if(!assembly.empty()){
                                    std::erase_if(models,[&](const auto& part){if(part.vehicle.assembly!=assembly)return false;EnginePhysics::PhysicsSystem::Get().RemoveActorForModel(part.modelId);return true;});
                                    if(auto node=EngineScene::SceneGraph::Get().FindNode(rootName))EngineScene::SceneGraph::Get().RemoveNode(node->GetId());
                                }
                            } else if (s_selectedModelType == 2 && s_selectedModelIndex >= 0 && s_selectedModelIndex < (int)dynModels.size()) {
                                EnginePhysics::PhysicsSystem::Get().RemoveActorForModel(dynModels[s_selectedModelIndex].modelId);
                                for(auto& character:EngineAnim::AnimationSystem::Get().GetCharacters())if(character){
                                    if(character->renderModelIndex==s_selectedModelIndex)character->renderModelIndex=-1;
                                    else if(character->renderModelIndex>s_selectedModelIndex)--character->renderModelIndex;
                                }
                                dynModels.erase(dynModels.begin() + s_selectedModelIndex);
                            }
                            s_selectedModelType = 0;
                            s_selectedModelIndex = -1;
                            m_renderer->InvalidateRaytracingScene();
                            sceneDirty = true;
                        }
                        ImGui::PopItemWidth();
                    }
                    ImGui::End();

                    if (ImGui::CollapsingHeader("Advanced object controls")) {
                    for (size_t i = 0; i < models.size(); ++i) {
                        ImGui::PushID(static_cast<int>(i));
                        if (ImGui::TreeNode(models[i].name.c_str())) {
                            bool isSelectedGizmo = (s_selectedModelType == 1 && s_selectedModelIndex == (int)i);
                            if (isSelectedGizmo) {
                                ImGui::TextColored(ImVec4(0.2f, 0.95f, 0.3f, 1.0f), " [Active 3D Gizmo]");
                                ImGui::SameLine();
                            } else {
                                if (ImGui::SmallButton(" Select Gizmo")) {
                                    s_selectedModelType = 1;
                                    s_selectedModelIndex = (int)i;
                                }
                                ImGui::SameLine();
                            }

                            if (ImGui::Checkbox("Visible", &models[i].visible)) {
                                m_renderer->InvalidateRaytracingScene();
                                sceneDirty = true;
                            }
                            ImGui::SameLine();
                            if (ImGui::Checkbox("Collision", &models[i].hasCollision)) { EnginePhysics::PhysicsSystem::Get().ApplyModelPhysics(m_renderer.get(),models[i].modelId,false);sceneDirty=true; }
                            ImGui::SameLine();
                            if (ImGui::Button("Delete")) {
                                if (s_selectedModelType == 1 && s_selectedModelIndex == (int)i) {
                                    s_selectedModelType = 0;
                                    s_selectedModelIndex = -1;
                                } else if (s_selectedModelType == 1 && s_selectedModelIndex > (int)i) {
                                    s_selectedModelIndex--;
                                }
                                EnginePhysics::PhysicsSystem::Get().RemoveActorForModel(models[i].modelId);
                                models.erase(models.begin() + i);
                                m_renderer->InvalidateRaytracingScene();
                                ImGui::TreePop();
                                ImGui::PopID();
                                i--;
                                sceneDirty = true;
                                continue;
                            }
                            
                            float pos[3] = { models[i].position.x, models[i].position.y, models[i].position.z };
                            if (ImGui::DragFloat3("Position", pos, 0.1f)) {
                                models[i].position = XMFLOAT3(pos[0], pos[1], pos[2]);
                                EnginePhysics::PhysicsSystem::Get().SetActorTransform(models[i].modelId, models[i].position, models[i].rotation);
                                m_renderer->InvalidateRaytracingScene();
                                sceneDirty = true;
                            }
                            
                            float rot[3] = { models[i].rotation.x, models[i].rotation.y, models[i].rotation.z };
                            if (ImGui::DragFloat3("Rotation", rot, 1.0f)) {
                                models[i].rotation = XMFLOAT3(rot[0], rot[1], rot[2]);
                                EnginePhysics::PhysicsSystem::Get().SetActorTransform(models[i].modelId, models[i].position, models[i].rotation);
                                m_renderer->InvalidateRaytracingScene();
                                sceneDirty = true;
                            }

                            float scale[3] = { models[i].scale.x, models[i].scale.y, models[i].scale.z };
                            if (ImGui::DragFloat3("Scale", scale, 0.05f)) {
                                models[i].scale = XMFLOAT3(scale[0], scale[1], scale[2]);
                                EnginePhysics::PhysicsSystem::Get().ApplyModelPhysics(m_renderer.get(),models[i].modelId,false);
                                m_renderer->InvalidateRaytracingScene();
                                sceneDirty = true;
                            }

                            if (models[i].name != "Floor") {
                                ImGui::Separator();
                                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "Melting / Liquefaction Physics:");
                                if (models[i].isMelting) {
                                    if (ImGui::Button(" Solidify / Restore")) {
                                        models[i].ResetMelting();
                                        m_renderer->InvalidateRaytracingScene();
                                        sceneDirty = true;
                                    }
                                } else {
                                    if (ImGui::Button(" Melt to Floor")) {
                                        models[i].StartMelting();
                                        m_renderer->InvalidateRaytracingScene();
                                        sceneDirty = true;
                                    }
                                }
                                ImGui::SameLine();
                                ImGui::ProgressBar(models[i].meltProgress, ImVec2(120, 0), "Melted");
                                if (ImGui::SliderFloat("Melt Speed", &models[i].meltSpeed, 0.1f, 3.0f, "%.2fx")) {
                                    sceneDirty = true;
                                }
                            }

                            DrawMaterialEditor(models[i].material, models[i].albedoIndex, models[i].normalIndex, models[i].rmaIndex, models[i].aoIndex, models[i].emissiveIndex);
                            DrawPhysicsEditor(models[i], false);
                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    }

                    for (size_t i = 0; i < dynModels.size(); ++i) {
                        ImGui::PushID(static_cast<int>(i + 1000)); // offset ID to avoid conflict
                        if (ImGui::TreeNode(dynModels[i].name.c_str())) {
                            bool isSelectedGizmo = (s_selectedModelType == 2 && s_selectedModelIndex == (int)i);
                            if (isSelectedGizmo) {
                                ImGui::TextColored(ImVec4(0.2f, 0.95f, 0.3f, 1.0f), " [Active 3D Gizmo]");
                                ImGui::SameLine();
                            } else {
                                if (ImGui::SmallButton(" Select Gizmo")) {
                                    s_selectedModelType = 2;
                                    s_selectedModelIndex = (int)i;
                                }
                                ImGui::SameLine();
                            }

                            if (ImGui::Checkbox("Visible", &dynModels[i].visible)) {
                                m_renderer->InvalidateRaytracingScene();
                                sceneDirty = true;
                            }
                            ImGui::SameLine();
                            if (ImGui::Checkbox("Collision", &dynModels[i].hasCollision)) { EnginePhysics::PhysicsSystem::Get().ApplyModelPhysics(m_renderer.get(),dynModels[i].modelId,true);sceneDirty=true; }
                            ImGui::SameLine();
                            if (ImGui::Button("Delete")) {
                                if (s_selectedModelType == 2 && s_selectedModelIndex == (int)i) {
                                    s_selectedModelType = 0;
                                    s_selectedModelIndex = -1;
                                } else if (s_selectedModelType == 2 && s_selectedModelIndex > (int)i) {
                                    s_selectedModelIndex--;
                                }
                                EnginePhysics::PhysicsSystem::Get().RemoveActorForModel(dynModels[i].modelId);
                                dynModels.erase(dynModels.begin() + i);
                                m_renderer->InvalidateRaytracingScene();
                                ImGui::TreePop();
                                ImGui::PopID();
                                i--;
                                sceneDirty = true;
                                continue;
                            }
                            
                            float pos[3] = { dynModels[i].position.x, dynModels[i].position.y, dynModels[i].position.z };
                            if (ImGui::DragFloat3("Position", pos, 0.1f)) {
                                dynModels[i].position = XMFLOAT3(pos[0], pos[1], pos[2]);
                                EnginePhysics::PhysicsSystem::Get().SetActorTransform(dynModels[i].modelId, dynModels[i].position, dynModels[i].rotation);
                                m_renderer->InvalidateRaytracingScene();
                                sceneDirty = true;
                            }
                            
                            float rot[3] = { dynModels[i].rotation.x, dynModels[i].rotation.y, dynModels[i].rotation.z };
                            if (ImGui::DragFloat3("Rotation", rot, 1.0f)) {
                                dynModels[i].rotation = XMFLOAT3(rot[0], rot[1], rot[2]);
                                EnginePhysics::PhysicsSystem::Get().SetActorTransform(dynModels[i].modelId, dynModels[i].position, dynModels[i].rotation);
                                m_renderer->InvalidateRaytracingScene();
                                sceneDirty = true;
                            }

                            float scale[3] = { dynModels[i].scale.x, dynModels[i].scale.y, dynModels[i].scale.z };
                            if (ImGui::DragFloat3("Scale", scale, 0.05f)) {
                                dynModels[i].scale = XMFLOAT3(scale[0], scale[1], scale[2]);
                                EnginePhysics::PhysicsSystem::Get().ApplyModelPhysics(m_renderer.get(),dynModels[i].modelId,true);
                                m_renderer->InvalidateRaytracingScene();
                                sceneDirty = true;
                            }

                            ImGui::Separator();
                            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "Melting / Liquefaction Physics:");
                            if (dynModels[i].isMelting) {
                                if (ImGui::Button(" Solidify / Restore")) {
                                    dynModels[i].ResetMelting();
                                    m_renderer->InvalidateRaytracingScene();
                                    sceneDirty = true;
                                }
                            } else {
                                if (ImGui::Button(" Melt to Floor")) {
                                    dynModels[i].StartMelting();
                                    m_renderer->InvalidateRaytracingScene();
                                    sceneDirty = true;
                                }
                            }
                            ImGui::SameLine();
                            ImGui::ProgressBar(dynModels[i].meltProgress, ImVec2(120, 0), "Melted");
                            if (ImGui::SliderFloat("Melt Speed", &dynModels[i].meltSpeed, 0.1f, 3.0f, "%.2fx")) {
                                sceneDirty = true;
                            }

                            DrawMaterialEditor(dynModels[i].material, dynModels[i].albedoIndex, dynModels[i].normalIndex, dynModels[i].rmaIndex, dynModels[i].aoIndex, dynModels[i].emissiveIndex);
                            DrawPhysicsEditor(dynModels[i], true);
                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    }
                    
                    } // Advanced object controls
                    ImGui::End();
                    if (s_showLightsUI) {
                        ImGui::SetNextWindowSize(ImVec2(420, 500), ImGuiCond_FirstUseEver);
                        if (ImGui::Begin(" Local Lights Studio", &s_showLightsUI)) {
                            auto& plList = m_renderer->GetPointLights();
                            auto& slList = m_renderer->GetSpotLights();

                            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Point Lights (%d / %d):", (int)plList.size(), (int)EngineRender::MAX_POINT_LIGHTS);
                            ImGui::SameLine();
                            if (ImGui::Button(" Add Point Light") && plList.size() < EngineRender::MAX_POINT_LIGHTS) {
                                EngineRender::PointLight pl;
                                pl.name = "PointLight_" + std::to_string(plList.size() + 1);
                                pl.position = m_camera->GetPosition();
                                pl.color = XMFLOAT3(1.0f, 0.85f, 0.6f);
                                pl.intensity = 3.0f;
                                pl.range = 12.0f;
                                pl.enabled = true;
                                plList.push_back(pl);
                            }

                            int removePl = -1;
                            for (int i = 0; i < (int)plList.size(); ++i) {
                                auto& pl = plList[i];
                                ImGui::PushID(i + 3000);
                                char title[64];
                                snprintf(title, sizeof(title), " %s##%d", pl.name.c_str(), i);
                                if (ImGui::TreeNode(title)) {
                                    ImGui::Checkbox("Enabled", &pl.enabled);
                                    ImGui::SameLine();
                                    if (ImGui::Button(" Remove")) removePl = i;

                                    ImGui::ColorEdit3("Color", &pl.color.x);
                                    ImGui::DragFloat3("Position", &pl.position.x, 0.1f);
                                    ImGui::SameLine();
                                    if (ImGui::Button(" To Cam")) pl.position = m_camera->GetPosition();

                                    ImGui::SliderFloat("Intensity", &pl.intensity, 0.0f, 25.0f, "%.1f");
                                    ImGui::SliderFloat("Range (m)", &pl.range, 0.5f, 40.0f, "%.1f");
                                    ImGui::TreePop();
                                }
                                ImGui::PopID();
                            }
                            if (removePl >= 0 && removePl < (int)plList.size()) {
                                plList.erase(plList.begin() + removePl);
                            }

                            ImGui::Separator();

                            ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "Spot Lights (%d / %d):", (int)slList.size(), (int)EngineRender::MAX_SPOT_LIGHTS);
                            ImGui::SameLine();
                            if (ImGui::Button(" Add Spot Light") && slList.size() < EngineRender::MAX_SPOT_LIGHTS) {
                                EngineRender::SpotLight sl;
                                sl.name = "SpotLight_" + std::to_string(slList.size() + 1);
                                sl.position = m_camera->GetPosition();
                                sl.direction = m_camera->GetForward();
                                sl.color = XMFLOAT3(1.0f, 1.0f, 1.0f);
                                sl.intensity = 5.0f;
                                sl.range = 20.0f;
                                sl.innerAngle = 20.0f;
                                sl.outerAngle = 35.0f;
                                sl.enabled = true;
                                slList.push_back(sl);
                            }

                            int removeSl = -1;
                            for (int i = 0; i < (int)slList.size(); ++i) {
                                auto& sl = slList[i];
                                ImGui::PushID(i + 4000);
                                char title[64];
                                snprintf(title, sizeof(title), " %s##%d", sl.name.c_str(), i);
                                if (ImGui::TreeNode(title)) {
                                    ImGui::Checkbox("Enabled", &sl.enabled);
                                    ImGui::SameLine();
                                    if (ImGui::Button(" Remove")) removeSl = i;

                                    ImGui::ColorEdit3("Color", &sl.color.x);
                                    ImGui::DragFloat3("Position", &sl.position.x, 0.1f);
                                    ImGui::SameLine();
                                    if (ImGui::Button(" To Cam")) sl.position = m_camera->GetPosition();

                                    ImGui::DragFloat3("Direction", &sl.direction.x, 0.02f, -1.0f, 1.0f);
                                    ImGui::SameLine();
                                    if (ImGui::Button(" Cam Dir")) sl.direction = m_camera->GetForward();

                                    ImGui::SliderFloat("Intensity", &sl.intensity, 0.0f, 35.0f, "%.1f");
                                    ImGui::SliderFloat("Range (m)", &sl.range, 1.0f, 60.0f, "%.1f");
                                    ImGui::SliderFloat("Inner Angle", &sl.innerAngle, 2.0f, 75.0f, "%.1f");
                                    ImGui::SliderFloat("Outer Angle", &sl.outerAngle, sl.innerAngle, 85.0f, "%.1f");
                                    ImGui::TreePop();
                                }
                                ImGui::PopID();
                            }
                            if (removeSl >= 0 && removeSl < (int)slList.size()) {
                                slList.erase(slList.begin() + removeSl);
                            }

                            ImGui::Separator();
                            if (ImGui::Button(" Cyberpunk Neon Set")) {
                                plList.clear();
                                slList.clear();
                                EngineRender::PointLight pl1;
                                pl1.name = "Neon Cyan Point";
                                pl1.position = XMFLOAT3(-4.0f, 2.5f, 2.0f);
                                pl1.color = XMFLOAT3(0.1f, 0.9f, 1.0f);
                                pl1.intensity = 4.0f;
                                pl1.range = 15.0f;
                                pl1.enabled = true;
                                plList.push_back(pl1);

                                EngineRender::PointLight pl2;
                                pl2.name = "Neon Magenta Point";
                                pl2.position = XMFLOAT3(4.0f, 2.5f, 2.0f);
                                pl2.color = XMFLOAT3(1.0f, 0.15f, 0.85f);
                                pl2.intensity = 4.0f;
                                pl2.range = 15.0f;
                                pl2.enabled = true;
                                plList.push_back(pl2);

                                EngineRender::SpotLight sl1;
                                sl1.name = "Arena Overhead Spot";
                                sl1.position = XMFLOAT3(0.0f, 9.0f, 0.0f);
                                sl1.direction = XMFLOAT3(0.0f, -1.0f, 0.0f);
                                sl1.color = XMFLOAT3(1.0f, 0.95f, 0.9f);
                                sl1.intensity = 6.0f;
                                sl1.range = 28.0f;
                                sl1.innerAngle = 25.0f;
                                sl1.outerAngle = 42.0f;
                                sl1.enabled = true;
                                slList.push_back(sl1);
                            }
                            ImGui::SameLine();
                            if (ImGui::Button(" Clear All Lights")) {
                                plList.clear();
                                slList.clear();
                            }
                        }
                        ImGui::End();
                    }

                    // -------------------------------------------------------
                    //  Scene Graph Editor Window
                    // -------------------------------------------------------
                    if (s_showSceneGraph) {
                        EngineScene::SceneGraph::Get().DrawUI(m_renderer.get(), m_camera.get());
                        // DrawUI manages its own ImGui::Begin/End windows
                    }

                    // -------------------------------------------------------
                    //  Lua Script System Editor Window
                    // -------------------------------------------------------
                    EngineScript::ScriptSystem::Get().DrawUI();

                    // -------------------------------------------------------
                    //  Navigation & AI Studio Window
                    // -------------------------------------------------------
                    EngineAI::NavigationSystem::Get().DrawUI(m_renderer.get(), m_camera->GetPosition(), m_camera->GetCollisionBoxes());

                    // -------------------------------------------------------
                    //  Particle / VFX Studio Window
                    // -------------------------------------------------------
                    EngineVFX::ParticleSystem::Get().DrawUI(m_camera->GetPosition(), m_camera->GetForwardVector());

                    // -------------------------------------------------------
                    //  Terrain & Foliage Studio Window
                    // -------------------------------------------------------
                    EngineTerrain::TerrainSystem::Get().DrawUI(m_camera->GetPosition());

                    // -------------------------------------------------------
                    //  Post-Processing & Color Grading Studio Window
                    // -------------------------------------------------------
                    EnginePostProcess::PostProcessSystem::Get().DrawUI(m_camera->GetPosition());

                    // -------------------------------------------------------
                    //  Prefab & Asset Registry Studio Window
                    // -------------------------------------------------------
                    EnginePrefab::PrefabSystem::Get().DrawUI(m_renderer.get(), m_camera->GetPosition(), m_camera->GetForwardVector());
                    EngineAsset::AssetRegistry::Get().DrawUI();

                    // -------------------------------------------------------
                    //  Engine Profiler & Telemetry Studio Window
                    // -------------------------------------------------------
                    EngineDebug::Profiler::Get().DrawUI();

                    // -------------------------------------------------------
                    //  In-Game Developer Console Window
                    // -------------------------------------------------------
                    EngineDebug::DebugConsole::Get().DrawUI();

                    // -------------------------------------------------------
                    //  Input Mapping & Controller Studio Window
                    // -------------------------------------------------------
                    EngineInput::InputMapping::Get().DrawUI(&dualSense);

                    if(s_showSpawner) {
                    ImGui::Begin("Spawner", &s_showSpawner, ImGuiWindowFlags_AlwaysAutoResize);
                    ImGui::Text("Spawn Primitive");
                    ImGui::Separator();
                    XMFLOAT3 spawnPos = m_camera->GetPosition();
                    spawnPos.y += 1.5f;
                    if (ImGui::Button("+ Vehicle")) { EnginePhysics::PhysicsSystem::Get().SpawnVehicle(m_renderer.get(),spawnPos);sceneDirty=true; }
                    if (ImGui::Button("+ Cube"))     { m_renderer->SpawnCube(spawnPos); sceneDirty = true; }
                    ImGui::SameLine();
                    if (ImGui::Button("+ Plane"))    { m_renderer->SpawnPlane(spawnPos); sceneDirty = true; }
                    ImGui::SameLine();
                    if (ImGui::Button("+ Sphere"))   { m_renderer->SpawnSphere(spawnPos); sceneDirty = true; }
                    ImGui::SameLine();
                    if (ImGui::Button("+ Triangle")) { m_renderer->SpawnTriangle(spawnPos); sceneDirty = true; }
                    
                    ImGui::Separator();
                    ImGui::Text("Import 3D Model (Auto-saves to src/Content)");
                    if (ImGui::Button(" Import 3D Model (FBX/OBJ/GLTF)...")) {
                        EngineUI::ContentBrowser::Get().ImportModelFileDialog(m_renderer.get(), m_camera.get());
                        sceneDirty = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(" Content Browser")) {
                        EngineUI::ContentBrowser::Get().SetOpen(true);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(" Level Editor")) {
                        EngineLevel::LevelSystem::Get().ToggleOpen();
                    }

                    static char modelPath[512] = "";
                    ImGui::InputText("Path", modelPath, sizeof(modelPath));
                    if (ImGui::Button("Load Custom Model from Path")) {
                        m_renderer->SpawnDynamicRenderModel(modelPath, spawnPos);
                        sceneDirty = true;
                    }

                    ImGui::End();

                    } // Spawner
                    if(s_showEnvironment) {
                    ImGui::Begin("Environment Controls", &s_showEnvironment, ImGuiWindowFlags_AlwaysAutoResize);
                    ImGui::Text("Sun Light Parameters");
                    ImGui::Separator();

                    XMFLOAT3 lightDir = m_renderer->GetLightDir();
                    XMFLOAT3 lightColor = m_renderer->GetLightColor();
                    XMFLOAT3 skyColor = m_renderer->GetSkyColor();

                    float azimuth = m_renderer->GetSunAzimuth();
                    float elevation = m_renderer->GetSunElevation();

                    float colorArray[3] = { lightColor.x, lightColor.y, lightColor.z };
                    if (ImGui::ColorEdit3("Color", colorArray)) {
                        lightColor = XMFLOAT3(colorArray[0], colorArray[1], colorArray[2]);
                        m_renderer->SetLightColor(lightColor);
                        sceneDirty = true;
                    }

                    
                    float skyArray[3] = { skyColor.x, skyColor.y, skyColor.z };
                    if (ImGui::ColorEdit3("Sky Color", skyArray)) {
                        m_renderer->SetSkyColor(XMFLOAT3(skyArray[0], skyArray[1], skyArray[2]));
                        sceneDirty = true;
                    }

                    ImGui::Separator();
                    ImGui::Text("Sun Position");

                    float sunAzimuth = m_renderer->GetSunAzimuth();
                    float sunElevation = m_renderer->GetSunElevation();

                    if (ImGui::SliderFloat("Azimuth", &sunAzimuth, 0.0f, 360.0f, "%.1f°")) {
                        m_renderer->SetSunAzimuth(sunAzimuth);
                        sceneDirty = true;
                    }
                    if (ImGui::SliderFloat("Elevation", &sunElevation, 0.0f, 90.0f, "%.1f°")) {
                        m_renderer->SetSunElevation(sunElevation);
                        sceneDirty = true;
                    }

                    ImGui::Separator();
                    ImGui::Text("Presets");
                    if (ImGui::Button("Morning")) {
                        m_renderer->SetLightDir(XMFLOAT3(-1.0f, -0.3f, 0.0f));
                        m_renderer->SetLightColor(XMFLOAT3(1.0f, 0.8f, 0.6f));
                        m_renderer->SetSkyColor(XMFLOAT3(0.4f, 0.6f, 0.9f));
                        m_renderer->SetSunAzimuth(45.0f);
                        m_renderer->SetSunElevation(20.0f);
                        sceneDirty = true;
                    }
                    if (ImGui::Button("Day")) {
                        m_renderer->SetLightDir(XMFLOAT3(-0.6f, -0.7f, -0.3f));
                        m_renderer->SetLightColor(XMFLOAT3(1.0f, 1.0f, 1.0f));
                        m_renderer->SetSkyColor(XMFLOAT3(0.2f, 0.4f, 0.8f));
                        m_renderer->SetSunAzimuth(135.0f);
                        m_renderer->SetSunElevation(60.0f);
                        sceneDirty = true;
                    }
                    if (ImGui::Button("Sunset")) {
                        m_renderer->SetLightDir(XMFLOAT3(1.0f, -0.15f, 0.0f));
                        m_renderer->SetLightColor(XMFLOAT3(1.0f, 0.4f, 0.1f));
                        m_renderer->SetSkyColor(XMFLOAT3(0.8f, 0.4f, 0.2f));
                        m_renderer->SetSunAzimuth(270.0f);
                        m_renderer->SetSunElevation(5.0f);
                        sceneDirty = true;
                    }
                    if (ImGui::Button("Night")) {
                        m_renderer->SetLightDir(XMFLOAT3(-0.35f, -0.75f, 0.45f));
                        m_renderer->SetLightColor(XMFLOAT3(0.35f, 0.45f, 0.70f));
                        m_renderer->SetSkyColor(XMFLOAT3(0.04f, 0.08f, 0.18f));
                        m_renderer->SetSunAzimuth(210.0f);
                        m_renderer->SetSunElevation(-25.0f);
                        sceneDirty = true;
                    }
                    
                    ImGui::Separator();
                    ImGui::Text("Cloud Parameters");
                    if (ImGui::SliderFloat("Cloud Density", &m_renderer->GetCloudDensity(), 0.0f, 2.0f)) {
                        sceneDirty = true;
                    }

                    ImGui::End();
                    

                    } // Environment
                    if(s_showGraphics) {
                    // ========== DEDICATED GRAPHICS SETTINGS WINDOW ==========
                    ImGui::SetNextWindowSize(ImVec2(500, 560), ImGuiCond_FirstUseEver);
                    if (ImGui::Begin("Graphics Settings", &s_showGraphics)) {
                        EngineUI::DrawAntiAliasingControls(*m_renderer);
                        ImGui::Separator();
                        if (ImGui::BeginTabBar("GraphicsTabBar")) {
                            
                            // TAB 1: DISPLAY & PERFORMANCE
                            if (ImGui::BeginTabItem("Display")) {
                                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Performance Metrics:");
                                ImGui::Text("FPS: %.1f  |  Frame Time: %.2f ms", m_fps, dt * 1000.0f);
                                ImGui::Text("Display: %d x %d  |  Render: %d x %d", 
                                    m_renderer->GetWidth(), m_renderer->GetHeight(),
                                    m_renderer->GetRenderWidth(), m_renderer->GetRenderHeight());
                                ImGui::Separator();

                                bool isFull = m_window->IsFullscreen();
                                if (ImGui::Checkbox("Fullscreen Mode (F11)", &isFull)) {
                                    m_window->ToggleFullscreen();
                                    m_window->ResetMouseDelta();
                                }

                                bool vsync = m_renderer->IsVSyncEnabled();
                                if (ImGui::Checkbox("V-Sync", &vsync)) {
                                    m_renderer->SetVSyncEnabled(vsync);
                                }

                                float fov = m_renderer->GetFOV();
                                if (ImGui::SliderFloat("FOV", &fov, 45.0f, 110.0f, "%.0f deg")) {
                                    m_renderer->SetFOV(fov);
                                }

                                ImGui::Separator();
                                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.6f, 1.0f), "Low Latency Technologies (Reflex / XeLL):");
                                if (m_renderer->IsReflexSupported()) {
                                    ReflexMode curReflex = m_renderer->GetReflexMode();
                                    int reflexIdx = static_cast<int>(curReflex);
                                    const char* reflexModes[] = { "Off", "On (Low Latency)", "On + Boost (Low Latency + Boost)" };
                                    if (ImGui::Combo("NVIDIA Reflex", &reflexIdx, reflexModes, 3)) {
                                        m_renderer->SetReflexMode(static_cast<ReflexMode>(reflexIdx));
                                    }
                                } else {
                                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "NVIDIA Reflex: Not supported or non-NVIDIA GPU");
                                }
                                if (m_renderer->IsXeLLSupported()) {
                                    bool xell = m_renderer->IsXeLLEnabled();
                                    if (ImGui::Checkbox("Intel XeLL (Xe Low Latency)", &xell)) {
                                        m_renderer->SetXeLLEnabled(xell);
                                    }
                                }

                                ImGui::EndTabItem();
                            }

                            // TAB 2: UPSCALING & FRAME GENERATION
                            if (ImGui::BeginTabItem("Upscaling & Frame Gen")) {
#if defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
                                ImGui::TextColored(ImVec4(0.2f, 0.6f, 1.0f, 1.0f), "[PlayStation 4: Native Full HD 1080p 60 FPS]");
                                if (m_renderer->GetPS4Backend()) {
                                    ImGui::Text("Render Backend: OpenOrbis libSceVideoOut Direct Memory Swapchain");
                                }
#elif defined(__APPLE__)
                                ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.4f, 1.0f), "[Apple Silicon MetalFX: Hardware Supported]");
                                if (m_renderer->GetMetalBackend()) {
                                    auto& fx = m_renderer->GetMetalBackend()->GetMetalFX();
                                    int fxMode = static_cast<int>(fx.GetMode());
                                    const char* fxNames[] = { "Disabled (Native Resolution)", "MetalFX Spatial Upscaling", "MetalFX Temporal Anti-Aliasing & Upscaling" };
                                    if (ImGui::Combo("MetalFX Technology", &fxMode, fxNames, 3)) {
                                        fx.SetMode(static_cast<EngineRender::MetalFXMode>(fxMode));
                                    }
                                    if (fxMode != 0) {
                                        int fxQual = static_cast<int>(fx.GetQuality());
                                        const char* fxQualNames[] = { "Performance", "Balanced", "Quality" };
                                        if (ImGui::Combo("MetalFX Quality", &fxQual, fxQualNames, 3)) {
                                            fx.SetQuality(static_cast<EngineRender::MetalFXQuality>(fxQual));
                                        }
                                        float sharpness = m_renderer->GetUpscaleSharpness();
                                        if (ImGui::SliderFloat("Sharpness", &sharpness, 0.0f, 1.0f, "%.2f")) {
                                            m_renderer->SetUpscaleSharpness(sharpness);
                                        }
                                    }
                                }
#else
                                bool dlssSupported = m_renderer->IsDLSSSupported();
                                bool dlssActive = m_renderer->IsDLSSActive();
                                bool dlssGSupported = m_renderer->IsDLSSGSupported();
                                if (dlssSupported) {
                                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.4f, 1.0f), "[NVIDIA DLSS: Hardware Supported]");
                                } else {
                                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "[NVIDIA DLSS: Not Supported on this GPU]");
                                }
                                if (m_renderer->IsXeSSSupported()) {
                                    ImGui::TextColored(ImVec4(0.0f, 0.7f, 1.0f, 1.0f), "[Intel XeSS 2.0: Hardware Supported]");
                                }

                                if (m_renderer->GetUpscaleTechnique() == UpscaleTechnique::DLSS) {
                                    if (dlssActive) {
                                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.4f, 1.0f), "Status: ACTIVE (AI Super Resolution 4.5)");
                                    } else {
                                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Status: Initializing...");
                                    }
                                } else if (m_renderer->GetUpscaleTechnique() == UpscaleTechnique::XeSS) {
                                    if (m_renderer->IsXeSSActive()) {
                                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.4f, 1.0f), "Status: ACTIVE (Intel XeSS 2.0 AI Super Sampling)");
                                    } else {
                                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Status: Initializing / Ready");
                                    }
                                }

                                ImGui::Separator();
                                UpscaleTechnique currentTech = m_renderer->GetUpscaleTechnique();
                                int techIndex = (currentTech == UpscaleTechnique::XeSS) ? 3 : static_cast<int>(currentTech);
                                if (techIndex < 0 || techIndex > 3) techIndex = 0;
                                const char* techNames[] = { "None (Off / Native)", "NVIDIA DLSS (Streamline)", "AMD FSR Temporal (SDK)", "Intel XeSS 2.0 (AI Super Sampling)" };
                                if (ImGui::Combo("Upscale Technology", &techIndex, techNames, 4)) {
                                    UpscaleTechnique newTech = (techIndex == 3) ? UpscaleTechnique::XeSS : static_cast<UpscaleTechnique>(techIndex);
                                    m_renderer->SetUpscaleTechnique(newTech);
                                }

                                if (techIndex != 0) {
                                    UpscaleQuality currentQuality = m_renderer->GetUpscaleQuality();
                                    int qualityIndex = static_cast<int>(currentQuality);
                                    const char* qualityNames[] = {
                                        techIndex == 1 ? "NVIDIA DLAA (1.0x)" : techIndex == 3 ? "XeSS Native AA (1.0x)" : "AMD FSR Native AA (1.0x)",
                                        "Quality (1.5x)",
                                        "Balanced (1.7x)",
                                        "Performance (2.0x)",
                                        "Ultra Performance (3.0x)"
                                    };
                                    if (ImGui::Combo("Quality Mode", &qualityIndex, qualityNames, 5)) {
                                        m_renderer->SetUpscaleQuality(static_cast<UpscaleQuality>(qualityIndex));
                                    }

                                    ImGui::TextColored(ImVec4(0.5f, 0.9f, 1.0f, 1.0f), "Quick Quality Mode:");
                                    ImGui::SameLine();
                                    if (ImGui::SmallButton("Native AA##uq")) m_renderer->SetUpscaleQuality(UpscaleQuality::Native);
                                    ImGui::SameLine();
                                    if (ImGui::SmallButton("Quality##uq")) m_renderer->SetUpscaleQuality(UpscaleQuality::Quality);
                                    ImGui::SameLine();
                                    if (ImGui::SmallButton("Balanced##uq")) m_renderer->SetUpscaleQuality(UpscaleQuality::Balanced);
                                    ImGui::SameLine();
                                    if (ImGui::SmallButton("Performance##uq")) m_renderer->SetUpscaleQuality(UpscaleQuality::Performance);
                                    ImGui::SameLine();
                                    if (ImGui::SmallButton("Ultra Perf##uq")) m_renderer->SetUpscaleQuality(UpscaleQuality::UltraPerformance);

                                    float sharpness = m_renderer->GetUpscaleSharpness();
                                    if (ImGui::SliderFloat("Upscaler sharpness", &sharpness, 0.0f, 1.0f, "%.2f")) {
                                        m_renderer->SetUpscaleSharpness(sharpness);
                                    }
                                }

                                ImGui::Separator();
                                ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "AI Frame Generation:");

                                ImGui::TextWrapped("FSR frame generation: SDK interpolation (2x).");
                                ImGui::BeginDisabled(!m_renderer->IsDLSSGSupported());
                                // NVIDIA DLSS-G Frame Generation & Multi-Frame Generation
                                if (currentTech == UpscaleTechnique::DLSS || dlssSupported) {
                                    bool dlssG = m_renderer->IsDLSSGRequested();
                                    if (ImGui::Checkbox("NVIDIA DLSS Frame Generation (DLSS-G)", &dlssG)) {
                                        m_renderer->SetDLSSGActive(dlssG);
                                    }
                                    if (dlssG) {
                                        const int multiplier=m_renderer->GetDLSSGMultiplier();
                                        const std::string preview=std::to_string(multiplier)+"x";
                                        if(ImGui::BeginCombo("DLSS Multi-Frame Multiplier",preview.c_str())){
                                            for(int value=2;value<=m_renderer->GetDLSSGMaxMultiplier();++value){
                                                const std::string label=std::to_string(value)+"x ("+std::to_string(value-1)+" generated)";
                                                if(ImGui::Selectable(label.c_str(),value==multiplier))m_renderer->SetDLSSGMultiplier(value);
                                            }
                                            ImGui::EndCombo();
                                        }
                                        ImGui::Text("NVIDIA interpolation: %s | SDK status: %d",m_renderer->IsDLSSGActive()?"active":"waiting",m_renderer->GetDLSSGStatus());
                                        ImGui::Text("Generated frames: %llu",static_cast<unsigned long long>(m_renderer->GetDLSSGGeneratedFrames()));
                                    }
                                }

                                ImGui::EndDisabled();
                                if(!m_renderer->IsDLSSGSupported())ImGui::TextDisabled("NVIDIA FG/MFG unavailable on this GPU/driver. FSR FG can be used separately.");
                                bool fsrFG=m_renderer->IsFSRFrameGenEnabled();
                                if(ImGui::Checkbox("AMD FSR Frame Generation (2x)",&fsrFG))m_renderer->SetFSRFrameGenEnabled(fsrFG);
                                ImGui::Text("FSR interpolation: %s",m_renderer->IsFSRFrameGenActive()?"active":"idle");
                                ImGui::TextWrapped("Frame generation modes are mutually exclusive. Generated frames do not increase simulation FPS.");
                                // Intel XeSS Frame Generation (XeFG)
                                if (currentTech == UpscaleTechnique::XeSS || m_renderer->IsXeSSSupported()) {
                                    bool xessFG = m_renderer->IsXeSSFrameGenEnabled();
                                    if (ImGui::Checkbox("Intel XeSS Frame Generation (XeFG)", &xessFG)) {
                                        m_renderer->SetXeSSFrameGenEnabled(xessFG);
                                    }
                                }
#endif

                                ImGui::EndTabItem();
                            }

                            // TAB 3: RASTERIZATION
                            if (ImGui::BeginTabItem("Rasterization")) {
                                ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Rasterized Lighting & Effects:");

                                // Screen Space Ambient Occlusion
                                bool ssao = m_renderer->IsSSAOEnabled();
                                if (ImGui::Checkbox("Screen Space Ambient Occlusion (SSAO)", &ssao)) {
                                    m_renderer->SetSSAOEnabled(ssao);
                                }
                                if (ssao) {
                                    float rad = m_renderer->GetSSAORadius();
                                    if (ImGui::SliderFloat("SSAO Radius", &rad, 0.05f, 2.0f, "%.2f")) {
                                        m_renderer->SetSSAORadius(rad);
                                    }
                                    float bias = m_renderer->GetSSAOBias();
                                    if (ImGui::SliderFloat("SSAO Bias", &bias, 0.001f, 0.1f, "%.3f")) {
                                        m_renderer->SetSSAOBias(bias);
                                    }
                                    float inten = m_renderer->GetSSAOIntensity();
                                    if (ImGui::SliderFloat("SSAO Intensity", &inten, 0.1f, 5.0f, "%.2f")) {
                                        m_renderer->SetSSAOIntensity(inten);
                                    }
                                }

                                ImGui::Separator();
                                // Screen Space Global Illumination
                                bool ssgi = m_renderer->IsSSGIEnabled();
                                if (ImGui::Checkbox("Global illumination (SSGI / DXR bounce)", &ssgi)) {
                                    m_renderer->SetSSGIEnabled(ssgi);
                                }
                                if (ssgi) {
                                    float ssgiInt = m_renderer->GetSSGIIntensity();
                                    if (ImGui::SliderFloat("Indirect light intensity", &ssgiInt, 0.0f, 3.0f, "%.2f")) {
                                        m_renderer->SetSSGIIntensity(ssgiInt);
                                    }
                                }

                                ImGui::Separator();
                                // Directional Shadow Map
                                bool vsm = m_renderer->IsVSMEnabled();
                                if (ImGui::Checkbox("Directional Shadow Map (2048)", &vsm)) {
                                    m_renderer->SetVSMEnabled(vsm);
                                }

                                ImGui::Separator();
                                // Volumetric Fog
                                bool fog = m_renderer->IsVolumetricFogEnabled();
                                if (ImGui::Checkbox("Volumetric Fog", &fog)) {
                                    m_renderer->SetVolumetricFogEnabled(fog);
                                }
                                if (fog) {
                                    float fogDens = m_renderer->GetVolumetricFogDensity();
                                    if (ImGui::SliderFloat("Fog Density", &fogDens, 0.001f, 0.05f, "%.4f")) {
                                        m_renderer->SetVolumetricFogDensity(fogDens);
                                    }
                                }

                                ImGui::Separator();
                                // Volumetric Clouds & Cloud Shadows
                                float clouds = m_renderer->GetCloudDensity();
                                if (ImGui::SliderFloat("Volumetric Clouds", &clouds, 0.0f, 3.0f, "%.2f")) {
                                    m_renderer->SetCloudDensity(clouds);
                                }
                                ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "Cloud Shadows: Full (Rasterization & RTX Active)");

                                ImGui::Separator();
                                // Atmosphere & Sun
                                float sunAz = m_renderer->GetSunAzimuth();
                                if (ImGui::SliderFloat("Sun Azimuth", &sunAz, 0.0f, 360.0f, "%.0f deg")) {
                                    m_renderer->SetSunAzimuth(sunAz);
                                }
                                float sunEl = m_renderer->GetSunElevation();
                                if (ImGui::SliderFloat("Sun Elevation", &sunEl, -10.0f, 90.0f, "%.0f deg")) {
                                    m_renderer->SetSunElevation(sunEl);
                                }

                                ImGui::EndTabItem();
                            }

                            // TAB 4: RAY TRACING (RTX / METAL RAY TRACING)
                            if (ImGui::BeginTabItem("Ray Tracing (RTX / Metal)")) {
#if defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
                                ImGui::TextColored(ImVec4(0.2f, 0.6f, 1.0f, 1.0f), "[PlayStation 4 GCN: High-Performance PBR Triangle Rasterizer Active]");
#elif defined(__APPLE__)
                                bool rtAvailable = false;
                                if (m_renderer->GetMetalBackend()) {
                                    rtAvailable = m_renderer->GetMetalBackend()->GetCapabilities().SupportsRayTracing;
                                }
                                if (rtAvailable) {
                                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.4f, 1.0f), "[Apple Silicon Metal Ray Tracing: Hardware Accelerated]");
                                } else {
                                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[Apple Silicon Metal Ray Tracing: Simulation Mode]");
                                }

                                if (ImGui::Checkbox("Metal Ray Tracing Master Switch", &m_renderer->m_dxrEnabled)) {
                                    m_renderer->InvalidateRaytracingScene();
                                }

                                if (m_renderer->m_dxrEnabled) {
                                    ImGui::Separator();
                                    ImGui::Checkbox("Ray Traced Reflections", &m_renderer->m_rtReflectionsEnabled);
                                    ImGui::Checkbox("Ray Traced Shadows", &m_renderer->m_rtShadowsEnabled);
                                    if (ImGui::Button("Rebuild Metal TLAS / Acceleration Structures")) {
                                        m_renderer->InvalidateRaytracingScene();
                                    }
                                } else {
                                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Ray Tracing is OFF - Native Metal Rasterizer Active.");
                                }
#else
                                if (m_renderer->m_dxrDevice) {
                                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.4f, 1.0f), "[DirectX Raytracing DXR 1.1: Active]");
                                } else {
                                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "[DXR Hardware: Not Available]");
                                }

                                if (ImGui::Checkbox("RTX Master Switch", &m_renderer->m_dxrEnabled)) {
                                    m_renderer->InvalidateRaytracingScene();
                                }

                                if (m_renderer->m_dxrEnabled && m_renderer->m_dxrDevice) {
                                    ImGui::Separator();
                                    
                                    bool isPT = m_renderer->IsPathTracingEnabled();
                                    const char* rtModes[] = { "Hybrid RTX (Real-Time Reflections + Shadows)", "Full Path Tracing (Global Illumination)" };
                                    int rtModeIdx = isPT ? 1 : 0;
                                    if (ImGui::Combo("RT Engine Mode", &rtModeIdx, rtModes, 2)) {
                                        m_renderer->SetPathTracingEnabled(rtModeIdx == 1);
                                    }

                                    if (isPT) {
                                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Full Path Tracing Mode (Multi-Bounce GI):");
                                        int bounces = m_renderer->GetPathTracingBounces();
                                        if (ImGui::SliderInt("Max Bounces", &bounces, 1, 4)) {
                                            m_renderer->SetPathTracingBounces(bounces);
                                        }
                                    } else {
                                        ImGui::Checkbox("RTX Reflections", &m_renderer->m_rtReflectionsEnabled);
                                        ImGui::Checkbox("RTX Shadows", &m_renderer->m_rtShadowsEnabled);
                                    }

                                    ImGui::Spacing();
                                    if (ImGui::Button("Rebuild Raytracing TLAS / BVH")) {
                                        m_renderer->InvalidateRaytracingScene();
                                    }
                                } else {
                                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "RTX is currently OFF - Full Rasterization Engine Active.");
                                }
#endif
                                ImGui::EndTabItem();
                            }

                            // TAB 5: TEXTURES & ANISOTROPIC FILTERING
                            if (ImGui::BeginTabItem("Textures & Filtering")) {
                                int currentAniso = m_renderer->GetAnisotropy();
                                const char* anisoLevels[] = { "Off (1x / Bilinear)", "2x", "4x", "8x", "16x (Ultra - Recommended)" };
                                int anisoIdx = 4;
                                if (currentAniso <= 1) anisoIdx = 0;
                                else if (currentAniso <= 2) anisoIdx = 1;
                                else if (currentAniso <= 4) anisoIdx = 2;
                                else if (currentAniso <= 8) anisoIdx = 3;
                                else anisoIdx = 4;

                                if (ImGui::Combo("Anisotropic Filtering", &anisoIdx, anisoLevels, 5)) {
                                    int newAniso = 16;
                                    if (anisoIdx == 0) newAniso = 1;
                                    else if (anisoIdx == 1) newAniso = 2;
                                    else if (anisoIdx == 2) newAniso = 4;
                                    else if (anisoIdx == 3) newAniso = 8;
                                    else if (anisoIdx == 4) newAniso = 16;
                                    m_renderer->SetAnisotropy(newAniso);
                                }

                                ImGui::Separator();
                                ImGui::Text("Texture Streaming:");
                                if (m_renderer->GetTextureStreamer()) {
                                    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Async Texture Streaming: Active (DirectX 12 Copy Queue)");
                                }
#if defined(_WIN32)
                                ImGui::Separator();
                                ImGui::Text("DirectStorage:");
                                if (m_renderer->IsDirectStorageSupported() && m_renderer->GetDirectStorage()) {
                                    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "DirectStorage: %s", m_renderer->GetDirectStorage()->GetStatusSummary().c_str());
                                } else {
                                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "DirectStorage: Disabled or Unsupported");
                                }
#endif
                                ImGui::EndTabItem();
                            }

                            // TAB 6: LEVEL OF DETAIL (LOD) & RUNTIME MAP LOADING
                            if (ImGui::BeginTabItem("LOD & Meshlets")) {
                                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "Level of Detail (LOD) System: Active");
                                
                                ImGui::Separator();
                                ImGui::Text("Active Frame LOD Metrics:");
                                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "  LOD 0 (Full Detail, 100%%): %u models", m_renderer->GetLOD0Count());
                                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.2f, 1.0f), "  LOD 1 (Medium Detail, ~50%%): %u models", m_renderer->GetLOD1Count());
                                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "  LOD 2 (Coarse Detail, ~25%%): %u models", m_renderer->GetLOD2Count());
                                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "  Distance Culled: %u models", m_renderer->GetLODCulledCount());

                                ImGui::Separator();
                                ImGui::Text("LOD Configuration:");

                                float lodMul = m_renderer->GetLODDistanceMultiplier();
                                if (ImGui::SliderFloat("LOD Distance Scale", &lodMul, 0.25f, 3.0f, "%.2fx")) {
                                    m_renderer->SetLODDistanceMultiplier(lodMul);
                                }

                                float lodBias = m_renderer->GetLODBias();
                                if (ImGui::SliderFloat("LOD Bias", &lodBias, -5.0f, 5.0f, "%.1f m")) {
                                    m_renderer->SetLODBias(lodBias);
                                }

                                const char* forcedLODNames[] = { "Auto (Distance-based)", "Force LOD 0 (High)", "Force LOD 1 (Medium)", "Force LOD 2 (Low)" };
                                int forcedIdx = m_renderer->GetForcedLOD() + 1; // -1 -> 0
                                if (ImGui::Combo("Force LOD Override", &forcedIdx, forcedLODNames, 4)) {
                                    m_renderer->SetForcedLOD(forcedIdx - 1);
                                }

                                bool debugColor = m_renderer->IsDebugLODColor();
                                if (ImGui::Checkbox("Debug LOD Color Tinting (Green=LOD0, Yellow=LOD1, Red=LOD2)", &debugColor)) {
                                    m_renderer->SetDebugLODColor(debugColor);
                                }

                                bool culling = m_renderer->IsLODCullingEnabled();
                                if (ImGui::Checkbox("Enable Distance Culling", &culling)) {
                                    m_renderer->SetLODCullingEnabled(culling);
                                }

                                if (culling) {
                                    float cullDist = m_renderer->GetLODCullDistance();
                                    if (ImGui::SliderFloat("Cull Distance", &cullDist, 50.0f, 500.0f, "%.0f m")) {
                                        m_renderer->SetLODCullDistance(cullDist);
                                    }
                                }

                                ImGui::Separator();
                                ImGui::Text("Runtime Map Loading:");
                                auto availLevels = EngineLevel::LevelSystem::Get().GetAvailableLevels();
                                static int s_selectedMapIdx = 0;
                                if (!availLevels.empty()) {
                                    std::vector<const char*> items;
                                    for (size_t i = 0; i < availLevels.size(); ++i) {
                                        items.push_back(availLevels[i].c_str());
                                        if (availLevels[i] == EngineLevel::LevelSystem::Get().GetCurrentLevelName()) {
                                            s_selectedMapIdx = (int)i;
                                        }
                                    }
                                    if (ImGui::Combo("Active Map", &s_selectedMapIdx, items.data(), (int)items.size())) {
                                        EngineLevel::LevelSystem::Get().RequestLoadLevel(availLevels[s_selectedMapIdx]);
                                    }
                                    ImGui::SameLine();
                                    if (ImGui::Button("Cycle Map (F7)")) {
                                        EngineLevel::LevelSystem::Get().CycleToNextLevel(m_renderer.get(), m_camera.get());
                                    }
                                }

                                ImGui::EndTabItem();
                            }

                            ImGui::EndTabBar();
                        }
                    }
                    ImGui::End();
                    } // Graphics
                    if(s_showSequencer) {
                    // ========== SEQUENCER WINDOW ==========
                    ImGui::SetNextWindowSize(ImVec2(600, 300), ImGuiCond_FirstUseEver);
                    ImGui::Begin("Sequencer");
                    {
                        // Playback Controls
                        if (m_sequencer->IsPlaying()) {
                            if (ImGui::Button("Pause")) m_sequencer->Pause();
                        } else {
                            if (ImGui::Button("Play")) m_sequencer->Play();
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Stop")) { m_sequencer->Stop(); }
                        ImGui::SameLine();

                        float seqTime = m_sequencer->GetTime();
                        float seqDur = m_sequencer->GetDuration();
                        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 120);
                        if (ImGui::SliderFloat("##Timeline", &seqTime, 0.0f, seqDur, "%.2f s")) {
                            m_sequencer->SetTime(seqTime);
                        }
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(80);
                        if (ImGui::DragFloat("Dur", &seqDur, 0.5f, 1.0f, 300.0f, "%.1f")) {
                            m_sequencer->SetDuration(seqDur);
                        }

                        ImGui::Separator();

                        // Add Keyframe section
                        static int selTrackModel = 0;
                        auto& allModels = m_renderer->GetModels();
                        auto& allDynModels = m_renderer->GetDynamicModels();
                        std::vector<std::string> modelNames;
                        for (auto& m : allModels) modelNames.push_back(m.name);
                        for (auto& m : allDynModels) modelNames.push_back(m.name);

                        if (!modelNames.empty()) {
                            ImGui::Text("Add Keyframe:");
                            ImGui::SetNextItemWidth(200);
                            if (ImGui::BeginCombo("##ModelSelect", selTrackModel < (int)modelNames.size() ? modelNames[selTrackModel].c_str() : "--")) {
                                for (int n = 0; n < (int)modelNames.size(); ++n) {
                                    if (ImGui::Selectable(modelNames[n].c_str(), selTrackModel == n))
                                        selTrackModel = n;
                                }
                                ImGui::EndCombo();
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Capture")) {
                                if (selTrackModel < (int)modelNames.size()) {
                                    const std::string& name = modelNames[selTrackModel];
                                    XMFLOAT3 p, r, s;
                                    bool found = false;
                                    for (auto& m : allModels) {
                                        if (m.name == name) { p = m.position; r = m.rotation; s = m.scale; found = true; break; }
                                    }
                                    if (!found) {
                                        for (auto& m : allDynModels) {
                                            if (m.name == name) { p = m.position; r = m.rotation; s = m.scale; found = true; break; }
                                        }
                                    }
                                    if (found) {
                                        m_sequencer->CaptureKeyframe(name, m_sequencer->GetTime(), p, r, s);
                                    }
                                }
                            }
                        }

                        ImGui::Separator();

                        // Track list with timeline visualization
                        auto& tracks = m_sequencer->GetTracks();
                        for (int ti = 0; ti < (int)tracks.size(); ++ti) {
                            auto& track = tracks[ti];
                            ImGui::PushID(ti);
                            bool trackOpen = ImGui::TreeNode(track.modelName.c_str());
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Delete##Track")) {
                                m_sequencer->RemoveTrack(ti);
                                if (trackOpen) ImGui::TreePop();
                                ImGui::PopID();
                                ti--;
                                continue;
                            }

                            if (trackOpen) {
                                // Draw mini timeline
                                float dur = m_sequencer->GetDuration();
                                ImVec2 tl_pos = ImGui::GetCursorScreenPos();
                                float tl_width = ImGui::GetContentRegionAvail().x;
                                float tl_height = 24.0f;
                                ImDrawList* dl = ImGui::GetWindowDrawList();
                                dl->AddRectFilled(tl_pos, ImVec2(tl_pos.x + tl_width, tl_pos.y + tl_height), IM_COL32(40, 40, 50, 255));

                                // Playhead
                                float px = tl_pos.x + (seqTime / dur) * tl_width;
                                dl->AddLine(ImVec2(px, tl_pos.y), ImVec2(px, tl_pos.y + tl_height), IM_COL32(255, 80, 80, 255), 2.0f);

                                // Keyframe diamonds
                                for (int ki = 0; ki < (int)track.keyframes.size(); ++ki) {
                                    float kx = tl_pos.x + (track.keyframes[ki].time / dur) * tl_width;
                                    float ky = tl_pos.y + tl_height * 0.5f;
                                    dl->AddQuadFilled(
                                        ImVec2(kx, ky - 5), ImVec2(kx + 5, ky),
                                        ImVec2(kx, ky + 5), ImVec2(kx - 5, ky),
                                        IM_COL32(100, 200, 255, 255));
                                }
                                ImGui::Dummy(ImVec2(tl_width, tl_height));

                                // Keyframe details
                                for (int ki = 0; ki < (int)track.keyframes.size(); ++ki) {
                                    auto& kf = track.keyframes[ki];
                                    ImGui::PushID(ki);
                                    ImGui::Text("t=%.2f  pos(%.1f,%.1f,%.1f)  rot(%.0f,%.0f,%.0f)  scl(%.2f,%.2f,%.2f)",
                                        kf.time,
                                        kf.position.x, kf.position.y, kf.position.z,
                                        kf.rotation.x, kf.rotation.y, kf.rotation.z,
                                        kf.scale.x, kf.scale.y, kf.scale.z);
                                    ImGui::SameLine();
                                    if (ImGui::SmallButton("X")) {
                                        track.RemoveKeyframe(ki);
                                        ki--;
                                    }
                                    ImGui::PopID();
                                }
                                ImGui::TreePop();
                            }
                            ImGui::PopID();
                        }

                        ImGui::Separator();
                        if (ImGui::Button("Save Sequence")) m_sequencer->Save("sequence.txt");
                        ImGui::SameLine();
                        if (ImGui::Button("Load Sequence")) m_sequencer->Load("sequence.txt");
                    }
                    ImGui::End();

                    } // Sequencer
                    // Weather / Rain Inspector Window
                    if(showRainWindow) {
                    if (ImGui::Begin("Weather / Rain", &showRainWindow, ImGuiWindowFlags_AlwaysAutoResize)) {
                        auto& rainSys = EngineRain::RainSystem::Get();
                        auto& settings = rainSys.GetSettings();
                        auto& stats = rainSys.GetDebugStats();

                        // 1. Main Controls
                        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Rain");
                        ImGui::Checkbox("Enable Rain", &settings.enabled);

                        ImGui::SliderFloat("Rain Intensity", &settings.intensity, 0.0f, 5.0f, "%.2f");
                        
                        settings.useCustomParticleCount = true;
                        ImGui::SliderInt("Rain Particles", &settings.particleCount, 1000, 100000);
                        ImGui::SliderFloat("Rain Area", &settings.areaRadius, 10.0f, 200.0f, "%.1f m");
                        ImGui::SliderFloat("Drop Speed", &settings.fallSpeed, 1.0f, 100.0f, "%.1f");

                        // 2. Wind Section
                        ImGui::Spacing();
                        ImGui::SeparatorText("Wind");
                        ImGui::Checkbox("Enable Wind", &settings.wind.enabled);
                        if (settings.wind.enabled) {
                            ImGui::SliderFloat("Wind Strength", &settings.wind.speed, 0.0f, 50.0f, "%.1f");
                            float wDir[3] = { settings.wind.direction.x, settings.wind.direction.y, settings.wind.direction.z };
                            if (ImGui::DragFloat3("Wind Direction", wDir, 0.02f, -1.0f, 1.0f, "%.2f")) {
                                settings.wind.direction = XMFLOAT3(wDir[0], wDir[1], wDir[2]);
                            }
                        }

                        // 3. Surface Interaction Section
                        ImGui::Spacing();
                        ImGui::SeparatorText("Surface");
                        ImGui::Checkbox("Surface Interaction", &settings.surface.enabled);
                        if (settings.surface.enabled) {
                            ImGui::Checkbox("Enable Wetness", &settings.surface.wetnessEnabled);
                            if (settings.surface.wetnessEnabled) {
                                ImGui::SliderFloat("Wetness Strength", &settings.surface.wetnessStrength, 0.0f, 1.0f, "%.2f");
                                ImGui::ProgressBar(stats.currentWetness, ImVec2(-1, 0), "Wetness Level");
                            }

                            ImGui::Checkbox("Enable Puddles", &settings.surface.puddlesEnabled);
                            if (settings.surface.puddlesEnabled) {
                                ImGui::SliderFloat("Puddle Accumulation", &settings.surface.puddleAccumulation, 0.0f, 1.0f, "%.2f");
                                ImGui::ProgressBar(stats.currentPuddle, ImVec2(-1, 0), "Puddle Level");
                            }

                            ImGui::Checkbox("Enable Splash", &settings.surface.splashesEnabled);
                            if (settings.surface.splashesEnabled) {
                                ImGui::SliderFloat("Splash Intensity", &settings.surface.splashIntensity, 0.0f, 2.0f, "%.2f");
                            }

                            ImGui::Checkbox("Enable Water Trails", &settings.surface.surfaceFlowEnabled);
                            if (settings.surface.surfaceFlowEnabled) {
                                ImGui::SliderFloat("Trail Strength", &settings.surface.trailStrength, 0.0f, 2.0f, "%.2f");
                            }
                        }

                        // 4. Camera Drops Section
                        ImGui::Spacing();
                        ImGui::SeparatorText("Camera");
                        ImGui::Checkbox("Camera Rain Drops", &settings.cameraDrops.enabled);
                        if (settings.cameraDrops.enabled) {
                            ImGui::SliderFloat("Camera Drop Intensity", &settings.cameraDrops.intensity, 0.0f, 1.0f, "%.2f");
                        }

                        // 5. Quality Section
                        ImGui::Spacing();
                        ImGui::SeparatorText("Quality");
                        const char* qualityNames[] = { "Low", "Medium", "High", "Ultra" };
                        int currentQ = static_cast<int>(settings.quality);
                        if (ImGui::Combo("Rain Quality", &currentQ, qualityNames, IM_ARRAYSIZE(qualityNames))) {
                            settings.quality = static_cast<EngineRain::RainQuality>(currentQ);
                            settings.useCustomParticleCount = false;
                        }

                        // 6. Preset System Section
                        ImGui::Spacing();
                        ImGui::SeparatorText("Presets");
                        if (ImGui::Button("Clear", ImVec2(70, 26))) {
                            rainSys.SetWeatherPreset(EngineRain::WeatherPreset::Clear);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Light Rain", ImVec2(85, 26))) {
                            rainSys.SetWeatherPreset(EngineRain::WeatherPreset::LightRain);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Rain", ImVec2(65, 26))) {
                            rainSys.SetWeatherPreset(EngineRain::WeatherPreset::Rain);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Heavy Rain", ImVec2(85, 26))) {
                            rainSys.SetWeatherPreset(EngineRain::WeatherPreset::HeavyRain);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Storm", ImVec2(65, 26))) {
                            rainSys.SetWeatherPreset(EngineRain::WeatherPreset::Storm);
                        }

                        // 7. Debug Visualization Section
                        ImGui::Spacing();
                        ImGui::SeparatorText("Debug Visualization");
                        ImGui::Checkbox("Show Particles", &settings.debugVis.showParticles);
                        ImGui::SameLine();
                        ImGui::Checkbox("Show Collision", &settings.debugVis.showCollision);
                        ImGui::Checkbox("Show Normals", &settings.debugVis.showNormals);
                        ImGui::SameLine();
                        ImGui::Checkbox("Show Flow", &settings.debugVis.showFlow);
                        ImGui::Checkbox("Show Wetness", &settings.debugVis.showWetness);
                        ImGui::SameLine();
                        ImGui::Checkbox("Show Puddles", &settings.debugVis.showPuddles);

                        ImGui::Separator();
                        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Active Particles: %d", stats.activeParticles);
                        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Active Splashes: %d", stats.activeSplashes);
                        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.4f, 1.0f), "Collision Hits/sec: %d", stats.collisionHitsPerSec);
                        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Sim Time: %.3f ms | Render: %.3f ms", stats.simulationTimeMs, stats.renderTimeMs);
                        ImGui::Text("Wind: (%.1f, %.1f, %.1f) m/s", stats.currentWind.x, stats.currentWind.y, stats.currentWind.z);
                        if (stats.isLightningActive) {
                            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "Lightning: ACTIVE");
                        }
                    }
                    ImGui::End();

                    // ==========================================
                    // DualSense (PS5) Control Center UI
                    // ==========================================
                    } // Weather
                    if(showDualSenseWindow) {
                    static bool s_weatherHaptics = true;
                    static float s_leftRumbleSlider = 0.0f;
                    static float s_rightRumbleSlider = 0.0f;
                    static float s_customLightColor[3] = { 0.0f, 0.47f, 1.0f };
                    static char s_profileSaveName[64] = "MyCustomProfile";
                    static int s_selectedSavedProfileIdx = 0;

                    // Dynamic Lightbar Updates for Weather sync
                    if (dualSense.IsConnected()) {
                        if (dualSense.GetLightbarMode() == 5) { // Weather / Sky Sync
                            auto& rainSys = EngineRain::RainSystem::Get();
                            const auto& weather = rainSys.GetWeatherSystem();
                            XMFLOAT3 col = weather.GetLightColor();
                            dualSense.SetLightbarColor(col.x, col.y, col.z);
                        }

                        // Ambient Weather / Rain Haptic feedback
                        static bool s_prevRainVibrating = false;
                        if (s_weatherHaptics) {
                            auto& rainSys = EngineRain::RainSystem::Get();
                            auto& rSettings = rainSys.GetSettings();
                            auto& rStats = rainSys.GetDebugStats();
                            if (rSettings.enabled && rSettings.intensity > 0.05f) {
                                s_prevRainVibrating = true;
                                if (rStats.isLightningActive) {
                                    dualSense.PlayRumble(0.7f, 0.4f, 0.2f);
                                } else {
                                    // Gentle raindrop vibration feel on right high-frequency motor
                                    float rainRumble = (std::min)(0.08f, rSettings.intensity * 0.015f);
                                    dualSense.SetRumble(0.0f, rainRumble);
                                }
                            } else if (s_prevRainVibrating) {
                                s_prevRainVibrating = false;
                                dualSense.SetRumble(0.0f, 0.0f);
                            }
                        } else if (s_prevRainVibrating) {
                            s_prevRainVibrating = false;
                            dualSense.SetRumble(0.0f, 0.0f);
                        }
                    }

                    if (ImGui::Begin("DualSense Control Center (PS5)", &showDualSenseWindow, ImGuiWindowFlags_AlwaysAutoResize)) {
                        bool connected = dualSense.IsConnected();
                        auto connType = dualSense.GetConnectionType();

                        // Header Status Indicator
                        if (connected) {
                            if (connType == EngineInput::DualSenseConnection::USB) {
                                ImGui::TextColored(ImVec4(0.2f, 0.9f, 1.0f, 1.0f), "[PHYSICAL USB CONNECTED (Quadraphonic Audio)]");
                            } else if (connType == EngineInput::DualSenseConnection::Bluetooth) {
                                ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "[BLUETOOTH CONNECTED (Native Mode)]");
                            } else {
                                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.2f, 1.0f), "[BLOCKED BY DSX / HIDHIDE -> XINPUT]");
                                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "  -> Close DSX or disable HidHide cloaking to unlock native Triggers, Lightbar & Haptics!");
                            }
                            ImGui::SameLine();
                            std::string modelStr(dualSense.GetProductString().begin(), dualSense.GetProductString().end());
                            ImGui::Text("(%s)", modelStr.empty() ? "DualSense Wireless Controller" : modelStr.c_str());

                            // Battery Bar
                            const auto& bat = dualSense.GetBattery();
                            ImGui::SameLine(500.0f);
                            char batText[64];
                            snprintf(batText, sizeof(batText), "Battery: %d%%%s", bat.level, bat.isCharging ? " [Charging]" : (bat.isFull ? " [Full]" : ""));
                            ImVec4 batCol = (bat.level > 50) ? ImVec4(0.2f, 0.85f, 0.3f, 1.0f) : ((bat.level > 20) ? ImVec4(0.95f, 0.8f, 0.1f, 1.0f) : ImVec4(0.95f, 0.2f, 0.2f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, batCol);
                            ImGui::ProgressBar(bat.level / 100.0f, ImVec2(180, 0), batText);
                            ImGui::PopStyleColor();

                            // Profiles Bar
                            ImGui::Spacing();
                            ImGui::SeparatorText("Presets & Profiles");
                            ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "Built-in Presets:");
                            ImGui::SameLine();
                            if (ImGui::Button("Default")) {
                                dualSense.ApplyProfile(dualSense.GetBuiltInProfile(EngineInput::DualSenseProfilePreset::Default));
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Shooter / FPS")) {
                                dualSense.ApplyProfile(dualSense.GetBuiltInProfile(EngineInput::DualSenseProfilePreset::Shooter));
                                EngineAudio::DualSenseAudioSystem::Get().PlayReloadSound(0.9f);
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Racing / Sim")) {
                                dualSense.ApplyProfile(dualSense.GetBuiltInProfile(EngineInput::DualSenseProfilePreset::Racing));
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Bow / Adventure")) {
                                dualSense.ApplyProfile(dualSense.GetBuiltInProfile(EngineInput::DualSenseProfilePreset::Bow));
                                EngineAudio::DualSenseAudioSystem::Get().PlayReloadSound(0.9f);
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Shotgun")) {
                                dualSense.ApplyProfile(dualSense.GetBuiltInProfile(EngineInput::DualSenseProfilePreset::Shotgun));
                                EngineAudio::DualSenseAudioSystem::Get().PlayReloadSound(0.9f);
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Off-Road")) {
                                dualSense.ApplyProfile(dualSense.GetBuiltInProfile(EngineInput::DualSenseProfilePreset::OffRoad));
                            }

                            // Custom Profiles Save / Load row
                            ImGui::SetNextItemWidth(160);
                            ImGui::InputText("##ProfileSaveName", s_profileSaveName, sizeof(s_profileSaveName));
                            ImGui::SameLine();
                            if (ImGui::Button("Save Profile")) {
                                dualSense.SaveProfile(s_profileSaveName);
                            }
                            ImGui::SameLine();

                            auto savedProfiles = dualSense.GetAvailableProfiles();
                            if (!savedProfiles.empty()) {
                                if (s_selectedSavedProfileIdx >= (int)savedProfiles.size()) s_selectedSavedProfileIdx = 0;
                                ImGui::SetNextItemWidth(160);
                                if (ImGui::BeginCombo("##SavedProfilesCombo", savedProfiles[s_selectedSavedProfileIdx].c_str())) {
                                    for (int i = 0; i < (int)savedProfiles.size(); ++i) {
                                        bool isSelected = (s_selectedSavedProfileIdx == i);
                                        if (ImGui::Selectable(savedProfiles[i].c_str(), isSelected)) {
                                            s_selectedSavedProfileIdx = i;
                                        }
                                        if (isSelected) ImGui::SetItemDefaultFocus();
                                    }
                                    ImGui::EndCombo();
                                }
                                ImGui::SameLine();
                                if (ImGui::Button("Load Profile")) {
                                    dualSense.LoadProfile(savedProfiles[s_selectedSavedProfileIdx]);
                                }
                                ImGui::SameLine();
                            }
                            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.5f, 1.0f), "Active: [%s]", dualSense.GetCurrentProfile().name.c_str());

                        } else {
                            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "[DISCONNECTED]");
                            ImGui::SameLine();
                            ImGui::TextDisabled("Plug in via USB-C or pair via Bluetooth (press PS + Share button)...");
                        }

                        ImGui::Separator();

                        if (ImGui::BeginTabBar("DualSenseTabs")) {
                            // ----------------------------------------------------
                            // Tab 1: Live Gamepad & Sensors Visualizer
                            // ----------------------------------------------------
                            if (ImGui::BeginTabItem("Telemetry & Visualizer")) {
                                ImDrawList* drawList = ImGui::GetWindowDrawList();
                                ImVec2 p = ImGui::GetCursorScreenPos();

                                ImGui::Columns(2, "SticksCol", false);
                                ImGui::SetColumnWidth(0, 200.0f);

                                // Left Stick Visualizer Box
                                ImGui::Text("Left Stick (Move)");
                                ImVec2 lsCenter = ImVec2(p.x + 80, p.y + 70);
                                float stickBoxRadius = 55.0f;
                                drawList->AddCircleFilled(lsCenter, stickBoxRadius, IM_COL32(35, 35, 45, 255), 32);
                                drawList->AddCircle(lsCenter, stickBoxRadius, IM_COL32(80, 80, 100, 255), 32, 1.5f);
                                drawList->AddCircle(lsCenter, stickBoxRadius * dualSense.GetStickDeadzone(), IM_COL32(120, 60, 60, 180), 24, 1.0f);
                                drawList->AddLine(ImVec2(lsCenter.x - stickBoxRadius, lsCenter.y), ImVec2(lsCenter.x + stickBoxRadius, lsCenter.y), IM_COL32(70, 70, 80, 255));
                                drawList->AddLine(ImVec2(lsCenter.x, lsCenter.y - stickBoxRadius), ImVec2(lsCenter.x, lsCenter.y + stickBoxRadius), IM_COL32(70, 70, 80, 255));

                                float lx = dualSense.GetLeftStickX();
                                float ly = dualSense.GetLeftStickY();
                                ImVec2 lsDot = ImVec2(lsCenter.x + lx * (stickBoxRadius - 8.0f), lsCenter.y - ly * (stickBoxRadius - 8.0f));
                                drawList->AddCircleFilled(lsDot, 7.0f, IM_COL32(80, 200, 255, 255));
                                drawList->AddCircle(lsDot, 7.0f, IM_COL32(255, 255, 255, 255), 16, 1.5f);

                                ImGui::Dummy(ImVec2(160, 130));
                                ImGui::Text("X: %+.2f | Y: %+.2f", lx, ly);

                                ImGui::NextColumn();

                                // Right Stick Visualizer Box
                                ImGui::Text("Right Stick (Look)");
                                ImVec2 rsCenter = ImVec2(p.x + 240, p.y + 70);
                                drawList->AddCircleFilled(rsCenter, stickBoxRadius, IM_COL32(35, 35, 45, 255), 32);
                                drawList->AddCircle(rsCenter, stickBoxRadius, IM_COL32(80, 80, 100, 255), 32, 1.5f);
                                drawList->AddCircle(rsCenter, stickBoxRadius * dualSense.GetStickDeadzone(), IM_COL32(120, 60, 60, 180), 24, 1.0f);
                                drawList->AddLine(ImVec2(rsCenter.x - stickBoxRadius, rsCenter.y), ImVec2(rsCenter.x + stickBoxRadius, rsCenter.y), IM_COL32(70, 70, 80, 255));
                                drawList->AddLine(ImVec2(rsCenter.x, rsCenter.y - stickBoxRadius), ImVec2(rsCenter.x, rsCenter.y + stickBoxRadius), IM_COL32(70, 70, 80, 255));

                                float rx = dualSense.GetRightStickX();
                                float ry = dualSense.GetRightStickY();
                                ImVec2 rsDot = ImVec2(rsCenter.x + rx * (stickBoxRadius - 8.0f), rsCenter.y - ry * (stickBoxRadius - 8.0f));
                                drawList->AddCircleFilled(rsDot, 7.0f, IM_COL32(255, 140, 50, 255));
                                drawList->AddCircle(rsDot, 7.0f, IM_COL32(255, 255, 255, 255), 16, 1.5f);

                                ImGui::Dummy(ImVec2(160, 130));
                                ImGui::Text("X: %+.2f | Y: %+.2f", rx, ry);

                                ImGui::Columns(1);
                                ImGui::Separator();

                                // Triggers (L2 / R2)
                                ImGui::Text("Analog Triggers:");
                                float l2Val = dualSense.GetLeftTrigger();
                                float r2Val = dualSense.GetRightTrigger();
                                char l2Buf[32], r2Buf[32];
                                snprintf(l2Buf, sizeof(l2Buf), "L2 (Aim): %.0f%%", l2Val * 100.0f);
                                snprintf(r2Buf, sizeof(r2Buf), "R2 (Shoot): %.0f%%", r2Val * 100.0f);
                                ImGui::ProgressBar(l2Val, ImVec2(240, 20), l2Buf);
                                ImGui::SameLine();
                                ImGui::ProgressBar(r2Val, ImVec2(240, 20), r2Buf);

                                ImGui::Spacing();
                                ImGui::SeparatorText("Buttons State");

                                auto RenderButtonBadge = [&](const char* label, bool active, const ImVec4& activeCol = ImVec4(0.2f, 0.7f, 1.0f, 1.0f)) {
                                    if (active) {
                                        ImGui::PushStyleColor(ImGuiCol_Button, activeCol);
                                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
                                    } else {
                                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.25f, 0.5f));
                                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                                    }
                                    ImGui::Button(label, ImVec2(62, 26));
                                    ImGui::PopStyleColor(2);
                                };

                                // Action Buttons & DPad row
                                RenderButtonBadge("[X] Cross", dualSense.IsButtonDown(EngineInput::DualSenseButton::Cross), ImVec4(0.2f, 0.6f, 1.0f, 1.0f)); ImGui::SameLine();
                                RenderButtonBadge("[O] Circle", dualSense.IsButtonDown(EngineInput::DualSenseButton::Circle), ImVec4(1.0f, 0.3f, 0.3f, 1.0f)); ImGui::SameLine();
                                RenderButtonBadge("[#] Square", dualSense.IsButtonDown(EngineInput::DualSenseButton::Square), ImVec4(0.9f, 0.4f, 0.8f, 1.0f)); ImGui::SameLine();
                                RenderButtonBadge("[^] Tri", dualSense.IsButtonDown(EngineInput::DualSenseButton::Triangle), ImVec4(0.3f, 0.9f, 0.4f, 1.0f)); ImGui::SameLine();
                                RenderButtonBadge("DPad U", dualSense.IsButtonDown(EngineInput::DualSenseButton::DPadUp)); ImGui::SameLine();
                                RenderButtonBadge("DPad D", dualSense.IsButtonDown(EngineInput::DualSenseButton::DPadDown)); ImGui::SameLine();
                                RenderButtonBadge("DPad L", dualSense.IsButtonDown(EngineInput::DualSenseButton::DPadLeft)); ImGui::SameLine();
                                RenderButtonBadge("DPad R", dualSense.IsButtonDown(EngineInput::DualSenseButton::DPadRight));

                                // Shoulder & System Buttons row
                                RenderButtonBadge("L1", dualSense.IsButtonDown(EngineInput::DualSenseButton::L1)); ImGui::SameLine();
                                RenderButtonBadge("R1", dualSense.IsButtonDown(EngineInput::DualSenseButton::R1)); ImGui::SameLine();
                                RenderButtonBadge("L2 Clk", dualSense.IsButtonDown(EngineInput::DualSenseButton::L2Button)); ImGui::SameLine();
                                RenderButtonBadge("R2 Clk", dualSense.IsButtonDown(EngineInput::DualSenseButton::R2Button)); ImGui::SameLine();
                                RenderButtonBadge("L3", dualSense.IsButtonDown(EngineInput::DualSenseButton::L3)); ImGui::SameLine();
                                RenderButtonBadge("R3", dualSense.IsButtonDown(EngineInput::DualSenseButton::R3)); ImGui::SameLine();
                                RenderButtonBadge("Create", dualSense.IsButtonDown(EngineInput::DualSenseButton::Create)); ImGui::SameLine();
                                RenderButtonBadge("Options", dualSense.IsButtonDown(EngineInput::DualSenseButton::Options));

                                // Touch, PS, Mute row
                                RenderButtonBadge("PS Home", dualSense.IsButtonDown(EngineInput::DualSenseButton::PS), ImVec4(0.9f, 0.8f, 0.2f, 1.0f)); ImGui::SameLine();
                                RenderButtonBadge("Touchpad", dualSense.IsButtonDown(EngineInput::DualSenseButton::Touchpad)); ImGui::SameLine();
                                RenderButtonBadge("Mic Mute", dualSense.IsButtonDown(EngineInput::DualSenseButton::MicMute), ImVec4(1.0f, 0.5f, 0.1f, 1.0f));

                                ImGui::Spacing();
                                ImGui::SeparatorText("Multi-Touch Touchpad (2 Points)");
                                const auto& t0 = dualSense.GetTouch(0);
                                const auto& t1 = dualSense.GetTouch(1);

                                ImVec2 tpPos = ImGui::GetCursorScreenPos();
                                float tpW = 320.0f;
                                float tpH = 90.0f;
                                drawList->AddRectFilled(tpPos, ImVec2(tpPos.x + tpW, tpPos.y + tpH), IM_COL32(30, 30, 38, 255), 6.0f);
                                drawList->AddRect(tpPos, ImVec2(tpPos.x + tpW, tpPos.y + tpH), IM_COL32(70, 70, 90, 255), 6.0f);

                                if (t0.active) {
                                    ImVec2 pt = ImVec2(tpPos.x + t0.x * tpW, tpPos.y + t0.y * tpH);
                                    drawList->AddCircleFilled(pt, 8.0f, IM_COL32(50, 180, 255, 255));
                                    drawList->AddText(ImVec2(pt.x + 10, pt.y - 8), IM_COL32(255, 255, 255, 255), "T0");
                                }
                                if (t1.active) {
                                    ImVec2 pt = ImVec2(tpPos.x + t1.x * tpW, tpPos.y + t1.y * tpH);
                                    drawList->AddCircleFilled(pt, 8.0f, IM_COL32(255, 100, 100, 255));
                                    drawList->AddText(ImVec2(pt.x + 10, pt.y - 8), IM_COL32(255, 255, 255, 255), "T1");
                                }
                                ImGui::Dummy(ImVec2(tpW, tpH + 5));
                                ImGui::Text("Touch 0: %s (%.2f, %.2f) | Touch 1: %s (%.2f, %.2f)",
                                    t0.active ? "Active" : "Inactive", t0.x, t0.y,
                                    t1.active ? "Active" : "Inactive", t1.x, t1.y);

                                ImGui::Spacing();
                                ImGui::SeparatorText("6-Axis Motion & IMU (Gyro + Accelerometer)");
                                const auto& motion = dualSense.GetMotion();
                                ImGui::Text("Gyro (deg/s): Pitch: %+.1f | Yaw: %+.1f | Roll: %+.1f", motion.gyroPitch, motion.gyroYaw, motion.gyroRoll);
                                ImGui::Text("Accel (G):    X: %+.2f | Y: %+.2f | Z: %+.2f", motion.accelX, motion.accelY, motion.accelZ);
                                if (ImGui::Button("Calibrate Gyro Zero Drift", ImVec2(220, 26))) {
                                    dualSense.CalibrateGyro();
                                }
                                ImGui::SameLine();
                                ImGui::TextDisabled("(Keep controller still on flat surface)");

                                ImGui::EndTabItem();
                            }

                            // ----------------------------------------------------
                            // Tab 2: Adaptive Triggers Studio
                            // ----------------------------------------------------
                            if (ImGui::BeginTabItem("Adaptive Triggers Studio")) {
                                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Real-time Hardware Adaptive Triggers Configuration:");
                                ImGui::Spacing();

                                const char* effectNames[] = {
                                    "Off / Normal (Smooth)",
                                    "Continuous Resistance (Uniform Force)",
                                    "Section Resistance (Zone Force)",
                                    "Galloping Weapon / Machine Gun Recoil",
                                    "Bow & Arrow Tension",
                                    "Rigid Stop (Physical Trigger Stop)",
                                    "Tactile Vibration Buzz",
                                    "Custom Direct Parameters"
                                };

                                static int leftModeIdx = 1; // Default Left Trigger: Continuous Resistance (Realistic Aim)
                                static int rightModeIdx = 3; // Default Right Trigger: Galloping Weapon Recoil
                                static int leftStartPos = 35, leftEndPos = 220, leftForce = 140, leftFreq = 120;
                                static int rightStartPos = 30, rightEndPos = 200, rightForce = 220, rightFreq = 160;
                                static bool s_triggersInitialized = false;

                                auto ApplyTriggerConfig = [&]() {
                                    EngineInput::DualSenseTriggerConfig lCfg, rCfg;
                                    lCfg.effect = static_cast<EngineInput::DualSenseTriggerEffect>(leftModeIdx);
                                    lCfg.startPosition = (uint8_t)leftStartPos;
                                    lCfg.endPosition = (uint8_t)leftEndPos;
                                    lCfg.force = (uint8_t)leftForce;
                                    lCfg.frequency = (uint8_t)leftFreq;
                                    lCfg.extraForce = (uint8_t)leftForce;

                                    rCfg.effect = static_cast<EngineInput::DualSenseTriggerEffect>(rightModeIdx);
                                    rCfg.startPosition = (uint8_t)rightStartPos;
                                    rCfg.endPosition = (uint8_t)rightEndPos;
                                    rCfg.force = (uint8_t)rightForce;
                                    rCfg.frequency = (uint8_t)rightFreq;
                                    rCfg.extraForce = (uint8_t)rightForce;

                                    dualSense.SetLeftTriggerEffect(lCfg);
                                    dualSense.SetRightTriggerEffect(rCfg);
                                };

                                if (!s_triggersInitialized) {
                                    s_triggersInitialized = true;
                                    const auto& curL = dualSense.GetLeftTriggerConfig();
                                    const auto& curR = dualSense.GetRightTriggerConfig();
                                    if (curL.effect != EngineInput::DualSenseTriggerEffect::None) {
                                        leftModeIdx = static_cast<int>(curL.effect);
                                        leftStartPos = curL.startPosition;
                                        leftEndPos = curL.endPosition;
                                        leftForce = curL.force;
                                        leftFreq = curL.frequency;
                                    }
                                    if (curR.effect != EngineInput::DualSenseTriggerEffect::None) {
                                        rightModeIdx = static_cast<int>(curR.effect);
                                        rightStartPos = curR.startPosition;
                                        rightEndPos = curR.endPosition;
                                        rightForce = curR.force;
                                        rightFreq = curR.frequency;
                                    }
                                }

                                ImGui::Columns(2, "TriggerCols", true);

                                // Left Trigger (L2)
                                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Left Trigger (L2 - Aim)");
                                float l2Val = dualSense.GetLeftTrigger();
                                char l2Buf[32]; snprintf(l2Buf, sizeof(l2Buf), "L2 Pull: %.0f%%", l2Val * 100.0f);
                                ImGui::ProgressBar(l2Val, ImVec2(-1, 20), l2Buf);

                                bool l2Active = (l2Val * 255.0f >= leftStartPos && (leftModeIdx == 1 || l2Val * 255.0f <= leftEndPos) && leftModeIdx != 0);
                                if (l2Active) ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "● RESISTANCE ACTIVE");
                                else ImGui::TextDisabled("○ Free Pull");

                                if (ImGui::Combo("##L2Effect", &leftModeIdx, effectNames, IM_ARRAYSIZE(effectNames))) {
                                    ApplyTriggerConfig();
                                }
                                if (ImGui::SliderInt("L2 Start Pos", &leftStartPos, 0, 255)) ApplyTriggerConfig();
                                if (ImGui::SliderInt("L2 End Pos", &leftEndPos, 0, 255)) ApplyTriggerConfig();
                                if (ImGui::SliderInt("L2 Force", &leftForce, 0, 255)) ApplyTriggerConfig();
                                if (ImGui::SliderInt("L2 Frequency", &leftFreq, 0, 255)) ApplyTriggerConfig();

                                ImGui::NextColumn();

                                // Right Trigger (R2)
                                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Right Trigger (R2 - Weapon / Shoot)");
                                float r2Val = dualSense.GetRightTrigger();
                                char r2Buf[32]; snprintf(r2Buf, sizeof(r2Buf), "R2 Pull: %.0f%%", r2Val * 100.0f);
                                ImGui::ProgressBar(r2Val, ImVec2(-1, 20), r2Buf);

                                bool r2Active = (r2Val * 255.0f >= rightStartPos && (rightModeIdx == 1 || rightModeIdx == 5 || r2Val * 255.0f <= rightEndPos) && rightModeIdx != 0);
                                if (r2Active) ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "● RESISTANCE ACTIVE");
                                else ImGui::TextDisabled("○ Free Pull");

                                if (ImGui::Combo("##R2Effect", &rightModeIdx, effectNames, IM_ARRAYSIZE(effectNames))) {
                                    ApplyTriggerConfig();
                                }
                                if (ImGui::SliderInt("R2 Start Pos", &rightStartPos, 0, 255)) ApplyTriggerConfig();
                                if (ImGui::SliderInt("R2 End Pos", &rightEndPos, 0, 255)) ApplyTriggerConfig();
                                if (ImGui::SliderInt("R2 Force", &rightForce, 0, 255)) ApplyTriggerConfig();
                                if (ImGui::SliderInt("R2 Frequency", &rightFreq, 0, 255)) ApplyTriggerConfig();

                                ImGui::Columns(1);
                                ImGui::Spacing();
                                ImGui::SeparatorText("Quick Weapon Presets for Right Trigger (R2)");

                                if (ImGui::Button("Continuous Resistance (Heavy Pull)", ImVec2(230, 28))) {
                                    rightModeIdx = 1; // Continuous resistance
                                    rightStartPos = 10;
                                    rightForce = 220;
                                    ApplyTriggerConfig();
                                    EngineAudio::DualSenseAudioSystem::Get().PlayReloadSound(0.85f);
                                }
                                ImGui::SameLine();
                                if (ImGui::Button("Pistol (Crisp Wall Break)", ImVec2(180, 28))) {
                                    rightModeIdx = 5; // Rigid stop (0x25 Weapon wall)
                                    rightStartPos = 60;
                                    rightForce = 255;
                                    ApplyTriggerConfig();
                                    EngineAudio::DualSenseAudioSystem::Get().PlayReloadSound(0.85f);
                                }
                                ImGui::SameLine();
                                if (ImGui::Button("Machine Gun (Galloping Recoil)", ImVec2(210, 28))) {
                                    rightModeIdx = 3; // Galloping weapon
                                    rightStartPos = 20;
                                    rightEndPos = 220;
                                    rightForce = 240;
                                    rightFreq = 180;
                                    ApplyTriggerConfig();
                                    EngineAudio::DualSenseAudioSystem::Get().PlayReloadSound(0.85f);
                                }

                                if (ImGui::Button("Shotgun (Heavy Wall)", ImVec2(160, 28))) {
                                    rightModeIdx = 5; // Rigid stop
                                    rightStartPos = 80;
                                    rightForce = 255;
                                    ApplyTriggerConfig();
                                    EngineAudio::DualSenseAudioSystem::Get().PlayReloadSound(0.85f);
                                }
                                ImGui::SameLine();
                                if (ImGui::Button("Bow & Arrow (Pull Tension)", ImVec2(190, 28))) {
                                    rightModeIdx = 4; // Bow
                                    rightStartPos = 20;
                                    rightEndPos = 230;
                                    rightForce = 180;
                                    ApplyTriggerConfig();
                                    EngineAudio::DualSenseAudioSystem::Get().PlayReloadSound(0.85f);
                                }
                                ImGui::SameLine();
                                if (ImGui::Button("Vibration Buzz (R2)", ImVec2(160, 28))) {
                                    rightModeIdx = 6; // Vibration
                                    rightStartPos = 25;
                                    rightForce = 220;
                                    rightFreq = 42;
                                    ApplyTriggerConfig();
                                    EngineAudio::DualSenseAudioSystem::Get().PlayChimeSound(600.0f, 0.12f, 0.8f);
                                }
                                ImGui::SameLine();
                                if (ImGui::Button("Dual Vibration (L2 + R2)", ImVec2(180, 28))) {
                                    leftModeIdx = 6;
                                    leftStartPos = 25;
                                    leftForce = 200;
                                    leftFreq = 40;
                                    rightModeIdx = 6;
                                    rightStartPos = 25;
                                    rightForce = 220;
                                    rightFreq = 44;
                                    ApplyTriggerConfig();
                                    EngineAudio::DualSenseAudioSystem::Get().PlayChimeSound(600.0f, 0.12f, 0.8f);
                                }
                                ImGui::SameLine();
                                if (ImGui::Button("Reset to Normal", ImVec2(140, 28))) {
                                    leftModeIdx = 0;
                                    rightModeIdx = 0;
                                    dualSense.ResetTriggers();
                                }

                                ImGui::Spacing();
                                ImGui::SeparatorText(" Adaptive Trigger Vibration Controller");
                                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Direct Hardware Trigger Vibration (L2 / R2 Geared Feedback Motors):");
                                
                                static int s_vibTarget = 2; // 0=L2, 1=R2, 2=Both
                                static int s_vibAmp = 8; // 1 to 8
                                static int s_vibFreq = 42; // 10 to 250 Hz
                                static int s_vibStartPos = 0; // 0 to 255
                                static int s_vibEndPos = 255; // 0 to 255
                                static bool s_vibContinuous = false;

                                ImGui::Text("Target Trigger:");
                                ImGui::SameLine();
                                ImGui::RadioButton("Left (L2)", &s_vibTarget, 0);
                                ImGui::SameLine();
                                ImGui::RadioButton("Right (R2)", &s_vibTarget, 1);
                                ImGui::SameLine();
                                ImGui::RadioButton("Both (L2 + R2)", &s_vibTarget, 2);

                                const char* ampDesc = (s_vibAmp <= 2) ? "Subtle Hum" : ((s_vibAmp <= 4) ? "Moderate Buzz" : ((s_vibAmp <= 6) ? "Strong Vibration" : "Maximum Recoil Kick"));
                                ImGui::SliderInt("Vibration Amplitude", &s_vibAmp, 1, 8, "%d [1..8]");
                                ImGui::SameLine();
                                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "(%s)", ampDesc);

                                const char* freqDesc = (s_vibFreq < 35) ? "Heavy Pulsing Clicks" : ((s_vibFreq < 70) ? "Weapon Recoil Chatter" : ((s_vibFreq < 140) ? "Engine / Motor Hum" : "High-Speed Electric Buzz"));
                                ImGui::SliderInt("Vibration Frequency", &s_vibFreq, 10, 250, "%d Hz");
                                ImGui::SameLine();
                                ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "(%s)", freqDesc);

                                ImGui::SliderInt("Pull Start Position", &s_vibStartPos, 0, 255, "%d / 255");
                                ImGui::SliderInt("Pull End Position", &s_vibEndPos, 0, 255, "%d / 255");
                                if (s_vibEndPos < s_vibStartPos) s_vibEndPos = s_vibStartPos;

                                auto ApplyVibrationToTarget = [&](bool continuous) {
                                    uint8_t amp = (uint8_t)s_vibAmp;
                                    uint8_t freq = (uint8_t)s_vibFreq;
                                    uint8_t start = (uint8_t)s_vibStartPos;
                                    uint8_t end = (uint8_t)s_vibEndPos;

                                    if (s_vibTarget == 0 || s_vibTarget == 2) {
                                        dualSense.SetLeftTriggerVibration(start, end, amp, freq);
                                        leftModeIdx = 6;
                                        leftStartPos = start;
                                        leftEndPos = end;
                                        leftForce = amp * 31;
                                        leftFreq = freq;
                                    }
                                    if (s_vibTarget == 1 || s_vibTarget == 2) {
                                        dualSense.SetRightTriggerVibration(start, end, amp, freq);
                                        rightModeIdx = 6;
                                        rightStartPos = start;
                                        rightEndPos = end;
                                        rightForce = amp * 31;
                                        rightFreq = freq;
                                    }
                                };

                                if (ImGui::Checkbox(" Enable Live Continuous Trigger Vibration (Feel in hands now)", &s_vibContinuous)) {
                                    if (s_vibContinuous) {
                                        ApplyVibrationToTarget(true);
                                    } else {
                                        dualSense.StopTriggerVibration(s_vibTarget == 0 || s_vibTarget == 2, s_vibTarget == 1 || s_vibTarget == 2);
                                        if (s_vibTarget == 0 || s_vibTarget == 2) leftModeIdx = 0;
                                        if (s_vibTarget == 1 || s_vibTarget == 2) rightModeIdx = 0;
                                    }
                                }
                                if (s_vibContinuous) {
                                    ApplyVibrationToTarget(true);
                                }

                                ImGui::Spacing();
                                ImGui::Text("Timed Vibration Bursts:");
                                if (ImGui::Button(" 0.2s Quick Tap", ImVec2(140, 26))) {
                                    dualSense.PlayTriggerVibration(s_vibTarget == 0 || s_vibTarget == 2, s_vibTarget == 1 || s_vibTarget == 2, (uint8_t)s_vibAmp, (uint8_t)s_vibFreq, 0.2f);
                                }
                                ImGui::SameLine();
                                if (ImGui::Button(" 0.5s Recoil Burst", ImVec2(150, 26))) {
                                    dualSense.PlayTriggerVibration(s_vibTarget == 0 || s_vibTarget == 2, s_vibTarget == 1 || s_vibTarget == 2, (uint8_t)s_vibAmp, (uint8_t)s_vibFreq, 0.5f);
                                }
                                ImGui::SameLine();
                                if (ImGui::Button(" 1.0s Heavy Shake", ImVec2(150, 26))) {
                                    dualSense.PlayTriggerVibration(s_vibTarget == 0 || s_vibTarget == 2, s_vibTarget == 1 || s_vibTarget == 2, (uint8_t)s_vibAmp, (uint8_t)s_vibFreq, 1.0f);
                                }
                                ImGui::SameLine();
                                if (ImGui::Button(" Stop Vibration", ImVec2(140, 26))) {
                                    s_vibContinuous = false;
                                    dualSense.StopTriggerVibration(true, true);
                                    leftModeIdx = 0;
                                    rightModeIdx = 0;
                                }

                                ImGui::Spacing();
                                ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "Tactile Vibration Feel Presets:");
                                if (ImGui::Button(" Assault Rifle (42 Hz)")) {
                                    s_vibAmp = 8; s_vibFreq = 42; s_vibStartPos = 25; s_vibEndPos = 255;
                                    ApplyVibrationToTarget(s_vibContinuous);
                                    dualSense.PlayTriggerVibration(s_vibTarget == 0 || s_vibTarget == 2, s_vibTarget == 1 || s_vibTarget == 2, (uint8_t)s_vibAmp, (uint8_t)s_vibFreq, 0.4f);
                                    EngineAudio::DualSenseAudioSystem::Get().PlayShootSound(1.0f);
                                }
                                ImGui::SameLine();
                                if (ImGui::Button(" Electric Shock (180 Hz)")) {
                                    s_vibAmp = 7; s_vibFreq = 180; s_vibStartPos = 0; s_vibEndPos = 255;
                                    ApplyVibrationToTarget(s_vibContinuous);
                                    dualSense.PlayTriggerVibration(s_vibTarget == 0 || s_vibTarget == 2, s_vibTarget == 1 || s_vibTarget == 2, (uint8_t)s_vibAmp, (uint8_t)s_vibFreq, 0.4f);
                                    EngineAudio::DualSenseAudioSystem::Get().PlayChimeSound(800.0f, 0.15f, 0.8f);
                                }
                                ImGui::SameLine();
                                if (ImGui::Button(" Engine RPM Hum (75 Hz)")) {
                                    s_vibAmp = 5; s_vibFreq = 75; s_vibStartPos = 15; s_vibEndPos = 255;
                                    ApplyVibrationToTarget(s_vibContinuous);
                                    dualSense.PlayTriggerVibration(s_vibTarget == 0 || s_vibTarget == 2, s_vibTarget == 1 || s_vibTarget == 2, (uint8_t)s_vibAmp, (uint8_t)s_vibFreq, 0.5f);
                                }
                                ImGui::SameLine();
                                if (ImGui::Button(" Helicopter Beat (16 Hz)")) {
                                    s_vibAmp = 8; s_vibFreq = 16; s_vibStartPos = 0; s_vibEndPos = 255;
                                    ApplyVibrationToTarget(s_vibContinuous);
                                    dualSense.PlayTriggerVibration(s_vibTarget == 0 || s_vibTarget == 2, s_vibTarget == 1 || s_vibTarget == 2, (uint8_t)s_vibAmp, (uint8_t)s_vibFreq, 0.6f);
                                }
                                ImGui::SameLine();
                                if (ImGui::Button(" Jackhammer (28 Hz)")) {
                                    s_vibAmp = 8; s_vibFreq = 28; s_vibStartPos = 10; s_vibEndPos = 255;
                                    ApplyVibrationToTarget(s_vibContinuous);
                                    dualSense.PlayTriggerVibration(s_vibTarget == 0 || s_vibTarget == 2, s_vibTarget == 1 || s_vibTarget == 2, (uint8_t)s_vibAmp, (uint8_t)s_vibFreq, 0.5f);
                                }
                                ImGui::SameLine();
                                if (ImGui::Button(" Insect Stinger (220 Hz)")) {
                                    s_vibAmp = 4; s_vibFreq = 220; s_vibStartPos = 30; s_vibEndPos = 255;
                                    ApplyVibrationToTarget(s_vibContinuous);
                                    dualSense.PlayTriggerVibration(s_vibTarget == 0 || s_vibTarget == 2, s_vibTarget == 1 || s_vibTarget == 2, (uint8_t)s_vibAmp, (uint8_t)s_vibFreq, 0.4f);
                                }

                                ImGui::EndTabItem();
                            }

                            // ----------------------------------------------------
                            // Tab 3: Lightbar & Player LEDs
                            // ----------------------------------------------------
                            if (ImGui::BeginTabItem("Lightbar & LEDs")) {
                                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "RGB Lightbar Mode & Color:");
                                const char* lbModes[] = {
                                    "Custom RGB Color",
                                    "Rainbow Spectrum Cycle",
                                    "Battery Level Gauge (Green > Yellow > Red)",
                                    "Audio Reactive (Pulses with Sound)",
                                    "Breathing Glow Pulse",
                                    "Atmosphere & Weather Sync"
                                };
                                int currentLightMode = dualSense.GetLightbarMode();
                                if (ImGui::Combo("Lightbar Mode", &currentLightMode, lbModes, IM_ARRAYSIZE(lbModes))) {
                                    dualSense.SetLightbarMode(currentLightMode);
                                }

                                if (currentLightMode == 0) {
                                    if (ImGui::ColorEdit3("Color", s_customLightColor)) {
                                        dualSense.SetLightbarColor(s_customLightColor[0], s_customLightColor[1], s_customLightColor[2]);
                                    }

                                    ImGui::Spacing();
                                    ImGui::Text("Quick Color Presets:");
                                    auto SetColorPreset = [&](const char* label, float r, float g, float b, const ImVec4& btnCol) {
                                        ImGui::PushStyleColor(ImGuiCol_Button, btnCol);
                                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
                                        if (ImGui::Button(label)) {
                                            s_customLightColor[0] = r;
                                            s_customLightColor[1] = g;
                                            s_customLightColor[2] = b;
                                            dualSense.SetLightbarColor(r, g, b);
                                        }
                                        ImGui::PopStyleColor(2);
                                    };

                                    SetColorPreset("PlayStation Blue", 0.0f, 0.47f, 1.0f, ImVec4(0.0f, 0.35f, 0.85f, 1.0f)); ImGui::SameLine();
                                    SetColorPreset("Crimson Red", 1.0f, 0.08f, 0.08f, ImVec4(0.8f, 0.1f, 0.1f, 1.0f)); ImGui::SameLine();
                                    SetColorPreset("Toxic Green", 0.1f, 1.0f, 0.2f, ImVec4(0.1f, 0.7f, 0.2f, 1.0f)); ImGui::SameLine();
                                    SetColorPreset("Cyber Yellow", 1.0f, 0.85f, 0.0f, ImVec4(0.8f, 0.65f, 0.0f, 1.0f)); ImGui::SameLine();
                                    SetColorPreset("Neon Purple", 0.7f, 0.1f, 1.0f, ImVec4(0.55f, 0.1f, 0.8f, 1.0f)); ImGui::SameLine();
                                    SetColorPreset("Pure White", 1.0f, 1.0f, 1.0f, ImVec4(0.5f, 0.5f, 0.5f, 1.0f)); ImGui::SameLine();
                                    SetColorPreset("Turn Off (Black)", 0.0f, 0.0f, 0.0f, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
                                }

                                ImGui::Spacing();
                                ImGui::SeparatorText("Player Indicators (White LEDs)");

                                const char* pLedNames[] = { "Off (No LEDs)", "Player 1 (Center dot)", "Player 2 (2 & 4 dots)", "Player 3 (1, 3, 5 dots)", "Player 4 (1, 2, 4, 5 dots)", "All 5 LEDs", "Custom Individual Dots" };
                                static int pLedIdx = 1;
                                static bool s_indLed[5] = { false, false, true, false, false };

                                auto ApplyPlayerLeds = [&]() {
                                    if (pLedIdx == 0) {
                                        dualSense.SetPlayerLed(EngineInput::DualSensePlayerLed::None);
                                    } else if (pLedIdx == 1) {
                                        dualSense.SetPlayerLed(EngineInput::DualSensePlayerLed::Player1);
                                    } else if (pLedIdx == 2) {
                                        dualSense.SetPlayerLed(EngineInput::DualSensePlayerLed::Player2);
                                    } else if (pLedIdx == 3) {
                                        dualSense.SetPlayerLed(EngineInput::DualSensePlayerLed::Player3);
                                    } else if (pLedIdx == 4) {
                                        dualSense.SetPlayerLed(EngineInput::DualSensePlayerLed::Player4);
                                    } else if (pLedIdx == 5) {
                                        dualSense.SetPlayerLed(EngineInput::DualSensePlayerLed::All);
                                    } else {
                                        uint8_t mask = 0;
                                        if (s_indLed[0]) mask |= 0x01;
                                        if (s_indLed[1]) mask |= 0x02;
                                        if (s_indLed[2]) mask |= 0x04;
                                        if (s_indLed[3]) mask |= 0x08;
                                        if (s_indLed[4]) mask |= 0x10;
                                        dualSense.SetPlayerLed(static_cast<EngineInput::DualSensePlayerLed>(mask));
                                    }
                                };

                                if (ImGui::Combo("Player Preset", &pLedIdx, pLedNames, IM_ARRAYSIZE(pLedNames))) {
                                    ApplyPlayerLeds();
                                }

                                if (pLedIdx == 6) {
                                    ImGui::Text("Individual LED Toggles:");
                                    if (ImGui::Checkbox("LED 1 (Left)", &s_indLed[0])) ApplyPlayerLeds(); ImGui::SameLine();
                                    if (ImGui::Checkbox("LED 2", &s_indLed[1])) ApplyPlayerLeds(); ImGui::SameLine();
                                    if (ImGui::Checkbox("LED 3 (Mid)", &s_indLed[2])) ApplyPlayerLeds(); ImGui::SameLine();
                                    if (ImGui::Checkbox("LED 4", &s_indLed[3])) ApplyPlayerLeds(); ImGui::SameLine();
                                    if (ImGui::Checkbox("LED 5 (Right)", &s_indLed[4])) ApplyPlayerLeds();
                                }

                                ImGui::Spacing();
                                const char* muteLedNames[] = { "Off", "Solid Orange", "Pulsing Orange" };
                                static int muteLedIdx = 0;
                                if (ImGui::Combo("Mic Mute LED", &muteLedIdx, muteLedNames, IM_ARRAYSIZE(muteLedNames))) {
                                    dualSense.SetMuteLed(static_cast<EngineInput::DualSenseMuteLed>(muteLedIdx));
                                }

                                ImGui::Spacing();
                                ImGui::SeparatorText("Window Focus & Background Control");
                                bool keepColor = dualSense.GetKeepColorInBackground();
                                if (ImGui::Checkbox("Keep Game Lightbar Active When Minimized", &keepColor)) {
                                    dualSense.SetKeepColorInBackground(keepColor);
                                }
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip("Default [OFF]: When minimized or unfocused, the engine yields control so Steam Desktop Layout operates without flicker. Game color restores instantly on focus.\nWhen [ON]: The engine continuously asserts its lightbar color even when minimized.");
                                }

                                ImGui::EndTabItem();
                            }

                            // ----------------------------------------------------
                            // Tab 4: Haptics & Audio-to-Haptics Studio
                            // ----------------------------------------------------
                            if (ImGui::BeginTabItem("Haptics & Audio-to-Haptics")) {
                                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Dual Rumble Motors Test:");
                                if (ImGui::SliderFloat("Left Motor (Heavy / Low Freq)", &s_leftRumbleSlider, 0.0f, 1.0f, "%.2f")) {
                                    dualSense.SetRumble(s_leftRumbleSlider, s_rightRumbleSlider);
                                }
                                if (ImGui::SliderFloat("Right Motor (Fine / High Freq)", &s_rightRumbleSlider, 0.0f, 1.0f, "%.2f")) {
                                    dualSense.SetRumble(s_leftRumbleSlider, s_rightRumbleSlider);
                                }

                                if (ImGui::Button("Test 0.5s Pulse", ImVec2(140, 26))) {
                                    dualSense.PlayRumble(s_leftRumbleSlider > 0.01f ? s_leftRumbleSlider : 0.7f,
                                                          s_rightRumbleSlider > 0.01f ? s_rightRumbleSlider : 0.5f,
                                                          0.5f);
                                }
                                ImGui::SameLine();
                                if (ImGui::Button("Stop Rumble", ImVec2(120, 26))) {
                                    s_leftRumbleSlider = 0.0f;
                                    s_rightRumbleSlider = 0.0f;
                                    dualSense.SetRumble(0.0f, 0.0f);
                                }

                                ImGui::Spacing();
                                ImGui::SeparatorText("Audio-to-Haptics (Sound-to-Rumble Engine)");
                                bool a2h = dualSense.IsAudioToHaptics();
                                if (ImGui::Checkbox("Enable Audio-to-Haptics", &a2h)) {
                                    dualSense.SetAudioToHaptics(a2h);
                                }
                                float a2hGain = dualSense.GetAudioToHapticsGain();
                                if (ImGui::SliderFloat("Haptic Sensitivity (Gain)", &a2hGain, 0.2f, 3.0f, "%.2fx")) {
                                    dualSense.SetAudioToHapticsGain(a2hGain);
                                }
                                float audioEnergy = EngineAudio::DualSenseAudioSystem::Get().GetAudioEnergy();
                                char eBuf[32]; snprintf(eBuf, sizeof(eBuf), "Live Audio Output Energy: %.0f%%", audioEnergy * 100.0f);
                                ImGui::ProgressBar(audioEnergy, ImVec2(-1, 20), eBuf);
                                ImGui::TextDisabled("Translates gunshots, explosions, and UI sounds from the engine directly into controller haptic vibrations!");

                                ImGui::Spacing();
                                ImGui::SeparatorText("Environmental Haptics");
                                ImGui::Checkbox("Enable Rain & Storm Haptic Feedback", &s_weatherHaptics);
                                ImGui::TextDisabled("Generates micro-haptic raindrop sensations and lightning thunder rumble.");

                                ImGui::EndTabItem();
                            }

                            // ----------------------------------------------------
                            // Tab 5: Touchpad & Gyro Aiming Studio
                            // ----------------------------------------------------
                            if (ImGui::BeginTabItem("Touchpad & Gyro")) {
                                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Touchpad as Mouse (Trackpad):");
                                bool touchMouse = dualSense.IsTouchpadAsMouse();
                                if (ImGui::Checkbox("Enable Touchpad as Mouse Cursor (Windows Trackpad)", &touchMouse)) {
                                    dualSense.SetTouchpadAsMouse(touchMouse);
                                }
                                float touchSens = dualSense.GetTouchpadSensitivity();
                                if (ImGui::SliderFloat("Trackpad Sensitivity", &touchSens, 0.2f, 3.0f, "%.2fx")) {
                                    dualSense.SetTouchpadSensitivity(touchSens);
                                }
                                ImGui::TextDisabled("Slide your finger across the DualSense touchpad to steer mouse. Click touchpad to left-click.");

                                ImGui::Spacing();
                                ImGui::SeparatorText("Gyro Motion Aiming (DualSense 6-Axis IMU)");
                                bool gyroAim = dualSense.IsGyroAimingEnabled();
                                if (ImGui::Checkbox("Enable Always-On Gyro Aiming", &gyroAim)) {
                                    dualSense.SetGyroAimingEnabled(gyroAim);
                                }
                                ImGui::TextDisabled("(Note: Gyro Aiming also activates automatically when pulling L2 trigger to aim)");

                                float gyroYaw = dualSense.GetGyroYawSensitivity();
                                float gyroPitch = dualSense.GetGyroPitchSensitivity();
                                if (ImGui::SliderFloat("Gyro Yaw Sensitivity", &gyroYaw, 0.1f, 5.0f, "%.2fx")) {
                                    dualSense.SetGyroSensitivity(gyroYaw, gyroPitch);
                                }
                                if (ImGui::SliderFloat("Gyro Pitch Sensitivity", &gyroPitch, 0.1f, 5.0f, "%.2fx")) {
                                    dualSense.SetGyroSensitivity(gyroYaw, gyroPitch);
                                }

                                const auto& motion = dualSense.GetMotion();
                                ImGui::Text("Gyro: Yaw = %+.1f deg/s | Pitch = %+.1f deg/s | Roll = %+.1f deg/s", motion.gyroYaw, motion.gyroPitch, motion.gyroRoll);
                                ImGui::Text("Accel: X = %+.2fG | Y = %+.2fG | Z = %+.2fG", motion.accelX, motion.accelY, motion.accelZ);
                                if (ImGui::Button(" Calibrate Gyro & Zero Drift", ImVec2(240, 26))) {
                                    dualSense.CalibrateGyro();
                                }

                                ImGui::Spacing();
                                ImGui::SeparatorText("Camera Look Sensitivity & Direction");
                                ImGui::SliderFloat("Stick Look Sensitivity", &s_stickLookSens, 0.5f, 8.0f, "%.1fx");
                                ImGui::Checkbox("Invert Y-Axis (Flight Style)", &s_invertLookY);

                                ImGui::Spacing();
                                ImGui::SeparatorText("Stick & Trigger Deadzones");
                                float stickDeadzone = dualSense.GetStickDeadzone();
                                float trigDeadzone = dualSense.GetTriggerDeadzone();
                                if (ImGui::SliderFloat("Stick Deadzone", &stickDeadzone, 0.01f, 0.40f, "%.2f")) {
                                    dualSense.SetDeadzone(stickDeadzone, trigDeadzone);
                                }
                                if (ImGui::SliderFloat("Trigger Deadzone", &trigDeadzone, 0.01f, 0.30f, "%.2f")) {
                                    dualSense.SetDeadzone(stickDeadzone, trigDeadzone);
                                }

                                ImGui::EndTabItem();
                            }

                            // ----------------------------------------------------
                            // Tab 6: Bluetooth & DualSense Controller Audio
                            // ----------------------------------------------------
                            if (ImGui::BeginTabItem("Speaker & Bluetooth Audio")) {
                                auto& audioSys = EngineAudio::DualSenseAudioSystem::Get();

                                // ---- Bluetooth Audio Global Enable ----
                                bool btEnabled = audioSys.IsBluetoothAudioEnabled();
                                if (ImGui::Checkbox("Enable Bluetooth Audio Mode", &btEnabled)) {
                                    audioSys.SetBluetoothAudioEnabled(btEnabled);
                                }
                                ImGui::SameLine();
                                ImGui::TextDisabled("(Auto-routes sound to BT headset/speakers when controller is Bluetooth)");

                                ImGui::Spacing();
                                ImGui::SeparatorText("Audio Output Device");

                                const auto& devices = audioSys.GetAudioDevices();
                                int currentDevIdx = audioSys.GetSelectedDeviceIndex();

                                std::vector<std::string> devDisplayNames;
                                for (size_t i = 0; i < devices.size(); ++i) {
                                    std::string tag = " [" + devices[i].busType + "]";
                                    if (devices[i].isDualSense) tag = " [DualSense PS5]";
                                    else if (devices[i].isDefault && !devices[i].isBluetooth) tag += " [Default]";
                                    devDisplayNames.push_back(devices[i].name + tag);
                                }

                                const char* currentLabel = (currentDevIdx >= 0 && currentDevIdx < (int)devDisplayNames.size())
                                    ? devDisplayNames[currentDevIdx].c_str() : "Select Audio Output Device...";
                                if (ImGui::BeginCombo("Audio Device", currentLabel)) {
                                    for (int i = 0; i < (int)devDisplayNames.size(); ++i) {
                                        bool isSelected = (currentDevIdx == i);
                                        if (ImGui::Selectable(devDisplayNames[i].c_str(), isSelected)) {
                                            audioSys.SetSelectedDevice(i);
                                        }
                                        if (isSelected) ImGui::SetItemDefaultFocus();
                                    }
                                    ImGui::EndCombo();
                                }
                                ImGui::SameLine();
                                if (ImGui::Button("Refresh List")) {
                                    audioSys.RefreshAudioDevices();
                                }
                                ImGui::SameLine();
                                if (ImGui::Button("Auto BT Device")) {
                                    audioSys.SelectBluetoothDevice();
                                }

                                // ---- Device Status Banner ----
                                ImGui::Spacing();
                                if (audioSys.IsDeviceOpen()) {
                                    if (audioSys.IsDualSenseDevice()) {
                                        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f),
                                            "Status: DualSense USB Audio Active (%d Hz, %d-ch) [Physical USB Audio]",
                                            audioSys.GetDeviceSampleRate(), audioSys.GetDeviceChannels());
                                    } else if (audioSys.IsCurrentDeviceBluetooth()) {
                                        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f),
                                            "Status: [BLUETOOTH AUDIO ACTIVE] -> %s (%d Hz, %d-ch)",
                                            audioSys.GetCurrentDeviceName().c_str(),
                                            audioSys.GetDeviceSampleRate(), audioSys.GetDeviceChannels());
                                    } else {
                                        ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f),
                                            "Status: Playing through: %s (%d Hz)",
                                            audioSys.GetCurrentDeviceName().c_str(), audioSys.GetDeviceSampleRate());
                                    }
                                } else {
                                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Status: Audio Device Closed");
                                }

                                // ---- Controller Connection / Audio-to-Haptics ----
                                if (dualSense.IsConnected()) {
                                    ImGui::Spacing();
                                    if (audioSys.IsDualSenseDevice()) {
                                        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f),
                                            "DualSense Speaker & Haptics Active (Physical USB 4-Channel)!");
                                    } else if (dualSense.GetConnectionType() == EngineInput::DualSenseConnection::Bluetooth) {
                                        ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f),
                                            "[Bluetooth Mode]: Native Bluetooth connection.");
                                        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f),
                                            "  -> Game audio: %s | Dual-Band Audio-to-Haptics active on gamepad.",
                                            audioSys.GetCurrentDeviceName().c_str());
                                    } else {
                                        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f),
                                            "[USB Connected]: Physical DualSense 4-Ch Speaker & Haptics Active.");
                                    }
                                }

                                // ---- Bluetooth Audio Settings ----
                                ImGui::Spacing();
                                ImGui::SeparatorText("Bluetooth Audio Settings");

                                bool btSim = audioSys.IsBluetoothSpeakerSimulation();
                                if (ImGui::Checkbox("DualSense Speaker Acoustic Resonance Filter (BT mode)", &btSim)) {
                                    audioSys.SetBluetoothSpeakerSimulation(btSim);
                                }
                                ImGui::TextDisabled("  Bandpass: 200Hz-6.5kHz + 3kHz presence boost — mimics DualSense internal speaker acoustics");

                                ImGui::Spacing();
                                int bufMs = audioSys.GetBluetoothBufferDurationMs();
                                ImGui::SetNextItemWidth(200);
                                if (ImGui::SliderInt("BT Buffer Duration (ms)##bufms", &bufMs, 50, 150)) {
                                    audioSys.SetBluetoothBufferDurationMs(bufMs);
                                }
                                ImGui::SameLine();
                                ImGui::TextDisabled("Higher = fewer dropouts, more latency");

                                // ---- Audio-to-Haptics Dual-Band ----
                                ImGui::Spacing();
                                ImGui::SeparatorText("Audio-to-Haptics Engine (Dual-Band)");

                                bool a2h = dualSense.IsAudioToHaptics();
                                if (ImGui::Checkbox("Enable Audio-to-Haptics (Vibrate to Sound)", &a2h)) {
                                    dualSense.SetAudioToHaptics(a2h);
                                }

                                if (a2h) {
                                    bool dualBand = dualSense.IsAudioToHapticsDualBand();
                                    if (ImGui::Checkbox("Dual-Band Mode (Bass->Left Motor, Treble->Right Motor)", &dualBand)) {
                                        dualSense.SetAudioToHapticsDualBand(dualBand);
                                    }

                                    float masterGain = dualSense.GetAudioToHapticsGain();
                                    ImGui::SetNextItemWidth(200);
                                    if (ImGui::SliderFloat("Master Haptic Gain##A2H", &masterGain, 0.2f, 3.0f, "%.1fx")) {
                                        dualSense.SetAudioToHapticsGain(masterGain);
                                    }

                                    if (dualBand) {
                                        float bassGain = dualSense.GetBassHapticsGain();
                                        ImGui::SetNextItemWidth(200);
                                        if (ImGui::SliderFloat("Bass Gain (Left Motor)##bassGain", &bassGain, 0.0f, 4.0f, "%.1fx")) {
                                            dualSense.SetBassHapticsGain(bassGain);
                                        }
                                        float trebleGain = dualSense.GetTrebleHapticsGain();
                                        ImGui::SetNextItemWidth(200);
                                        if (ImGui::SliderFloat("Treble Gain (Right Motor)##trebGain", &trebleGain, 0.0f, 4.0f, "%.1fx")) {
                                            dualSense.SetTrebleHapticsGain(trebleGain);
                                        }

                                        // Live energy meters
                                        float bassE = audioSys.GetBassAudioEnergy();
                                        float trebE = audioSys.GetTrebleAudioEnergy();
                                        char bassLabel[48], trebLabel[48];
                                        snprintf(bassLabel, sizeof(bassLabel), "Bass  %.0f%%", bassE * 100.0f);
                                        snprintf(trebLabel, sizeof(trebLabel), "Treble %.0f%%", trebE * 100.0f);
                                        ImGui::ProgressBar(bassE,   ImVec2(200, 10), bassLabel);
                                        ImGui::SameLine(); ImGui::TextDisabled("Bass -> Left Motor");
                                        ImGui::ProgressBar(trebE,   ImVec2(200, 10), trebLabel);
                                        ImGui::SameLine(); ImGui::TextDisabled("Treble -> Right Motor");
                                    } else {
                                        float audioE = audioSys.GetAudioEnergy();
                                        char eLabel[48];
                                        snprintf(eLabel, sizeof(eLabel), "Audio %.0f%%", audioE * 100.0f);
                                        ImGui::ProgressBar(audioE, ImVec2(200, 10), eLabel);
                                        ImGui::SameLine(); ImGui::TextDisabled("Live Audio Output Energy");
                                    }

                                    // BT sync delay slider — relevant for Bluetooth
                                    if (dualSense.GetConnectionType() == EngineInput::DualSenseConnection::Bluetooth) {
                                        float btDelay = dualSense.GetBluetoothAudioSyncDelay() * 1000.0f; // convert to ms
                                        ImGui::SetNextItemWidth(200);
                                        if (ImGui::SliderFloat("BT Haptic Sync Delay (ms)##btDelay", &btDelay, 0.0f, 150.0f, "%.0f ms")) {
                                            dualSense.SetBluetoothAudioSyncDelay(btDelay / 1000.0f);
                                        }
                                        ImGui::TextDisabled("  Offsets haptic pulses to sync with BT audio latency");
                                    }
                                }

                                // ---- Channel Routing & Volume ----
                                ImGui::Spacing();
                                ImGui::SeparatorText("Channel Routing & Volume");

                                const char* routeNames[] = {
                                    "Auto Routing (Speaker + Headset + Tactile Haptics)",
                                    "Hardware Speaker & Headphone Channels (Ch 1 & 2)",
                                    "Broadcast to All 4 Channels (Audio + Tactile Haptics in Sync)"
                                };
                                int currentRoute = static_cast<int>(audioSys.GetChannelRouting());
                                if (currentRoute > 2) currentRoute = 0;
                                if (ImGui::Combo("Speaker Routing", &currentRoute, routeNames, IM_ARRAYSIZE(routeNames))) {
                                    audioSys.SetChannelRouting(static_cast<EngineAudio::DualSenseChannelRouting>(currentRoute));
                                }

                                if (dualSense.IsConnected() && dualSense.GetPhysicalConnectionType() == EngineInput::DualSenseConnection::USB) {
                                    int hwRouting = (dualSense.GetAudioRouting() == 0x30) ? 0 : ((dualSense.GetAudioRouting() == 0x20) ? 1 : 2);
                                    const char* hwRouteNames[] = {
                                        "Internal Speaker Only (0x30 - Unmuted)",
                                        "Internal Speaker + Headphone (0x20 - Both Active)",
                                        "Headphones Only (0x00 - Speaker Muted)"
                                    };
                                    if (ImGui::Combo("Hardware Speaker Routing (USB)", &hwRouting, hwRouteNames, IM_ARRAYSIZE(hwRouteNames))) {
                                         uint8_t flags = (hwRouting == 0) ? 0x30 : ((hwRouting == 1) ? 0x20 : 0x00);
                                         dualSense.SetAudioRouting(flags);
                                    }
                                }

                                float masterVol = audioSys.GetMasterVolume();
                                if (ImGui::SliderFloat("Speaker Volume", &masterVol, 0.0f, 2.0f, "%.0f%%", ImGuiSliderFlags_None)) {
                                    audioSys.SetMasterVolume(masterVol);
                                }

                                bool gameplaySounds = audioSys.IsGameplaySoundsEnabled();
                                if (ImGui::Checkbox("Enable DualSense Gameplay Sound Effects", &gameplaySounds)) {
                                    audioSys.SetGameplaySoundsEnabled(gameplaySounds);
                                }
                                ImGui::TextDisabled("(Plays gunshots on R2, clicks on reload, rumble/impacts on controller)");

                                // ---- Soundboard ----
                                ImGui::Spacing();
                                ImGui::SeparatorText("Interactive Soundboard & Speaker Verification");

                                if (audioSys.IsPlayingSound()) {
                                    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "SOUND PLAYING THROUGH ACTIVE AUDIO DEVICE NOW!");
                                } else {
                                    if (dualSense.GetPhysicalConnectionType() == EngineInput::DualSenseConnection::Bluetooth) {
                                        ImGui::TextDisabled("Idle (Sound plays via active PC audio device + triggers gamepad haptic vibration):");
                                    } else {
                                        ImGui::TextDisabled("Idle (Click a button below to test physical DualSense speaker):");
                                    }
                                }

                                if (ImGui::Button("PLAY SPEAKER TEST MELODY (Fanfare Chimes)", ImVec2(320, 34))) {
                                    audioSys.PlayMelodyDemo();
                                    dualSense.PlayRumble(0.4f, 0.2f, 0.3f);
                                }
                                ImGui::SameLine();
                                if (ImGui::Button("Stop Sound", ImVec2(160, 34))) {
                                    audioSys.StopAllSounds();
                                }

                                ImGui::Spacing();
                                if (ImGui::Button("Shoot Projectile (Gunshot)", ImVec2(240, 30))) {
                                    audioSys.PlayShootSound(1.0f);
                                    dualSense.PlayRumble(0.7f, 0.3f, 0.1f);
                                }
                                ImGui::SameLine();
                                if (ImGui::Button("Mechanical Reload (Clicks)", ImVec2(240, 30))) {
                                    audioSys.PlayReloadSound(1.0f);
                                }

                                if (ImGui::Button("Heavy Impact / Explosion", ImVec2(240, 30))) {
                                    audioSys.PlayImpactSound(1.0f);
                                    dualSense.PlayRumble(0.9f, 0.5f, 0.25f);
                                }
                                ImGui::SameLine();
                                if (ImGui::Button("UI Chime Tone", ImVec2(240, 30))) {
                                    audioSys.PlayChimeSound(880.0f, 0.18f, 1.0f);
                                }

                                if (ImGui::Button("Raindrop Micro-Pop", ImVec2(240, 30))) {
                                    audioSys.PlayRainDropSound(0.9f);
                                }
                                ImGui::SameLine();
                                if (ImGui::Button("440 Hz Sine Beep", ImVec2(240, 30))) {
                                    audioSys.PlayTone(440.0f, 0.25f, 0.8f);
                                }

                                ImGui::Spacing();
                                ImGui::SeparatorText("Custom WAV Audio File Player");
                                static char wavPathBuf[256] = "Content/Audio/test.wav";
                                ImGui::InputText("WAV File Path", wavPathBuf, sizeof(wavPathBuf));

                                static float wavVolume = 1.0f;
                                ImGui::SliderFloat("WAV Volume", &wavVolume, 0.0f, 2.0f, "%.2f");

                                if (ImGui::Button("Play WAV on Controller", ImVec2(200, 28))) {
                                    audioSys.PlayWavFile(wavPathBuf, wavVolume, false);
                                }
                                ImGui::SameLine();
                                if (ImGui::Button("Play Looped", ImVec2(140, 28))) {
                                    audioSys.PlayWavFile(wavPathBuf, wavVolume, true);
                                }

                                ImGui::EndTabItem();
                            }


                            ImGui::EndTabBar();
                        }
                    }
                    ImGui::End();

                    // Platform Security & Online Studio
                    } // Controller tools
#if defined(ENGINE_PLATFORM_EPIC)
                    EngineEpic::EpicSystem::Get().DrawUI();
#elif defined(ENGINE_PLATFORM_STEAM)
                    EngineSteam::SteamSystem::Get().DrawUI();
#endif
                    // Discord Social Studio & Rich Presence
                    EngineDiscord::DiscordSystem::Get().DrawUI();

                    // Multi-Camera Studio & Controls
                    CameraSystem::Get().DrawUI(m_renderer.get(), m_camera.get(), (float)m_width, (float)m_height);

                    // =======================================================
                    //  DEBUG MENU WINDOWS & VIEWPORT OVERLAYS
                    // =======================================================
                    if (s_showImGuiMetrics) {
                        ImGui::ShowMetricsWindow(&s_showImGuiMetrics);
                    }
                    if (s_showImGuiStyleEditor) {
                        if (ImGui::Begin("Dear ImGui Style Editor", &s_showImGuiStyleEditor)) {
                            ImGui::ShowStyleEditor();
                        }
                        ImGui::End();
                    }

                    // 1. DX12 & Render Pipeline Inspector
                    if (s_showRenderDebugUI) {
                        ImGui::SetNextWindowSize(ImVec2(560, 520), ImGuiCond_FirstUseEver);
                        if (ImGui::Begin("DX12 & Render Pipeline Inspector", &s_showRenderDebugUI)) {
                            ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f), "DirectX 12 Hardware & Render Pipeline Diagnostics");
                            ImGui::Separator();

                            ImGui::Text("Display Resolution: %d x %d  |  Internal Render: %d x %d",
                                m_renderer->GetWidth(), m_renderer->GetHeight(),
                                m_renderer->GetRenderWidth(), m_renderer->GetRenderHeight());
                            ImGui::Text("CPU Frame Time: %.2f ms (%.1f FPS)  |  GPU Frame: %.2f ms  |  GPU Wait: %.2f ms",
                                dt * 1000.0f, m_fps, m_renderer->GetLastGpuFrameMs(), m_renderer->GetLastGpuWaitMs());

                            uint32_t totalStaticTris = 0;
                            for (const auto& m : models) if (m.visible) totalStaticTris += m.indexCount / 3;
                            uint32_t totalDynTris = 0;
                            for (const auto& dm : dynModels) if (dm.visible) totalDynTris += dm.indexCount / 3;
                            ImGui::Text("Static Models: %zu (%u tris)  |  Dynamic Models: %zu (%u tris)",
                                models.size(), totalStaticTris, dynModels.size(), totalDynTris);
                            ImGui::Text("Active Lights: Point %zu / %d  |  Spot %zu / %d",
                                m_renderer->GetPointLights().size(), (int)EngineRender::MAX_POINT_LIGHTS,
                                m_renderer->GetSpotLights().size(), (int)EngineRender::MAX_SPOT_LIGHTS);

                            ImGui::SeparatorText("Graphics & Upscaler Presets");
                            EngineUI::DrawAntiAliasingControls(*m_renderer);

                            ImGui::SeparatorText("Pipeline Passes & Feature Toggles");
                            bool vsync = m_renderer->IsVSyncEnabled();
                            if (ImGui::Checkbox("V-Sync", &vsync)) m_renderer->SetVSyncEnabled(vsync);
                            ImGui::SameLine();
                            bool ssao = m_renderer->IsSSAOEnabled();
                            if (ImGui::Checkbox("SSAO", &ssao)) m_renderer->SetSSAOEnabled(ssao);
                            ImGui::SameLine();
                            bool ssgi = m_renderer->IsSSGIEnabled();
                            if (ImGui::Checkbox("SSGI", &ssgi)) m_renderer->SetSSGIEnabled(ssgi);

                            bool vsm = m_renderer->IsVSMEnabled();
                            if (ImGui::Checkbox("Directional Shadow Map (2048)", &vsm)) m_renderer->SetVSMEnabled(vsm);
                            ImGui::SameLine();
                            bool fog = m_renderer->IsVolumetricFogEnabled();
                            if (ImGui::Checkbox("Volumetric Fog", &fog)) m_renderer->SetVolumetricFogEnabled(fog);

#if defined(_WIN32)
                            ImGui::SeparatorText("DXR 1.1 Ray Tracing & Upscaling State");
                            ImGui::Text("DXR Device Status: %s", m_renderer->m_dxrDevice ? "READY (DirectX Raytracing 1.1)" : "UNAVAILABLE");
                            if (ImGui::Checkbox("Enable DXR Ray Tracing", &m_renderer->m_dxrEnabled)) {
                                m_renderer->InvalidateRaytracingScene();
                            }
                            if (m_renderer->m_dxrEnabled) {
                                ImGui::SameLine();
                                ImGui::Checkbox("RT Reflections", &m_renderer->m_rtReflectionsEnabled);
                                ImGui::SameLine();
                                ImGui::Checkbox("RT Shadows", &m_renderer->m_rtShadowsEnabled);
                                bool pt = m_renderer->IsPathTracingEnabled();
                                if (ImGui::Checkbox("Full Path Tracing GI", &pt)) m_renderer->SetPathTracingEnabled(pt);
                                ImGui::SameLine();
                                if (ImGui::SmallButton("Rebuild TLAS/BVH")) m_renderer->InvalidateRaytracingScene();
                            }
                            const char* upNames[] = { "None (Native)", "NVIDIA DLSS", "AMD FSR Temporal", "Intel XeSS 2.0" };
                            UpscaleTechnique curUp = m_renderer->GetUpscaleTechnique();
                            int upIdx = (curUp == UpscaleTechnique::XeSS) ? 3 : static_cast<int>(curUp);
                            if (upIdx < 0 || upIdx > 3) upIdx = 0;
                            if (ImGui::Combo("Active Upscaler", &upIdx, upNames, 4)) {
                                UpscaleTechnique nextUp = (upIdx == 3) ? UpscaleTechnique::XeSS : static_cast<UpscaleTechnique>(upIdx);
                                m_renderer->SetUpscaleTechnique(nextUp);
                            }
                            ImGui::Text("DLSS Supported: %s (Active: %s) | DLSS-G: %s | XeSS: %s",
                                m_renderer->IsDLSSSupported() ? "Yes" : "No",
                                m_renderer->IsDLSSActive() ? "Yes" : "No",
                                m_renderer->IsDLSSGActive() ? "ON" : "OFF",
                                m_renderer->IsXeSSSupported() ? "Yes" : "No");
#endif

                            ImGui::SeparatorText("LOD & Culling Inspector");
                            ImGui::Text("LOD0: %u  |  LOD1: %u  |  LOD2: %u  |  Distance Culled: %u",
                                m_renderer->GetLOD0Count(), m_renderer->GetLOD1Count(),
                                m_renderer->GetLOD2Count(), m_renderer->GetLODCulledCount());
                            bool lodColor = m_renderer->IsDebugLODColor();
                            if (ImGui::Checkbox("LOD Debug Color Tinting", &lodColor)) m_renderer->SetDebugLODColor(lodColor);
                            ImGui::SameLine();
                            bool lodCull = m_renderer->IsLODCullingEnabled();
                            if (ImGui::Checkbox("Distance Culling", &lodCull)) m_renderer->SetLODCullingEnabled(lodCull);
                            int forcedLod = m_renderer->GetForcedLOD() + 1;
                            const char* lodModes[] = { "Auto Distance", "Force LOD 0", "Force LOD 1", "Force LOD 2" };
                            if (ImGui::Combo("Force LOD", &forcedLod, lodModes, 4)) m_renderer->SetForcedLOD(forcedLod - 1);
                        }
                        ImGui::End();
                    }

                    // 2. Memory, VRAM & Streaming Debug
                    if (s_showMemoryDebugUI) {
                        ImGui::SetNextWindowSize(ImVec2(540, 460), ImGuiCond_FirstUseEver);
                        if (ImGui::Begin("Memory, VRAM & Streaming Debug", &s_showMemoryDebugUI)) {
                            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), "System RAM, Process Working Set & Asset Streaming");
                            ImGui::Separator();
#if defined(_WIN32)
                            PROCESS_MEMORY_COUNTERS pmc = {};
                            if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
                                ImGui::BulletText("Working Set (RAM): %.2f MB (Peak: %.2f MB)",
                                    pmc.WorkingSetSize / (1024.0 * 1024.0), pmc.PeakWorkingSetSize / (1024.0 * 1024.0));
                                ImGui::BulletText("Commit / Pagefile Usage: %.2f MB (Peak: %.2f MB)",
                                    pmc.PagefileUsage / (1024.0 * 1024.0), pmc.PeakPagefileUsage / (1024.0 * 1024.0));
                                ImGui::BulletText("Page Fault Count: %lu", (unsigned long)pmc.PageFaultCount);
                            }
                            if (m_renderer->GetDevice()) {
                                LUID luid = m_renderer->GetDevice()->GetAdapterLuid();
                                ComPtr<IDXGIFactory4> factory;
                                if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
                                    ComPtr<IDXGIAdapter3> adapter3;
                                    if (SUCCEEDED(factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter3)))) {
                                        DXGI_ADAPTER_DESC2 desc = {};
                                        adapter3->GetDesc2(&desc);
                                        DXGI_QUERY_VIDEO_MEMORY_INFO localMem = {}, nonLocalMem = {};
                                        adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &localMem);
                                        adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonLocalMem);
                                        ImGui::SeparatorText("DXGI GPU Video Memory (VRAM)");
                                        char gpuName[128] = {};
                                        WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, gpuName, sizeof(gpuName), nullptr, nullptr);
                                        ImGui::Text("GPU Adapter: %s", gpuName);
                                        float usedMB = localMem.CurrentUsage / (1024.0f * 1024.0f);
                                        float budgetMB = localMem.Budget / (1024.0f * 1024.0f);
                                        char vramOverlay[64];
                                        snprintf(vramOverlay, sizeof(vramOverlay), "VRAM: %.1f MB / %.1f MB", usedMB, budgetMB);
                                        ImGui::ProgressBar(budgetMB > 0.0f ? (usedMB / budgetMB) : 0.0f, ImVec2(-1, 20), vramOverlay);
                                        ImGui::Text("Shared System GPU Memory: %.1f MB / %.1f MB",
                                            nonLocalMem.CurrentUsage / (1024.0f * 1024.0f), nonLocalMem.Budget / (1024.0f * 1024.0f));
                                    }
                                }
                            }
                            ImGui::SeparatorText("DirectStorage & Async Texture Streamer");
                            if (m_renderer->GetDirectStorage()) {
                                ImGui::BulletText("DirectStorage Status: %s", m_renderer->GetDirectStorage()->GetStatusSummary().c_str());
                                ImGui::BulletText("GPU GDeflate Decompression: %s | BypassIO: %s",
                                    m_renderer->GetDirectStorage()->HasGpuDecompression() ? "YES" : "NO",
                                    m_renderer->GetDirectStorage()->HasBypassIO() ? "VERIFIED" : "NOT VERIFIED");
                            }
#endif
                            ImGui::BulletText("Async Texture Streamer: %s", m_renderer->GetTextureStreamer() ? "Active (DX12 Copy Queue)" : "Inactive");
                            ImGui::BulletText("Registered Assets in Registry: %zu", EngineAsset::AssetRegistry::Get().GetAssetCount());
                            if (ImGui::Button("Open Asset Registry")) {
                                EngineAsset::AssetRegistry::Get().SetUIOpen(true);
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Open Content Browser")) {
                                EngineUI::ContentBrowser::Get().SetOpen(true);
                            }
                        }
                        ImGui::End();
                    }

                    // 3. Time Scale & Simulation Control
                    if (s_showTimeSimDebugUI) {
                        ImGui::SetNextWindowSize(ImVec2(460, 360), ImGuiCond_FirstUseEver);
                        if (ImGui::Begin("Time Scale & Simulation Control", &s_showTimeSimDebugUI)) {
                            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Global Engine Clock & Physics Subsystems");
                            ImGui::Separator();

                            float timeScale = EngineDebug::DebugConsole::Get().GetTimeScale();
                            if (ImGui::SliderFloat("Global Time Scale", &timeScale, 0.0f, 4.0f, "%.2fx")) {
                                EngineDebug::DebugConsole::Get().SetTimeScale(timeScale);
                            }
                            if (ImGui::Button("Pause (0.0x)")) EngineDebug::DebugConsole::Get().SetTimeScale(0.0f);
                            ImGui::SameLine();
                            if (ImGui::Button("Slow-Mo (0.25x)")) EngineDebug::DebugConsole::Get().SetTimeScale(0.25f);
                            ImGui::SameLine();
                            if (ImGui::Button("Half (0.5x)")) EngineDebug::DebugConsole::Get().SetTimeScale(0.5f);
                            ImGui::SameLine();
                            if (ImGui::Button("Normal (1.0x)")) EngineDebug::DebugConsole::Get().SetTimeScale(1.0f);
                            ImGui::SameLine();
                            if (ImGui::Button("Fast (2.0x)")) EngineDebug::DebugConsole::Get().SetTimeScale(2.0f);

                            ImGui::SeparatorText("PhysX Rigidbodies");
                            auto& phys = EnginePhysics::PhysicsSystem::Get();
                            bool physPaused = phys.IsPaused();
                            if (ImGui::Checkbox("Pause PhysX Simulation", &physPaused)) phys.SetPaused(physPaused);
                            ImGui::SameLine();
                            if (ImGui::Button("Single Step PhysX")) phys.SingleStep();
                            ImGui::SameLine();
                            if (ImGui::Button("Reset All Actors")) phys.ResetSimulation(m_renderer.get());

                            XMFLOAT3 grav = phys.GetGravity();
                            if (ImGui::DragFloat3("Gravity (m/s^2)", &grav.x, 0.2f, -50.0f, 50.0f)) {
                                phys.SetGravity(grav);
                            }
                            if (ImGui::SmallButton("Earth (-9.81)")) phys.SetGravity({0.0f, -9.81f, 0.0f});
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Moon (-1.62)")) phys.SetGravity({0.0f, -1.62f, 0.0f});
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Zero-G (0.0)")) phys.SetGravity({0.0f, 0.0f, 0.0f});

                            ImGui::SeparatorText("XPBD Cloth & Hair Solvers");
                            bool clothPaused = EnginePhysics::ClothSystem::Get().IsPaused();
                            if (ImGui::Checkbox("Pause Cloth", &clothPaused)) EnginePhysics::ClothSystem::Get().SetPaused(clothPaused);
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Step Cloth")) EnginePhysics::ClothSystem::Get().SingleStep();
                            ImGui::SameLine();
                            bool hairPaused = EnginePhysics::HairSystem::Get().IsPaused();
                            if (ImGui::Checkbox("Pause Hair", &hairPaused)) EnginePhysics::HairSystem::Get().SetPaused(hairPaused);
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Step Hair")) EnginePhysics::HairSystem::Get().SingleStep();
                        }
                        ImGui::End();
                    }

                    // 4. Camera, Input & Raycast Inspector
                    if (s_showInputDebugUI) {
                        ImGui::SetNextWindowSize(ImVec2(500, 440), ImGuiCond_FirstUseEver);
                        if (ImGui::Begin("Camera, Input & Raycast Inspector", &s_showInputDebugUI)) {
                            ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "Active Camera Telemetry");
                            XMFLOAT3 camPos = m_camera->GetPosition();
                            XMFLOAT3 camFwd = m_camera->GetForwardVector();
                            ImGui::Text("Position: (%.2f, %.2f, %.2f)  |  Mode: %s",
                                camPos.x, camPos.y, camPos.z, m_camera->IsFreecamMode() ? "Freecam (Editor)" : "FPS Character");
                            ImGui::Text("Forward:  (%.2f, %.2f, %.2f)  |  Yaw: %.1f deg | Pitch: %.1f deg",
                                camFwd.x, camFwd.y, camFwd.z, XMConvertToDegrees(m_camera->GetYaw()), XMConvertToDegrees(m_camera->GetPitch()));
                            if (ImGui::Button("Teleport to Origin (0, 2, -5)")) {
                                m_camera->SetPosition(0.0f, 2.0f, -5.0f);
                            }
                            ImGui::SameLine();
                            bool freecam = m_camera->IsFreecamMode();
                            if (ImGui::Checkbox("Freecam Mode", &freecam)) m_camera->SetFreecamMode(freecam);

                            ImGui::SeparatorText("Live Camera Forward Raycast (PhysX)");
                            EnginePhysics::RaycastHitInfo hitInfo;
                            if (EnginePhysics::PhysicsSystem::Get().Raycast(camPos, camFwd, 500.0f, hitInfo) && hitInfo.hasHit) {
                                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "HIT DETECTED at %.2f m", hitInfo.distance);
                                ImGui::Text("Hit Point:  (%.2f, %.2f, %.2f)", hitInfo.position.x, hitInfo.position.y, hitInfo.position.z);
                                ImGui::Text("Hit Normal: (%.2f, %.2f, %.2f)", hitInfo.normal.x, hitInfo.normal.y, hitInfo.normal.z);
                                ImGui::Text("Model ID:   #%u (%s)", hitInfo.ownerModelId, hitInfo.isDynamicModel ? "Dynamic" : "Static");
                            } else {
                                ImGui::TextDisabled("No PhysX collider hit along camera forward ray (500m)");
                            }

                            ImGui::SeparatorText("Mouse, Keyboard & Controller State");
                            ImVec2 mPos = ImGui::GetMousePos();
                            XMFLOAT2 mDelta = EngineInput::InputMapping::Get().GetFrameMouseDelta();
                            ImGui::Text("Mouse Cursor: (%.0f, %.0f) | Raw Delta: (%.1f, %.1f)", mPos.x, mPos.y, mDelta.x, mDelta.y);
                            ImGui::Text("ImGui Capture: Mouse=%s | Keyboard=%s",
                                ImGui::GetIO().WantCaptureMouse ? "YES" : "NO",
                                ImGui::GetIO().WantCaptureKeyboard ? "YES" : "NO");
                            ImGui::Text("DualSense (PS5): %s | DualShock 4 (PS4): %s",
                                dualSense.IsConnected() ? "CONNECTED" : "Disconnected",
                                dualShock.IsConnected() ? "CONNECTED" : "Disconnected");
                        }
                        ImGui::End();
                    }

                    // 5. Security & DLL Integrity Audit
                    if (s_showSecurityDebugUI) {
                        ImGui::SetNextWindowSize(ImVec2(580, 440), ImGuiCond_FirstUseEver);
                        if (ImGui::Begin("Security & DLL Integrity Audit", &s_showSecurityDebugUI)) {
                            auto& sec = EngineSecurity::SecuritySystem::Get();
                            const auto& rep = sec.GetLastReport();
                            ImGui::TextColored(ImVec4(0.3f, 0.9f, 1.0f, 1.0f), "Engine Anti-Tamper, Code CRC & ThirdParty DLL Verification");
                            ImGui::Separator();

                            ImGui::Text("Background Watchdog Thread: ");
                            ImGui::SameLine();
                            if (sec.IsWatchdogRunning()) {
                                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "ACTIVE");
                            } else {
                                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "STANDBY (Editor Mode)");
                            }

                            ImGui::Text("Last Integrity Audit Status: ");
                            ImGui::SameLine();
                            if (rep.passed) {
                                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "PASSED (%s)", rep.summary.empty() ? "All checks clean" : rep.summary.c_str());
                            } else {
                                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "VIOLATIONS DETECTED (%zu)", rep.violations.size());
                            }

                            if (ImGui::Button("Run On-Demand Full Integrity Audit Now")) {
                                EngineSecurity::SecurityReport liveReport;
                                bool ok = sec.PerformIntegrityCheck(liveReport);
                                if (ok) {
                                    EngineDebug::DebugConsole::Get().LogSuccess("[Security] On-demand integrity audit PASSED.");
                                } else {
                                    EngineDebug::DebugConsole::Get().LogError("[Security] Integrity audit reported violations: " + liveReport.summary);
                                }
                            }

                            ImGui::SeparatorText("Verified Protection Layers");
                            ImGui::BulletText("Manifest & DLL Location: ThirdParty/* (Signed SHA-256 in content01.pak)");
                            ImGui::BulletText("Executable .text Section Block-wise CRC32 Verification");
                            ImGui::BulletText("Win32 / NTAPI Hook & Root DLL Hijack Scanner");
                            ImGui::BulletText("Hardware Breakpoint & Debugger Detection");

                            if (!rep.violations.empty()) {
                                ImGui::SeparatorText("Recorded Violations");
                                for (const auto& v : rep.violations) {
                                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "- %s", v.c_str());
                                }
                            }
                        }
                        ImGui::End();
                    }

                    // 6. Scene & Entity Batch Inspector
                    if (s_showSceneEntityDebugUI) {
                        ImGui::SetNextWindowSize(ImVec2(760, 520), ImGuiCond_FirstUseEver);
                        if (ImGui::Begin("Scene & Entity Batch Inspector", &s_showSceneEntityDebugUI)) {
                            ImGui::TextColored(ImVec4(0.35f, 0.9f, 1.0f, 1.0f), "Live Scene Outliner, Batch Operations & Entity Table");
                            ImGui::Separator();

                            if (ImGui::Button("Show All Models")) {
                                for (auto& m : models) m.visible = true;
                                for (auto& dm : dynModels) dm.visible = true;
                                m_renderer->InvalidateRaytracingScene();
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Melt All Objects")) {
                                EngineDebug::DebugConsole::Get().Execute("melt all 0.8");
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Restore / Solidify All")) {
                                EngineDebug::DebugConsole::Get().Execute("melt reset");
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Randomize PBR Colors")) {
                                for (size_t i = 0; i < models.size(); ++i) {
                                    if (models[i].name == "Floor") continue;
                                    models[i].material.albedo = {
                                        0.2f + 0.8f * ((i * 73) % 100) / 100.0f,
                                        0.2f + 0.8f * ((i * 41 + 29) % 100) / 100.0f,
                                        0.2f + 0.8f * ((i * 19 + 61) % 100) / 100.0f
                                    };
                                }
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Make All Gold Mirror")) {
                                for (auto& m : models) {
                                    if (m.name == "Floor") continue;
                                    m.material.ApplyPreset(EngineRender::MaterialPreset::Gold);
                                }
                            }

                            static char s_entityFilter[128] = "";
                            ImGui::SetNextItemWidth(240.0f);
                            ImGui::InputTextWithHint("##EntityFilter", "Filter models by name...", s_entityFilter, sizeof(s_entityFilter));
                            ImGui::SameLine();
                            ImGui::Text("Total: %zu Static, %zu Dynamic, %zu Point Lights, %zu Spot Lights",
                                models.size(), dynModels.size(), m_renderer->GetPointLights().size(), m_renderer->GetSpotLights().size());

                            if (ImGui::BeginTable("EntityDebugTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY, ImVec2(0, 360))) {
                                ImGui::TableSetupColumn("ID / Type", ImGuiTableColumnFlags_WidthFixed, 95.0f);
                                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                                ImGui::TableSetupColumn("Position (X, Y, Z)", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                                ImGui::TableSetupColumn("Triangles", ImGuiTableColumnFlags_WidthFixed, 75.0f);
                                ImGui::TableSetupColumn("Vis", ImGuiTableColumnFlags_WidthFixed, 40.0f);
                                ImGui::TableSetupColumn("Quick Actions", ImGuiTableColumnFlags_WidthFixed, 210.0f);
                                ImGui::TableHeadersRow();

                                std::string filt(s_entityFilter);
                                std::transform(filt.begin(), filt.end(), filt.begin(), ::tolower);

                                int deleteStaticIdx = -1;
                                for (size_t i = 0; i < models.size(); ++i) {
                                    auto& m = models[i];
                                    if (!filt.empty()) {
                                        std::string lowName = m.name;
                                        std::transform(lowName.begin(), lowName.end(), lowName.begin(), ::tolower);
                                        if (lowName.find(filt) == std::string::npos) continue;
                                    }
                                    ImGui::TableNextRow();
                                    ImGui::PushID(static_cast<int>(m.modelId + 10000));
                                    ImGui::TableSetColumnIndex(0);
                                    ImGui::Text("#%u [Static]", m.modelId);
                                    ImGui::TableSetColumnIndex(1);
                                    ImGui::TextUnformatted(m.name.c_str());
                                    ImGui::TableSetColumnIndex(2);
                                    ImGui::Text("(%.1f, %.1f, %.1f)", m.position.x, m.position.y, m.position.z);
                                    ImGui::TableSetColumnIndex(3);
                                    ImGui::Text("%u", m.indexCount / 3);
                                    ImGui::TableSetColumnIndex(4);
                                    if (ImGui::Checkbox("##v", &m.visible)) m_renderer->InvalidateRaytracingScene();
                                    ImGui::TableSetColumnIndex(5);
                                    if (ImGui::SmallButton("Select")) { s_selectedModelType = 1; s_selectedModelIndex = static_cast<int>(i); }
                                    ImGui::SameLine();
                                    if (ImGui::SmallButton("TP")) {
                                        m_camera->SetPosition(m.position.x, m.position.y + 2.5f, m.position.z - 3.5f);
                                    }
                                    ImGui::SameLine();
                                    if (ImGui::SmallButton("Melt")) { m.StartMelting(); m_renderer->InvalidateRaytracingScene(); }
                                    if (m.name != "Floor") {
                                        ImGui::SameLine();
                                        if (ImGui::SmallButton("Del")) deleteStaticIdx = static_cast<int>(i);
                                    }
                                    ImGui::PopID();
                                }
                                if (deleteStaticIdx >= 0 && deleteStaticIdx < static_cast<int>(models.size())) {
                                    EnginePhysics::PhysicsSystem::Get().RemoveActorForModel(models[deleteStaticIdx].modelId);
                                    models.erase(models.begin() + deleteStaticIdx);
                                    m_renderer->InvalidateRaytracingScene();
                                }

                                int deleteDynIdx = -1;
                                for (size_t i = 0; i < dynModels.size(); ++i) {
                                    auto& dm = dynModels[i];
                                    if (!filt.empty()) {
                                        std::string lowName = dm.name;
                                        std::transform(lowName.begin(), lowName.end(), lowName.begin(), ::tolower);
                                        if (lowName.find(filt) == std::string::npos) continue;
                                    }
                                    ImGui::TableNextRow();
                                    ImGui::PushID(static_cast<int>(dm.modelId + 50000));
                                    ImGui::TableSetColumnIndex(0);
                                    ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "#%u [Dyn]", dm.modelId);
                                    ImGui::TableSetColumnIndex(1);
                                    ImGui::TextUnformatted(dm.name.c_str());
                                    ImGui::TableSetColumnIndex(2);
                                    ImGui::Text("(%.1f, %.1f, %.1f)", dm.position.x, dm.position.y, dm.position.z);
                                    ImGui::TableSetColumnIndex(3);
                                    ImGui::Text("%u", dm.indexCount / 3);
                                    ImGui::TableSetColumnIndex(4);
                                    if (ImGui::Checkbox("##v", &dm.visible)) m_renderer->InvalidateRaytracingScene();
                                    ImGui::TableSetColumnIndex(5);
                                    if (ImGui::SmallButton("Select")) { s_selectedModelType = 2; s_selectedModelIndex = static_cast<int>(i); }
                                    ImGui::SameLine();
                                    if (ImGui::SmallButton("TP")) {
                                        m_camera->SetPosition(dm.position.x, dm.position.y + 2.5f, dm.position.z - 3.5f);
                                    }
                                    ImGui::SameLine();
                                    if (ImGui::SmallButton("Melt")) { dm.StartMelting(); m_renderer->InvalidateRaytracingScene(); }
                                    ImGui::SameLine();
                                    if (ImGui::SmallButton("Del")) deleteDynIdx = static_cast<int>(i);
                                    ImGui::PopID();
                                }
                                if (deleteDynIdx >= 0 && deleteDynIdx < static_cast<int>(dynModels.size())) {
                                    EnginePhysics::PhysicsSystem::Get().RemoveActorForModel(dynModels[deleteDynIdx].modelId);
                                    dynModels.erase(dynModels.begin() + deleteDynIdx);
                                    m_renderer->InvalidateRaytracingScene();
                                }
                                ImGui::EndTable();
                            }
                        }
                        ImGui::End();
                    }

                    // 7. Physics, Force Field & Destruction Sandbox
                    if (s_showPhysicsSandboxDebugUI) {
                        ImGui::SetNextWindowSize(ImVec2(540, 480), ImGuiCond_FirstUseEver);
                        if (ImGui::Begin("Physics, Force Field & Destruction Sandbox", &s_showPhysicsSandboxDebugUI)) {
                            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "PhysX 5.5 Interactive Destruction, Projectiles & Force Fields");
                            ImGui::Separator();

                            auto& phys = EnginePhysics::PhysicsSystem::Get();
                            ImGui::SeparatorText("Projectile Cannon");
                            static float s_projSpeed = 42.0f;
                            static float s_projMass = 25.0f;
                            static int s_projBurst = 1;
                            ImGui::SliderFloat("Muzzle Speed (m/s)", &s_projSpeed, 10.0f, 140.0f, "%.1f");
                            ImGui::SliderFloat("Projectile Mass (kg)", &s_projMass, 1.0f, 200.0f, "%.1f");
                            ImGui::SliderInt("Burst Count", &s_projBurst, 1, 15);
                            if (ImGui::Button("Fire Projectile Burst from Camera", ImVec2(-1, 30))) {
                                XMFLOAT3 pos = m_camera->GetPosition();
                                XMFLOAT3 fwd = m_camera->GetForwardVector();
                                for (int i = 0; i < s_projBurst; ++i) {
                                    XMFLOAT3 origin = { pos.x + fwd.x * (1.2f + i * 0.7f), pos.y + fwd.y * (1.2f + i * 0.7f), pos.z + fwd.z * (1.2f + i * 0.7f) };
                                    phys.ShootProjectile(m_renderer.get(), origin, fwd, s_projSpeed, s_projMass, 0.35f);
                                }
                            }

                            ImGui::SeparatorText("Radial Shockwave & Black Hole Implosion");
                            static float s_blastForce = 2800.0f;
                            static float s_blastRadius = 18.0f;
                            ImGui::SliderFloat("Shockwave Force", &s_blastForce, -5000.0f, 8000.0f, "%.0f");
                            ImGui::SliderFloat("Shockwave Radius (m)", &s_blastRadius, 3.0f, 50.0f, "%.1f");
                            if (ImGui::Button("Detonate Shockwave Ahead (5m)", ImVec2(245, 28))) {
                                XMFLOAT3 p = m_camera->GetPosition(), f = m_camera->GetForwardVector();
                                XMFLOAT3 c = { p.x + f.x * 5.0f, p.y + f.y * 5.0f, p.z + f.z * 5.0f };
                                phys.AddExplosion(c, s_blastRadius, s_blastForce, 0.35f);
                                EngineVFX::ParticleSystem::Get().SpawnExplosion(c);
                                EngineAudio::AudioSystem::Get().PlayProceduralExplosion(0.75f);
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Black Hole Implosion (Pull)", ImVec2(245, 28))) {
                                XMFLOAT3 p = m_camera->GetPosition(), f = m_camera->GetForwardVector();
                                XMFLOAT3 c = { p.x + f.x * 6.0f, p.y + f.y * 6.0f, p.z + f.z * 6.0f };
                                phys.AddExplosion(c, s_blastRadius * 1.4f, -std::abs(s_blastForce), -0.1f);
                                EngineVFX::ParticleSystem::Get().SpawnMagicEnergy(c);
                            }

                            ImGui::SeparatorText("Destructible Brick Tower Generator");
                            static int s_tw = 3, s_th = 6, s_td = 3;
                            ImGui::SliderInt("Tower Width", &s_tw, 1, 6);
                            ImGui::SliderInt("Tower Height", &s_th, 1, 12);
                            ImGui::SliderInt("Tower Depth", &s_td, 1, 6);
                            if (ImGui::Button("Spawn PhysX Brick Tower Ahead (8m)", ImVec2(-1, 28))) {
                                XMFLOAT3 p = m_camera->GetPosition(), f = m_camera->GetForwardVector();
                                phys.SpawnTower(m_renderer.get(), { p.x + f.x * 8.0f, 0.0f, p.z + f.z * 8.0f }, s_tw, s_th, s_td, 0.8f);
                                m_renderer->InvalidateRaytracingScene();
                            }
                        }
                        ImGui::End();
                    }

                    // 8. Atmosphere, Time-of-Day & Weather Tweaker
                    if (s_showAtmosphereDebugUI) {
                        ImGui::SetNextWindowSize(ImVec2(540, 470), ImGuiCond_FirstUseEver);
                        if (ImGui::Begin("Atmosphere, Time-of-Day & Weather Tweaker", &s_showAtmosphereDebugUI)) {
                            ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "Real-Time Sun, Sky, Clouds, Weather & Post-Process Presets");
                            ImGui::Separator();

                            ImGui::SeparatorText("One-Click Time-of-Day Presets");
                            const char* todPresets[] = { "dawn", "morning", "noon", "golden", "sunset", "dusk", "night", "bloodmoon" };
                            for (int i = 0; i < 8; ++i) {
                                if (i > 0 && i != 4) ImGui::SameLine();
                                if (ImGui::Button(todPresets[i], ImVec2(115, 26))) {
                                    EngineDebug::DebugConsole::Get().Execute(std::string("tod ") + todPresets[i]);
                                }
                            }

                            ImGui::SeparatorText("Sun Angles & Sky Colors");
                            float az = m_renderer->GetSunAzimuth();
                            float el = m_renderer->GetSunElevation();
                            if (ImGui::SliderFloat("Sun Azimuth (deg)", &az, 0.0f, 360.0f, "%.1f")) m_renderer->SetSunAzimuth(az);
                            if (ImGui::SliderFloat("Sun Elevation (deg)", &el, -10.0f, 90.0f, "%.1f")) m_renderer->SetSunElevation(el);
                            XMFLOAT3 skyCol = m_renderer->GetSkyColor();
                            if (ImGui::ColorEdit3("Sky Color", &skyCol.x)) m_renderer->SetSkyColor(skyCol);
                            XMFLOAT3 sunCol = m_renderer->GetLightColor();
                            if (ImGui::ColorEdit3("Sunlight Color", &sunCol.x)) m_renderer->SetLightColor(sunCol);
                            float clouds = m_renderer->GetCloudDensity();
                            if (ImGui::SliderFloat("Volumetric Clouds", &clouds, 0.0f, 3.0f, "%.2f")) m_renderer->SetCloudDensity(clouds);

                            ImGui::SeparatorText("Weather & Rain Presets");
                            if (ImGui::Button("Clear Sky")) EngineDebug::DebugConsole::Get().Execute("weather clear");
                            ImGui::SameLine();
                            if (ImGui::Button("Light Rain")) EngineDebug::DebugConsole::Get().Execute("weather light");
                            ImGui::SameLine();
                            if (ImGui::Button("Normal Rain")) EngineDebug::DebugConsole::Get().Execute("weather rain");
                            ImGui::SameLine();
                            if (ImGui::Button("Heavy Rain")) EngineDebug::DebugConsole::Get().Execute("weather heavy");
                            ImGui::SameLine();
                            if (ImGui::Button("Thunderstorm")) EngineDebug::DebugConsole::Get().Execute("weather storm");

                            ImGui::SeparatorText("Post-Processing Color Grading Presets");
                            if (ImGui::Button("Cinematic")) EngineDebug::DebugConsole::Get().Execute("pp cinematic");
                            ImGui::SameLine();
                            if (ImGui::Button("Cyberpunk")) EngineDebug::DebugConsole::Get().Execute("pp cyberpunk");
                            ImGui::SameLine();
                            if (ImGui::Button("Horror")) EngineDebug::DebugConsole::Get().Execute("pp horror");
                            ImGui::SameLine();
                            if (ImGui::Button("Retro VHS")) EngineDebug::DebugConsole::Get().Execute("pp retro");
                            ImGui::SameLine();
                            if (ImGui::Button("Clean Neutral")) EngineDebug::DebugConsole::Get().Execute("pp neutral");
                        }
                        ImGui::End();
                    }

                    // 9. Audio, VFX, AI & Lua Subsystem Debugger
                    if (s_showSubsystemsDebugUI) {
                        ImGui::SetNextWindowSize(ImVec2(550, 460), ImGuiCond_FirstUseEver);
                        if (ImGui::Begin("Audio, VFX, AI & Lua Subsystem Debugger", &s_showSubsystemsDebugUI)) {
                            ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.65f, 1.0f), "Engine Subsystems Live Control & Diagnostics");
                            ImGui::Separator();

                            ImGui::SeparatorText("Particle / VFX Spawner (4m Ahead of Camera)");
                            if (ImGui::Button("Spawn Explosion")) EngineDebug::DebugConsole::Get().Execute("vfx explosion");
                            ImGui::SameLine();
                            if (ImGui::Button("Spawn Fire")) EngineDebug::DebugConsole::Get().Execute("vfx fire");
                            ImGui::SameLine();
                            if (ImGui::Button("Spawn Sparks")) EngineDebug::DebugConsole::Get().Execute("vfx sparks");
                            ImGui::SameLine();
                            if (ImGui::Button("Spawn Smoke")) EngineDebug::DebugConsole::Get().Execute("vfx smoke");
                            ImGui::SameLine();
                            if (ImGui::Button("Magic Energy")) EngineDebug::DebugConsole::Get().Execute("vfx magic");
                            ImGui::Text("Active Particles: %u  |  ", EngineDebug::Profiler::Get().GetGpuTelemetry().activeParticles);
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Clear All VFX Emitters")) EngineDebug::DebugConsole::Get().Execute("vfx clear");

                            ImGui::SeparatorText("3D Spatial Audio & Procedural Synthesizer");
                            auto& aud = EngineAudio::AudioSystem::Get();
                            float masterVol = aud.GetMasterVolume();
                            if (ImGui::SliderFloat("Master Volume", &masterVol, 0.0f, 2.0f, "%.2f")) aud.SetMasterVolume(masterVol);
                            if (ImGui::Button("Synth Beep (520Hz)")) aud.PlayProceduralBeep(520.0f, 0.22f, 0.6f);
                            ImGui::SameLine();
                            if (ImGui::Button("Synth Coin")) aud.PlayProceduralCoin(0.65f);
                            ImGui::SameLine();
                            if (ImGui::Button("Synth Boom")) aud.PlayProceduralExplosion(0.75f);
                            ImGui::SameLine();
                            if (ImGui::Button("Stop All Audio")) aud.StopAll();

                            ImGui::SeparatorText("Lua 5.4 Live Expression Evaluator");
                            static char s_luaSnippet[256] = "print('Hello from Idontknow Lua 5.4!')";
                            ImGui::SetNextItemWidth(-90.0f);
                            bool runLua = ImGui::InputText("##LuaQuickEval", s_luaSnippet, sizeof(s_luaSnippet), ImGuiInputTextFlags_EnterReturnsTrue);
                            ImGui::SameLine();
                            if (ImGui::Button("Exec Lua", ImVec2(80, 0))) runLua = true;
                            if (runLua && s_luaSnippet[0] != '\0') {
                                EngineDebug::DebugConsole::Get().Execute(std::string("lua ") + s_luaSnippet);
                            }
                        }
                        ImGui::End();
                    }

                    // 10. Stress Test & Benchmark Studio
                    if (s_showBenchmarkDebugUI) {
                        ImGui::SetNextWindowSize(ImVec2(560, 470), ImGuiCond_FirstUseEver);
                        if (ImGui::Begin("Stress Test & Benchmark Studio", &s_showBenchmarkDebugUI)) {
                            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "GPU / PhysX / DXR Stress Testing & Real-Time Telemetry");
                            ImGui::Separator();

                            const auto& gpu = EngineDebug::Profiler::Get().GetGpuTelemetry();
                            ImGui::Text("Current FPS: %.1f (%.2f ms)  |  Avg FPS: %.1f  |  1%% Low: %.1f",
                                EngineDebug::Profiler::Get().GetCurrentFPS(),
                                EngineDebug::Profiler::Get().GetCurrentFrameTimeMs(),
                                EngineDebug::Profiler::Get().GetAverageFPS(),
                                EngineDebug::Profiler::Get().GetOnePercentLowFPS());
                            ImGui::Text("GPU Frame: %.2f ms  |  Draw Calls: %u  |  Triangles: %u  |  Active Particles: %u",
                                gpu.gpuFrameMs, gpu.drawCalls, gpu.triangles, gpu.activeParticles);

                            ImGui::SeparatorText("One-Click Stress Spawners");
                            if (ImGui::Button("Spawn +25 Dynamic Physics Cubes", ImVec2(260, 30))) {
                                XMFLOAT3 p = m_camera->GetPosition(), f = m_camera->GetForwardVector();
                                XMFLOAT3 base = { p.x + f.x * 7.0f, p.y + 3.0f, p.z + f.z * 7.0f };
                                for (int i = 0; i < 25; ++i) {
                                    XMFLOAT3 sp = { base.x + (i % 5 - 2) * 1.1f, base.y + (i / 5) * 1.1f, base.z + ((i * 3) % 5 - 2) * 0.8f };
                                    m_renderer->SpawnCube(sp);
                                    auto& m = m_renderer->GetModels().back();
                                    m.scale = { 0.45f, 0.45f, 0.45f };
                                    m.physicsBodyType = 2; // Dynamic
                                    EnginePhysics::PhysicsSystem::Get().ApplyModelPhysics(m_renderer.get(), m.modelId, false);
                                }
                                m_renderer->InvalidateRaytracingScene();
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Spawn 16 Max Point Lights Grid", ImVec2(260, 30))) {
                                auto& pls = m_renderer->GetPointLights();
                                pls.clear();
                                for (int i = 0; i < 16; ++i) {
                                    EngineRender::PointLight pl;
                                    pl.name = "StressLight_" + std::to_string(i + 1);
                                    pl.position = { (float)((i % 4) - 1.5f) * 5.0f, 2.5f, (float)((i / 4) - 1.5f) * 5.0f };
                                    pl.color = { 0.3f + 0.7f * ((i * 37) % 10) / 10.0f, 0.3f + 0.7f * ((i * 61) % 10) / 10.0f, 0.3f + 0.7f * ((i * 83) % 10) / 10.0f };
                                    pl.intensity = 4.5f;
                                    pl.range = 12.0f;
                                    pl.enabled = true;
                                    pls.push_back(pl);
                                }
                            }

                            if (ImGui::Button("Spawn Massive PhysX Tower (5x10x5)", ImVec2(260, 30))) {
                                EngineDebug::DebugConsole::Get().Execute("tower 5 10 5");
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Trigger Multi-VFX Particle Storm", ImVec2(260, 30))) {
                                EngineDebug::DebugConsole::Get().Execute("vfx explosion ; vfx fire ; vfx sparks ; vfx magic");
                            }

                            ImGui::SeparatorText("Cleanup & Benchmark Capture");
                            if (ImGui::Button("Clear All Dynamic Objects", ImVec2(170, 28))) {
                                for (const auto& dm : dynModels) EnginePhysics::PhysicsSystem::Get().RemoveActorForModel(dm.modelId);
                                dynModels.clear();
                                m_renderer->InvalidateRaytracingScene();
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Clear All Lights", ImVec2(170, 28))) {
                                EngineDebug::DebugConsole::Get().Execute("light_clear");
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Save Screenshot BMP", ImVec2(170, 28))) {
                                EngineDebug::DebugConsole::Get().Execute("screenshot");
                            }
                        }
                        ImGui::End();
                    }

                } else {
                    EngineDebug::Profiler::Get().DrawUI();
                    EngineDebug::DebugConsole::Get().DrawUI();
                }

#if defined(ENGINE_EDITOR)
                if(showEditorUI&&s_showSceneGizmos)
                    EngineUI::DrawSceneGizmos(*m_renderer,*m_camera,float(m_width),float(m_height),s_selectedModelType,s_selectedModelIndex,s_showLightsUI);
#endif

                // 11. Viewport 3D Debug Overlays (Visible in BOTH Editor and In-Game modes)
                if (s_debugShowObjectLabels || s_debugShowWorldGrid || s_debugShowCrosshairRay || s_debugShowSunWindVectors) {
                    const auto& models = m_renderer->GetModels();
                    const auto& dynModels = m_renderer->GetDynamicModels();
                    ImDrawList* fgDraw = ImGui::GetForegroundDrawList();
                    XMMATRIX vp = m_camera->GetViewMatrix() * m_camera->GetCleanProjectionMatrix(XMConvertToRadians(m_renderer->GetFOV()), (float)m_width / (float)m_height, 0.1f, 2000.0f);
                    auto projectLine = [&](const XMFLOAT3& p0, const XMFLOAT3& p1, ImU32 col, float thickness = 1.5f) {
                        XMVECTOR a = XMVector4Transform(XMVectorSet(p0.x, p0.y, p0.z, 1.0f), vp);
                        XMVECTOR b = XMVector4Transform(XMVectorSet(p1.x, p1.y, p1.z, 1.0f), vp);
                        if (XMVectorGetW(a) <= 0.1f || XMVectorGetW(b) <= 0.1f) return;
                        a = XMVectorScale(a, 1.0f / XMVectorGetW(a));
                        b = XMVectorScale(b, 1.0f / XMVectorGetW(b));
                        fgDraw->AddLine(
                            ImVec2((XMVectorGetX(a) * 0.5f + 0.5f) * m_width, (-XMVectorGetY(a) * 0.5f + 0.5f) * m_height),
                            ImVec2((XMVectorGetX(b) * 0.5f + 0.5f) * m_width, (-XMVectorGetY(b) * 0.5f + 0.5f) * m_height),
                            col, thickness);
                    };
                    auto projectPoint = [&](const XMFLOAT3& pt, ImVec2& outScreen) -> bool {
                        XMVECTOR v = XMVector4Transform(XMVectorSet(pt.x, pt.y, pt.z, 1.0f), vp);
                        if (XMVectorGetW(v) <= 0.1f) return false;
                        v = XMVectorScale(v, 1.0f / XMVectorGetW(v));
                        outScreen.x = (XMVectorGetX(v) * 0.5f + 0.5f) * m_width;
                        outScreen.y = (-XMVectorGetY(v) * 0.5f + 0.5f) * m_height;
                        return (outScreen.x >= 0.0f && outScreen.x <= (float)m_width && outScreen.y >= 0.0f && outScreen.y <= (float)m_height);
                    };

                    if (s_debugShowWorldGrid) {
                        const int gridExtent = 20;
                        for (int g = -gridExtent; g <= gridExtent; ++g) {
                            ImU32 col = (g == 0) ? IM_COL32(180, 180, 200, 180) : ((g % 5 == 0) ? IM_COL32(110, 120, 140, 130) : IM_COL32(75, 80, 95, 85));
                            float thick = (g % 5 == 0) ? 1.5f : 1.0f;
                            projectLine({ (float)g, 0.02f, (float)-gridExtent }, { (float)g, 0.02f, (float)gridExtent }, col, thick);
                            projectLine({ (float)-gridExtent, 0.02f, (float)g }, { (float)gridExtent, 0.02f, (float)g }, col, thick);
                        }
                    }

                    if (s_debugShowSunWindVectors) {
                        XMFLOAT3 camP = m_camera->GetPosition();
                        XMFLOAT3 camF = m_camera->GetForwardVector();
                        XMFLOAT3 anchor = { camP.x + camF.x * 4.0f, camP.y + camF.y * 4.0f, camP.z + camF.z * 4.0f };
                        XMFLOAT3 lDir = m_renderer->GetLightDir();
                        XMFLOAT3 sunEnd = { anchor.x - lDir.x * 2.0f, anchor.y - lDir.y * 2.0f, anchor.z - lDir.z * 2.0f };
                        projectLine(anchor, sunEnd, IM_COL32(255, 220, 60, 240), 2.8f);
                        ImVec2 sunScreen;
                        if (projectPoint(sunEnd, sunScreen)) {
                            fgDraw->AddText(sunScreen, IM_COL32(255, 230, 80, 255), "SUN");
                        }
                        const auto& wind = EngineRain::RainSystem::Get().GetSettings().wind;
                        if (wind.enabled) {
                            XMFLOAT3 wEnd = { anchor.x + wind.direction.x * 2.0f, anchor.y + wind.direction.y * 2.0f, anchor.z + wind.direction.z * 2.0f };
                            projectLine(anchor, wEnd, IM_COL32(80, 220, 255, 240), 2.4f);
                            ImVec2 wScreen;
                            if (projectPoint(wEnd, wScreen)) {
                                fgDraw->AddText(wScreen, IM_COL32(100, 230, 255, 255), "WIND");
                            }
                        }
                    }

                    if (s_debugShowObjectLabels) {
                        XMFLOAT3 camP = m_camera->GetPosition();
                        auto drawLabel = [&](const std::string& name, uint32_t id, const XMFLOAT3& pos, bool isDyn) {
                            ImVec2 scr;
                            XMFLOAT3 labelPos = { pos.x, pos.y + 0.6f, pos.z };
                            if (!projectPoint(labelPos, scr)) return;
                            float dx = pos.x - camP.x, dy = pos.y - camP.y, dz = pos.z - camP.z;
                            float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
                            if (dist > 80.0f) return;
                            char buf[128];
                            snprintf(buf, sizeof(buf), "%s [#%u] (%.1fm)", name.c_str(), id, dist);
                            ImVec2 tsz = ImGui::CalcTextSize(buf);
                            fgDraw->AddRectFilled(ImVec2(scr.x - 4, scr.y - 2), ImVec2(scr.x + tsz.x + 4, scr.y + tsz.y + 2), IM_COL32(15, 18, 24, 185), 3.0f);
                            fgDraw->AddText(scr, isDyn ? IM_COL32(110, 225, 255, 255) : IM_COL32(240, 245, 250, 245), buf);
                        };
                        for (const auto& m : models) if (m.visible && m.name != "Floor") drawLabel(m.name, m.modelId, m.position, false);
                        for (const auto& dm : dynModels) if (dm.visible) drawLabel(dm.name, dm.modelId, dm.position, true);
                    }

                    if (s_debugShowCrosshairRay) {
                        float cx = m_width * 0.5f, cy = m_height * 0.5f;
                        fgDraw->AddLine(ImVec2(cx - 10, cy), ImVec2(cx + 10, cy), IM_COL32(80, 255, 120, 220), 1.5f);
                        fgDraw->AddLine(ImVec2(cx, cy - 10), ImVec2(cx, cy + 10), IM_COL32(80, 255, 120, 220), 1.5f);
                        fgDraw->AddCircle(ImVec2(cx, cy), 4.0f, IM_COL32(80, 255, 120, 180), 16, 1.2f);
                        EnginePhysics::RaycastHitInfo hit;
                        if (EnginePhysics::PhysicsSystem::Get().Raycast(m_camera->GetPosition(), m_camera->GetForwardVector(), 500.0f, hit) && hit.hasHit) {
                            char hitBuf[160];
                            snprintf(hitBuf, sizeof(hitBuf), "Target ID #%u | %.2fm | (%.1f, %.1f, %.1f)",
                                hit.ownerModelId, hit.distance, hit.position.x, hit.position.y, hit.position.z);
                            fgDraw->AddText(ImVec2(cx + 14, cy + 10), IM_COL32(100, 255, 140, 255), hitBuf);
                            XMFLOAT3 nEnd = { hit.position.x + hit.normal.x * 0.8f, hit.position.y + hit.normal.y * 0.8f, hit.position.z + hit.normal.z * 0.8f };
                            projectLine(hit.position, nEnd, IM_COL32(255, 90, 90, 240), 2.2f);
                        }
                    }
                }

                if (s_debugShowAllColliders || s_debugShowModelBounds || s_debugShowLightGizmos || s_debugShowWorldAxes) {
                    const auto& models = m_renderer->GetModels();
                    const auto& dynModels = m_renderer->GetDynamicModels();
                    ImDrawList* fgDraw = ImGui::GetForegroundDrawList();
                        XMMATRIX vp = m_camera->GetViewMatrix() * m_camera->GetCleanProjectionMatrix(XMConvertToRadians(m_renderer->GetFOV()), (float)m_width / (float)m_height, 0.1f, 2000.0f);
                        auto projectLine = [&](const XMFLOAT3& p0, const XMFLOAT3& p1, ImU32 col, float thickness = 1.5f) {
                            XMVECTOR a = XMVector4Transform(XMVectorSet(p0.x, p0.y, p0.z, 1.0f), vp);
                            XMVECTOR b = XMVector4Transform(XMVectorSet(p1.x, p1.y, p1.z, 1.0f), vp);
                            if (XMVectorGetW(a) <= 0.1f || XMVectorGetW(b) <= 0.1f) return;
                            a = XMVectorScale(a, 1.0f / XMVectorGetW(a));
                            b = XMVectorScale(b, 1.0f / XMVectorGetW(b));
                            fgDraw->AddLine(
                                ImVec2((XMVectorGetX(a) * 0.5f + 0.5f) * m_width, (-XMVectorGetY(a) * 0.5f + 0.5f) * m_height),
                                ImVec2((XMVectorGetX(b) * 0.5f + 0.5f) * m_width, (-XMVectorGetY(b) * 0.5f + 0.5f) * m_height),
                                col, thickness);
                        };

                        if (s_debugShowWorldAxes) {
                            projectLine({0,0,0}, {5,0,0}, IM_COL32(255, 60, 60, 240), 2.5f);
                            projectLine({0,0,0}, {0,5,0}, IM_COL32(60, 255, 60, 240), 2.5f);
                            projectLine({0,0,0}, {0,0,5}, IM_COL32(60, 140, 255, 240), 2.5f);
                        }

                        if (s_debugShowAllColliders) {
                            for (const auto& m : models) {
                                if (!m.hasCollision) continue;
                                auto lines = EnginePhysics::PhysicsSystem::Get().GetColliderWireframe(m.modelId);
                                for (size_t i = 0; i + 1 < lines.size(); i += 2) projectLine(lines[i], lines[i+1], IM_COL32(70, 240, 130, 190), 1.3f);
                            }
                            for (const auto& dm : dynModels) {
                                if (!dm.hasCollision) continue;
                                auto lines = EnginePhysics::PhysicsSystem::Get().GetColliderWireframe(dm.modelId);
                                for (size_t i = 0; i + 1 < lines.size(); i += 2) projectLine(lines[i], lines[i+1], IM_COL32(80, 210, 255, 210), 1.5f);
                            }
                        }

                        if (s_debugShowModelBounds) {
                            auto drawAABB = [&](uint32_t modelId, ImU32 col) {
                                XMFLOAT3 lo, hi;
                                if (!EnginePhysics::PhysicsSystem::Get().GetColliderBounds(modelId, lo, hi)) return;
                                XMFLOAT3 c[8] = {
                                    {lo.x,lo.y,lo.z}, {hi.x,lo.y,lo.z}, {hi.x,hi.y,lo.z}, {lo.x,hi.y,lo.z},
                                    {lo.x,lo.y,hi.z}, {hi.x,lo.y,hi.z}, {hi.x,hi.y,hi.z}, {lo.x,hi.y,hi.z}
                                };
                                int edges[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
                                for (auto& e : edges) projectLine(c[e[0]], c[e[1]], col, 1.2f);
                            };
                            for (const auto& m : models) if (m.name != "Floor") drawAABB(m.modelId, IM_COL32(255, 200, 60, 180));
                            for (const auto& dm : dynModels) drawAABB(dm.modelId, IM_COL32(255, 120, 220, 200));
                        }

                        if (m_camera->IsFreecamMode() && s_debugShowLightGizmos) {
                            for (const auto& pl : m_renderer->GetPointLights()) {
                                if (!pl.enabled) continue;
                                ImU32 col = IM_COL32((int)(pl.color.x*255), (int)(pl.color.y*255), (int)(pl.color.z*255), 200);
                                const int segs = 24;
                                for (int s = 0; s < segs; ++s) {
                                    float a0 = (s / (float)segs) * XM_2PI;
                                    float a1 = ((s + 1) / (float)segs) * XM_2PI;
                                    projectLine(
                                        {pl.position.x + cosf(a0)*pl.range, pl.position.y, pl.position.z + sinf(a0)*pl.range},
                                        {pl.position.x + cosf(a1)*pl.range, pl.position.y, pl.position.z + sinf(a1)*pl.range}, col, 1.4f);
                                }
                            }
                            for (const auto& sl : m_renderer->GetSpotLights()) {
                                if (!sl.enabled) continue;
                                ImU32 col = IM_COL32((int)(sl.color.x*255), (int)(sl.color.y*255), (int)(sl.color.z*255), 220);
                                XMFLOAT3 endPt = {
                                    sl.position.x + sl.direction.x * sl.range,
                                    sl.position.y + sl.direction.y * sl.range,
                                    sl.position.z + sl.direction.z * sl.range
                                };
                                projectLine(sl.position, endPt, col, 2.0f);
                            }
                        }
                    }


                if (!showEditorUI || EngineDebug::DebugConsole::Get().IsFpsOverlayEnabled()) {
                    const float hudY = showEditorUI ? 68.0f : 10.0f;
                    ImGui::SetNextWindowPos(ImVec2(10.0f, hudY), ImGuiCond_Always);
                    ImGui::SetNextWindowBgAlpha(0.65f);
                    if (ImGui::Begin("##InGameTelemetryHUD", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs)) {
                        ImVec4 fpsCol = (m_fps >= 58.0f) ? ImVec4(0.25f, 1.0f, 0.45f, 1.0f) : ((m_fps >= 30.0f) ? ImVec4(1.0f, 0.85f, 0.25f, 1.0f) : ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
                        ImGui::TextColored(fpsCol, "FPS: %.1f  |  Frame: %.2f ms  |  GPU: %.2f ms (Wait: %.2f ms)",
                            m_fps, realFrameTime * 1000.0f, m_renderer->GetLastGpuFrameMs(), m_renderer->GetLastGpuWaitMs());

                        if (EngineDebug::DebugConsole::Get().IsFpsOverlayEnabled()) {
                            const char* rtMode = !m_renderer->m_dxrEnabled ? "DX12 Raster" : (m_renderer->IsPathTracingEnabled() ? "DXR Full Path Tracing" : "DXR 1.1 Hybrid RT");
                            const char* upNames[] = { "Native", "DLSS", "FSR", "MetalFX", "XeSS" };
                            const char* qNames[] = { "Native AA", "Quality", "Balanced", "Perf", "UltraPerf" };
                            int upIdx = (std::clamp)(static_cast<int>(m_renderer->GetUpscaleTechnique()), 0, 4);
                            int qIdx = (std::clamp)(static_cast<int>(m_renderer->GetUpscaleQuality()), 0, 4);
                            ImGui::TextColored(ImVec4(0.55f, 0.88f, 1.0f, 1.0f), "Res: %dx%d (Internal %dx%d)  |  %s  |  Upscaler: %s (%s)",
                                m_renderer->GetWidth(), m_renderer->GetHeight(), m_renderer->GetRenderWidth(), m_renderer->GetRenderHeight(),
                                rtMode, upNames[upIdx], upIdx == 0 ? (m_renderer->IsTAAEnabled() ? "TAA" : "Off") : qNames[qIdx]);

                            uint32_t tris = 0;
                            for (const auto& m : m_renderer->GetModels()) if (m.visible) tris += m.indexCount / 3;
                            for (const auto& dm : m_renderer->GetDynamicModels()) if (dm.visible) tris += dm.indexCount / 3;
                            XMFLOAT3 cp = m_camera->GetPosition();
                            ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.9f, 1.0f), "Models: %zu+%zu (%u Tris)  |  LOD 0/1/2: %u/%u/%u  |  Pos: (%.1f, %.1f, %.1f)",
                                m_renderer->GetModels().size(), m_renderer->GetDynamicModels().size(), tris,
                                m_renderer->GetLOD0Count(), m_renderer->GetLOD1Count(), m_renderer->GetLOD2Count(), cp.x, cp.y, cp.z);
                        }

                        if (!showEditorUI) {
                            ImGui::TextColored(ImVec4(1.0f, 0.92f, 0.3f, 1.0f), "[TAB] Editor UI   [F3] Toggle Telemetry   [~] Console   [C] Camera");
                        }
                        auto& mpMgr = EngineNetwork::MultiplayerManager::Get();
                        if (mpMgr.IsConnected()) {
                            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "Multiplayer: %s | ID #%u | Ping: %.0fms | Players: %zu",
                                mpMgr.GetServerAddressString().c_str(), mpMgr.GetLocalClientId(), mpMgr.GetPingMs(), mpMgr.GetConnectedPlayerCount());
                        } else if (mpMgr.IsConnecting()) {
                            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.2f, 1.0f), "Multiplayer: Connecting to %s...", mpMgr.GetServerAddressString().c_str());
                        }
                    }
                    ImGui::End();
                }

                if(!m_benchmarkFrameLimit){
                    auto& saves=EngineSave::SaveManager::Get();
                    if(!m_recoveryChecked){
                        m_recoveryChecked=true;std::error_code ec;
                        auto canonical=EngineProject::Path(saves.GetSaveFilePath());
                        auto savedTime=std::filesystem::last_write_time(canonical,ec);bool missing=bool(ec);
                        for(int i=0;i<6;++i){auto path=EngineProject::Path(saves.GetSaveFilePath("EditorRecovery"+std::to_string(i)+".save"));
                            ec.clear();auto time=std::filesystem::last_write_time(path,ec);if(!ec&&(missing||time>savedTime))m_showRecovery=true;
                        }
                    }
                    static auto lastAutosave=std::chrono::steady_clock::now();
                    static std::string lastAutosaveState;
                    if(std::chrono::steady_clock::now()-lastAutosave>std::chrono::seconds(m_autosaveSeconds)){
                        lastAutosave=std::chrono::steady_clock::now();auto state=CaptureScene();
                        if(state!=lastAutosaveState&&m_camera->IsFreecamMode()){
                            if(saves.SaveGame(state,"","EditorRecovery"+std::to_string(m_recoverySlot)+".save")){lastAutosaveState=std::move(state);m_recoverySlot=(m_recoverySlot+1)%6;}
                        }
                    }
                    if(m_showRecovery)ImGui::OpenPopup("Scene recovery");
                    if(ImGui::BeginPopupModal("Scene recovery",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
                        ImGui::TextWrapped("Recovery copies and the five previous saved versions belong to this project. Restoring can be undone.");
                        for(int i=0;i<12;++i){
                            const std::string slot=i<6?"EditorRecovery"+std::to_string(i)+".save":saves.GetSaveFilePath()+(i==11?".bak":".version"+std::to_string(i-5));
                            if(saves.SaveExists(slot)){
                                ImGui::PushID(i);
                                if(ImGui::Button((std::string("Restore ")+(i<6?"autosave ":"saved version ")+std::to_string(i<6?i+1:i-5)).c_str())){
                                    EngineSave::SavePackage package;if(saves.LoadGame(package,slot)){ApplyScene(package.sceneData);sceneDirty=true;s_selectedModelType=0;s_selectedModelIndex=-1;m_showRecovery=false;ImGui::CloseCurrentPopup();}
                                }
                                ImGui::PopID();
                            }
                        }
                        if(ImGui::Button("Keep current scene")){m_showRecovery=false;ImGui::CloseCurrentPopup();}
                        ImGui::EndPopup();
                    }
                }
                static bool pendingSave=false;
                static auto lastEdit = std::chrono::steady_clock::now();
                bool edited = sceneDirty;
                if (!edited && showEditorUI && (ImGui::IsAnyItemActive() || ImGui::IsMouseReleased(0))) {
                    std::string curState = editorState();
                    if (curState != s_lastEditorState) { s_lastEditorState = std::move(curState); edited = true; }
                } else if (edited) {
                    s_lastEditorState = editorState();
                }
                if(edited)m_historyChanged=true;
                static auto historyCheck=std::chrono::steady_clock::now();
                if(m_camera->IsFreecamMode()&&!ImGui::IsAnyItemActive()&&!ImGui::IsMouseDown(0)&&!ImGuizmo::IsUsing()&&
                    (m_historyChanged||ImGui::IsMouseReleased(0)||std::chrono::steady_clock::now()-historyCheck>std::chrono::milliseconds(500))){
                    if(m_editHistory.Commit(CaptureScene())){pendingSave=true;lastEdit=std::chrono::steady_clock::now();}
                    m_historyChanged=false;historyCheck=std::chrono::steady_clock::now();
                }
                if (edited) { pendingSave=true; lastEdit=std::chrono::steady_clock::now(); }
                if (pendingSave && !ImGui::IsMouseDown(0) && !ImGui::IsAnyItemActive() && std::chrono::steady_clock::now()-lastEdit > std::chrono::milliseconds(750) && !m_benchmarkFrameLimit && m_camera->IsFreecamMode()) {
                    SaveScene(); pendingSave=false;
                }
                EngineDebug::Profiler::Get().RecordSubsystemTime(EngineDebug::Subsystem::UI,std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now()-editorUIStart).count());
#endif

                // Update sequencer (animate models)
                m_sequencer->Update(dt, m_renderer.get());

                const auto renderStart = std::chrono::steady_clock::now();
                if (m_benchmarkFrameLimit) {
                    m_camera->SetPosition(-8.0f, 4.0f, -8.0f);
                    m_camera->SetPitch(-0.10f);
                    m_camera->SetYaw(0.464f);
#if defined(_WIN32)
                    if (GetEnvironmentVariableW(L"IDK_BENCHMARK_CAMERA_MOTION", nullptr, 0) > 0) {
                        const float distance = 2.0f * std::sin(static_cast<float>(benchmarkFrames) * 6.2831853f / 59.0f);
                        m_camera->SetPosition(-8.0f + distance * 0.447f, 4.0f, -8.0f + distance * 0.894f);
                    }
#endif
                    if (m_benchmarkCloudDensity >= 0.0f)
                        m_renderer->SetCloudDensity(m_benchmarkCloudDensity);
                }
                if(m_benchmarkFrameLimit&&std::isfinite(m_benchmarkSunElevation)&&m_benchmarkSunElevation>=-90&&m_benchmarkSunElevation<=90){
                    m_camera->SetFreecamMode(true);m_camera->SetPosition(0,4,-5);
                    m_camera->SetYaw(XMConvertToRadians(m_benchmarkSkyYaw));m_camera->SetPitch(.15f);
                    m_renderer->SetSunAzimuth(m_benchmarkSunAzimuth);m_renderer->SetSunElevation(m_benchmarkSunElevation);
                }
                { EngineDebug::ScopedTimer profile(EngineDebug::Subsystem::Render);m_renderer->Render(dt); }
                if (m_renderer->IsDeviceRemoved()) return 3;
                if (m_benchmarkFrameLimit) {
                    const auto renderEnd = std::chrono::steady_clock::now();
                    if(m_benchmarkFSRFG&&m_benchmarkXeFG){
                        if(benchmarkFrames==30||benchmarkFrames==150)m_renderer->SetFSRFrameGenEnabled(true);
                        if(benchmarkFrames==60){m_renderer->SetFSRFrameGenEnabled(false);m_renderer->SetXeSSFrameGenEnabled(false);}
                        if(benchmarkFrames==90)m_renderer->SetXeSSFrameGenEnabled(true);
                        if(benchmarkFrames==120)m_renderer->Resize(1024,576);
                    }
                    benchmark << benchmarkFrames << ',' << realFrameTime * 1000.0f << ','
                        << std::chrono::duration<double, std::milli>(renderStart - frameStart).count() << ','
                        << std::chrono::duration<double, std::milli>(renderEnd - renderStart).count() << ','
                        << m_renderer->GetLastGpuWaitMs() << ',' << m_renderer->GetLastGpuFrameMs() << ','
                        << m_renderer->GetRenderWidth() << ',' << m_renderer->GetRenderHeight() << ','
                        << m_renderer->m_dxrEnabled << '\n';
                    if (++benchmarkFrames >= m_benchmarkFrameLimit) {
#if defined(_WIN32)
                        wchar_t executable[MAX_PATH] = {};
                        GetModuleFileNameW(nullptr, executable, MAX_PATH);
                        const auto output = EngineDiagnostics::Directory();
                        m_renderer->SaveScreenshotBMP((output / "benchmark_frame.bmp").string());
                        std::ofstream world(output / "benchmark_world.txt");
                        for (const auto& model : m_renderer->GetModels()) world << model.name << '\n';
                        for (const auto& model : m_renderer->GetDynamicModels()) world << "DYNAMIC " << model.name << '\n';
#endif
                        break;
                    }
                }

                // Update GPU Telemetry and end profiler frame
                EngineDebug::GpuTelemetry telemetry;
                telemetry.drawCalls = static_cast<uint32_t>(m_renderer->GetModels().size() + m_renderer->GetDynamicModels().size());
                uint32_t totalTriangles = 0;
                for (const auto& model : m_renderer->GetModels()) totalTriangles += model.indexCount / 3;
                for (const auto& model : m_renderer->GetDynamicModels()) totalTriangles += model.indexCount / 3;
                telemetry.triangles = totalTriangles;
                telemetry.pointLights = static_cast<uint32_t>(m_renderer->GetPointLights().size());
                telemetry.spotLights = static_cast<uint32_t>(m_renderer->GetSpotLights().size());
                telemetry.activeParticles = static_cast<uint32_t>(EngineVFX::ParticleSystem::Get().GetTotalActiveParticles());
                telemetry.lod0Models = m_renderer->GetLOD0Count();
                telemetry.lod1Models = m_renderer->GetLOD1Count();
                telemetry.lod2Models = m_renderer->GetLOD2Count();
                telemetry.lodCulledModels = m_renderer->GetLODCulledCount();
                telemetry.gpuPassMs=m_renderer->GetGpuPassTimes();
                telemetry.gpuFrameMs = static_cast<float>(m_renderer->GetLastGpuFrameMs());
                telemetry.gpuWaitMs = static_cast<float>(m_renderer->GetLastGpuWaitMs());
                EngineDebug::Profiler::Get().SetGpuTelemetry(telemetry);
                EngineDebug::Profiler::Get().EndFrame();

#if defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
                static uint32_t s_ps4FrameCount = 0;
                s_ps4FrameCount++;
                if (s_ps4FrameCount <= 10 || (s_ps4FrameCount % 60) == 0) {
                    std::cout << "[Idontknow PS4] Frame " << s_ps4FrameCount << " rendered & presented (FPS: " << m_fps << ")" << std::endl;
                }
#endif
            } else {
                static bool loggedOnce = false;
                if (!loggedOnce) {
                    LOG("App::Run: Device removed, skipping frame rendering");
                    loggedOnce = true;
                }
            }
        }
    }
    LOG("App::Run: Exiting loop, saving scene to Save1.save");
    if (!m_benchmarkFrameLimit) SaveScene();
#if defined(_WIN32)
    return m_benchmarkFrameLimit ? 0 : static_cast<int>(msg.wParam);
#else
    return 0;
#endif
}
