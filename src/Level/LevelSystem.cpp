#include "LevelSystem.h"
#include "Core/Project.h"
static std::filesystem::path LevelPath(const std::string& value){return EngineProject::active?EngineProject::Path(value):std::filesystem::path(value);}
static std::string LevelString(const std::filesystem::path& value){return EngineProject::active?EngineProject::Utf8(value):value.string();}
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <iomanip>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#ifndef ENGINE_SERVER
#include "Renderer/Renderer.h"
#include "Renderer/Camera.h"
#include "Physics/PhysicsSystem.h"
#include "Physics/ClothSystem.h"
#include "Physics/HairSystem.h"
#include <imgui.h>
#endif

namespace EngineLevel {

std::string LevelData::Serialize() const {
    std::ostringstream ss;
    ss << "# ==========================================================\n"
       << "# Idontknow Level / Map Data File\n"
       << "# Extension: .level (Packed into .pak for release distribution)\n"
       << "# ==========================================================\n\n";

    ss << "FORMAT " << format << "\n";
    ss << "VERSION " << version << "\n";
    ss << "LEVEL_NAME " << levelName << "\n";
    ss << "AUTHOR " << author << "\n";
    ss << "DESCRIPTION " << description << "\n\n";

    // Player Spawn
    ss << "# Player Spawn Point\n";
    ss << "SPAWN_POS " << spawnPosition.x << " " << spawnPosition.y << " " << spawnPosition.z << "\n";
    ss << "SPAWN_ROT " << spawnRotation.x << " " << spawnRotation.y << " " << spawnRotation.z << "\n";
    ss << "SPAWN_YAW " << spawnYaw << "\n\n";

    // Environment
    ss << "# Environment & Atmosphere\n";
    ss << "SKY_COLOR " << skyColor.x << " " << skyColor.y << " " << skyColor.z << "\n";
    ss << "LIGHT_DIR " << lightDir.x << " " << lightDir.y << " " << lightDir.z << "\n";
    ss << "LIGHT_COLOR " << lightColor.x << " " << lightColor.y << " " << lightColor.z << "\n";
    ss << "AMBIENT_COLOR " << ambientColor.x << " " << ambientColor.y << " " << ambientColor.z << "\n";
    ss << "FOG_COLOR " << fogColor.x << " " << fogColor.y << " " << fogColor.z << "\n";
    ss << "FOG_DENSITY " << fogDensity << "\n\n";

    // Weather
    ss << "# Weather Settings\n";
    ss << "RAIN_ENABLED " << (rainEnabled ? 1 : 0) << "\n";
    ss << "RAIN_INTENSITY " << rainIntensity << "\n\n";

    // Local Point Lights
    ss << "# Local Point Lights (" << pointLights.size() << " lights)\n";
    for (const auto& pl : pointLights) {
        ss << "POINT_LIGHT_START\n";
        ss << "NAME " << pl.name << "\n";
        ss << "POS " << pl.position.x << " " << pl.position.y << " " << pl.position.z << "\n";
        ss << "COLOR " << pl.color.x << " " << pl.color.y << " " << pl.color.z << "\n";
        ss << "INTENSITY " << pl.intensity << "\n";
        ss << "RANGE " << pl.range << "\n";
        ss << "ENABLED " << (pl.enabled ? 1 : 0) << "\n";
        ss << "LIGHT_END\n";
    }
    ss << "\n";

    // Local Spot Lights
    ss << "# Local Spot Lights (" << spotLights.size() << " lights)\n";
    for (const auto& sl : spotLights) {
        ss << "SPOT_LIGHT_START\n";
        ss << "NAME " << sl.name << "\n";
        ss << "POS " << sl.position.x << " " << sl.position.y << " " << sl.position.z << "\n";
        ss << "DIR " << sl.direction.x << " " << sl.direction.y << " " << sl.direction.z << "\n";
        ss << "COLOR " << sl.color.x << " " << sl.color.y << " " << sl.color.z << "\n";
        ss << "INTENSITY " << sl.intensity << "\n";
        ss << "RANGE " << sl.range << "\n";
        ss << "INNER_ANGLE " << sl.innerAngle << "\n";
        ss << "OUTER_ANGLE " << sl.outerAngle << "\n";
        ss << "ENABLED " << (sl.enabled ? 1 : 0) << "\n";
        ss << "LIGHT_END\n";
    }
    ss << "\n";

    // Entities
    ss << "# Level Entities (" << entities.size() << " objects)\n";
    for (const auto& e : entities) {
        ss << "ENTITY_START\n";
        ss << "NAME " << e.name << "\n";
        if (!e.filepath.empty()) ss << "FILEPATH " << e.filepath << "\n";
        ss << "PRIMITIVE " << e.primitiveType << "\n";
        ss << "POS " << e.position.x << " " << e.position.y << " " << e.position.z << "\n";
        ss << "ROT " << e.rotation.x << " " << e.rotation.y << " " << e.rotation.z << "\n";
        ss << "SCALE " << e.scale.x << " " << e.scale.y << " " << e.scale.z << "\n";
        ss << "VISIBLE " << (e.visible ? 1 : 0) << "\n";
        ss << "COLLISION " << (e.hasCollision ? 1 : 0) << "\n";
        ss << "PRESET " << e.materialPreset << "\n";
        ss << "MAT_ALBEDO " << e.albedo.x << " " << e.albedo.y << " " << e.albedo.z << "\n";
        ss << "MAT_ROUGHNESS " << e.roughness << "\n";
        ss << "MAT_METALLIC " << e.metallic << "\n";
        ss << "MAT_REFLECTANCE " << e.reflectance << "\n";
        ss << "MAT_EMISSION " << e.emission << "\n";
        ss << "MAT_IOR " << e.ior << "\n";
        ss << "UV_TILING " << e.uvTiling.x << " " << e.uvTiling.y << "\n";
        if (!e.albedoPath.empty()) ss << "ALBEDO_PATH " << e.albedoPath << "\n";
        if (!e.normalPath.empty()) ss << "NORMAL_PATH " << e.normalPath << "\n";
        if (!e.rmaPath.empty()) ss << "RMA_PATH " << e.rmaPath << "\n";
        if (!e.aoPath.empty()) ss << "AO_PATH " << e.aoPath << "\n";
        if (!e.emissivePath.empty()) ss << "EMISSIVE_PATH " << e.emissivePath << "\n";
        EnginePhysics::WriteCollider(ss, e.collider);
        EnginePhysics::WriteRagdoll(ss, e.ragdollSettings);
        EnginePhysics::WriteVehicle(ss, e.vehicle);
        ss << "PHYSICS_BODY " << e.physicsBodyType << "\n";
        ss << "PHYSICS_SHAPE " << e.physicsShapeType << "\n";
        ss << "PHYSICS_MASS " << e.physicsMass << "\n";
        ss << "PHYSICS_DAMPING " << e.physicsLinearDamping << " " << e.physicsAngularDamping << "\n";
        ss << "PHYSICS_FRICTION " << e.physicsFriction << "\n";
        ss << "PHYSICS_RESTITUTION " << e.physicsRestitution << "\n";
        ss << "PHYSICS_GRAVITY " << (e.physicsEnableGravity ? 1 : 0) << "\n";
        ss << "PHYSICS_CCD " << (e.physicsEnableCCD ? 1 : 0) << "\n";
        ss << "ENTITY_END\n\n";
    }

    return ss.str();
}

bool LevelData::Deserialize(const std::string& text) {
    if (text.empty()) return false;

    entities.clear();
    pointLights.clear();
    spotLights.clear();
    std::istringstream stream(text);
    std::string line;
    LevelEntity currentEntity;
    bool inEntity = false;

    EngineRender::PointLight currentPointLight;
    bool inPointLight = false;

    EngineRender::SpotLight currentSpotLight;
    bool inSpotLight = false;

    while (std::getline(stream, line)) {
        // Strip comment
        size_t cPos = line.find('#');
        if (cPos != std::string::npos) line = line.substr(0, cPos);

        // Trim
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string tag;
        iss >> tag;

        if (tag == "FORMAT") iss >> format;
        else if (tag == "VERSION") iss >> version;
        else if (tag == "LEVEL_NAME") std::getline(iss >> std::ws, levelName);
        else if (tag == "AUTHOR") std::getline(iss >> std::ws, author);
        else if (tag == "DESCRIPTION") std::getline(iss >> std::ws, description);
        else if (tag == "SPAWN_POS") iss >> spawnPosition.x >> spawnPosition.y >> spawnPosition.z;
        else if (tag == "SPAWN_ROT") iss >> spawnRotation.x >> spawnRotation.y >> spawnRotation.z;
        else if (tag == "SPAWN_YAW") iss >> spawnYaw;
        else if (tag == "SKY_COLOR") iss >> skyColor.x >> skyColor.y >> skyColor.z;
        else if (tag == "LIGHT_DIR") iss >> lightDir.x >> lightDir.y >> lightDir.z;
        else if (tag == "LIGHT_COLOR") iss >> lightColor.x >> lightColor.y >> lightColor.z;
        else if (tag == "AMBIENT_COLOR") iss >> ambientColor.x >> ambientColor.y >> ambientColor.z;
        else if (tag == "FOG_COLOR") iss >> fogColor.x >> fogColor.y >> fogColor.z;
        else if (tag == "FOG_DENSITY") iss >> fogDensity;
        else if (tag == "RAIN_ENABLED") { int r; iss >> r; rainEnabled = (r != 0); }
        else if (tag == "RAIN_INTENSITY") iss >> rainIntensity;
        else if (tag == "ENTITY_START") {
            currentEntity = LevelEntity();
            inEntity = true;
        } else if (tag == "ENTITY_END") {
            if (inEntity) {
                entities.push_back(currentEntity);
                inEntity = false;
            }
        } else if (tag == "POINT_LIGHT_START") {
            currentPointLight = EngineRender::PointLight();
            inPointLight = true;
        } else if (tag == "SPOT_LIGHT_START") {
            currentSpotLight = EngineRender::SpotLight();
            inSpotLight = true;
        } else if (tag == "LIGHT_END") {
            if (inPointLight) {
                pointLights.push_back(currentPointLight);
                inPointLight = false;
            } else if (inSpotLight) {
                spotLights.push_back(currentSpotLight);
                inSpotLight = false;
            }
        } else if (inPointLight) {
            if (tag == "NAME") std::getline(iss >> std::ws, currentPointLight.name);
            else if (tag == "POS") iss >> currentPointLight.position.x >> currentPointLight.position.y >> currentPointLight.position.z;
            else if (tag == "COLOR") iss >> currentPointLight.color.x >> currentPointLight.color.y >> currentPointLight.color.z;
            else if (tag == "INTENSITY") iss >> currentPointLight.intensity;
            else if (tag == "RANGE") iss >> currentPointLight.range;
            else if (tag == "ENABLED") { int en; iss >> en; currentPointLight.enabled = (en != 0); }
        } else if (inSpotLight) {
            if (tag == "NAME") std::getline(iss >> std::ws, currentSpotLight.name);
            else if (tag == "POS") iss >> currentSpotLight.position.x >> currentSpotLight.position.y >> currentSpotLight.position.z;
            else if (tag == "DIR") iss >> currentSpotLight.direction.x >> currentSpotLight.direction.y >> currentSpotLight.direction.z;
            else if (tag == "COLOR") iss >> currentSpotLight.color.x >> currentSpotLight.color.y >> currentSpotLight.color.z;
            else if (tag == "INTENSITY") iss >> currentSpotLight.intensity;
            else if (tag == "RANGE") iss >> currentSpotLight.range;
            else if (tag == "INNER_ANGLE") iss >> currentSpotLight.innerAngle;
            else if (tag == "OUTER_ANGLE") iss >> currentSpotLight.outerAngle;
            else if (tag == "ENABLED") { int en; iss >> en; currentSpotLight.enabled = (en != 0); }
        } else if (inEntity) {
            if (tag == "NAME") std::getline(iss >> std::ws, currentEntity.name);
            else if (tag == "FILEPATH") std::getline(iss >> std::ws, currentEntity.filepath);
            else if (tag == "PRIMITIVE") iss >> currentEntity.primitiveType;
            else if (tag == "POS") iss >> currentEntity.position.x >> currentEntity.position.y >> currentEntity.position.z;
            else if (tag == "ROT") iss >> currentEntity.rotation.x >> currentEntity.rotation.y >> currentEntity.rotation.z;
            else if (tag == "SCALE") iss >> currentEntity.scale.x >> currentEntity.scale.y >> currentEntity.scale.z;
            else if (tag == "VISIBLE") { int v; iss >> v; currentEntity.visible = (v != 0); }
            else if (tag == "COLLISION") { int c; iss >> c; currentEntity.hasCollision = (c != 0); }
            else if (tag == "PRESET") iss >> currentEntity.materialPreset;
            else if (tag == "MAT_ALBEDO") iss >> currentEntity.albedo.x >> currentEntity.albedo.y >> currentEntity.albedo.z;
            else if (tag == "MAT_ROUGHNESS") iss >> currentEntity.roughness;
            else if (tag == "MAT_METALLIC") iss >> currentEntity.metallic;
            else if (tag == "MAT_REFLECTANCE") iss >> currentEntity.reflectance;
            else if (tag == "MAT_EMISSION") iss >> currentEntity.emission;
            else if (tag == "MAT_IOR") iss >> currentEntity.ior;
            else if (tag == "UV_TILING") iss >> currentEntity.uvTiling.x >> currentEntity.uvTiling.y;
            else if (tag == "ALBEDO_PATH") std::getline(iss >> std::ws, currentEntity.albedoPath);
            else if (tag == "NORMAL_PATH") std::getline(iss >> std::ws, currentEntity.normalPath);
            else if (tag == "RMA_PATH") std::getline(iss >> std::ws, currentEntity.rmaPath);
            else if (tag == "AO_PATH") std::getline(iss >> std::ws, currentEntity.aoPath);
            else if (tag == "EMISSIVE_PATH") std::getline(iss >> std::ws, currentEntity.emissivePath);
            else if (EnginePhysics::ReadCollider(tag, iss, currentEntity.collider)) {}
            else if (EnginePhysics::ReadRagdoll(tag, iss, currentEntity.ragdollSettings)) {}
            else if (EnginePhysics::ReadVehicle(tag, iss, currentEntity.vehicle)) {}
            else if (tag == "PHYSICS_BODY") iss >> currentEntity.physicsBodyType;
            else if (tag == "PHYSICS_SHAPE") iss >> currentEntity.physicsShapeType;
            else if (tag == "PHYSICS_MASS") iss >> currentEntity.physicsMass;
            else if (tag == "PHYSICS_DAMPING") iss >> currentEntity.physicsLinearDamping >> currentEntity.physicsAngularDamping;
            else if (tag == "PHYSICS_FRICTION") iss >> currentEntity.physicsFriction;
            else if (tag == "PHYSICS_RESTITUTION") iss >> currentEntity.physicsRestitution;
            else if (tag == "PHYSICS_GRAVITY") { int g; iss >> g; currentEntity.physicsEnableGravity = (g != 0); }
            else if (tag == "PHYSICS_CCD") { int ccd; iss >> ccd; currentEntity.physicsEnableCCD = (ccd != 0); }
        }
    }

    return true;
}

LevelSystem& LevelSystem::Get() {
    static LevelSystem s_instance;
    return s_instance;
}

std::filesystem::path LevelSystem::GetProjectRootDir() {
    if(EngineProject::active)return EngineProject::current.Root();
    namespace fs = std::filesystem;
    std::error_code ec;

#ifdef _WIN32
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    fs::path exeDir = fs::path(exePath).parent_path();
#else
    char exePath[1024] = {};
    ssize_t count = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    fs::path exeDir = (count > 0) ? fs::path(std::string(exePath, count)).parent_path() : fs::current_path();
#endif

    std::vector<fs::path> candidates = {
        exeDir.parent_path(),
        exeDir,
        fs::current_path(),
        "d:/MyEngine"
    };

    for (const auto& c : candidates) {
        if (c.empty()) continue;
        if (fs::exists(c / "src" / "Content", ec) || fs::exists(c / "build.bat", ec)) {
            return fs::canonical(c, ec);
        }
    }

    std::string folderName = exeDir.filename().string();
    if (folderName == "EditorBuild" || folderName == "ReleaseBuild" ||
        folderName == "ServerBuild" || folderName == "ServerBuild_Linux") {
        return exeDir.parent_path();
    }

    return exeDir.parent_path();
}

std::filesystem::path LevelSystem::GetServerMapsDirectory() {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path mapsDir = GetProjectRootDir() / "ServerMaps";
    if (!fs::exists(mapsDir, ec)) {
        fs::create_directories(mapsDir, ec);
    }
    return mapsDir;
}

std::string LevelSystem::GetLevelsDirectory() {
    namespace fs = std::filesystem;
    std::error_code ec;

    fs::path root = GetProjectRootDir();
    fs::path target = EngineProject::active ? EngineProject::current.Resolve(EngineProject::current.content)/"Levels" : root / "Content" / "Levels";
#ifndef ENGINE_RELEASE
    if (!fs::exists(target, ec)) {
        fs::create_directories(target, ec);
    }
#endif
    return LevelString(target);
}

void LevelSystem::Initialize() {
    namespace fs = std::filesystem;
    std::error_code ec;

    std::string levelsDir = GetLevelsDirectory();
#ifndef ENGINE_RELEASE
    fs::create_directories(LevelPath(levelsDir), ec);

    // If main.level doesn't exist anywhere, create a default starting level
    std::string mainPath = levelsDir + "/main.level";
    if (!fs::exists(LevelPath(mainPath), ec)) {
        LevelData defaultLevel;
        defaultLevel.levelName = "main";
        defaultLevel.author = "Developer";
        defaultLevel.description = "Default Starter Level";

        // Add a default Floor
        LevelEntity floor;
        floor.name = "Floor";
        floor.primitiveType = 3;
        floor.position = { 0.0f, 0.0f, 0.0f };
        floor.scale = { 1.0f, 1.0f, 1.0f };
        floor.materialPreset = 9; // Concrete
        floor.roughness = 0.85f;
        defaultLevel.entities.push_back(floor);

        LevelEntity walls;
        walls.name = "House Walls";
        walls.position = { 0.0f, 0.0f, 8.0f };
        walls.albedo = { 0.88f, 0.85f, 0.80f };
        walls.roughness = 0.65f;
        defaultLevel.entities.push_back(walls);
        LevelEntity roof;
        roof.name = "House Roof";
        roof.position = walls.position;
        roof.albedo = { 0.75f, 0.28f, 0.18f };
        roof.roughness = 0.50f;
        defaultLevel.entities.push_back(roof);
        std::ofstream file(LevelPath(mainPath));
        if (file.is_open()) {
            file << defaultLevel.Serialize();
        }
    }
#endif
}

std::string LevelSystem::ResolveLevelPath(const std::string& levelNameOrPath, bool& outInPak) {
    outInPak = false;
    namespace fs = std::filesystem;
    std::error_code ec;

    // Check directly if it's already an existing file path
    fs::path p=LevelPath(levelNameOrPath);
    if (fs::exists(p, ec) && fs::is_regular_file(p, ec)) {
        return LevelString(fs::canonical(p, ec));
    }

    std::string name = levelNameOrPath;
    if (name.size() < 6 || name.substr(name.size() - 6) != ".level") {
        name += ".level";
    }

    fs::path root = GetProjectRootDir();
    std::vector<fs::path> candidates = {
        p,
        root / "src" / "Content" / "Levels" / LevelPath(name),
        root / "src" / "Content" / "Levels" / LevelPath(levelNameOrPath),
        root / "Content" / "Levels" / LevelPath(name),
        root / "Contents" / "Levels" / LevelPath(name),
        root / "Levels" / LevelPath(name),
        root / "worlds" / LevelPath(name),
        fs::current_path() / LevelPath(name)
    };

    for (const auto& c : candidates) {
        if (fs::exists(c, ec) && fs::is_regular_file(c, ec)) {
            return LevelString(fs::canonical(c, ec));
        }
    }

    // PAK archive check fallback
    outInPak = true;
    return LevelString(root / "src" / "Content" / "Levels" / LevelPath(name));
}

void LevelSystem::ApplyLevelToScene(Renderer* renderer, Camera* camera) {
#ifndef ENGINE_SERVER
    if (renderer) {
        auto& models = renderer->GetModels();
        models.clear();

        auto& dynModels = renderer->GetDynamicModels();
        dynModels.clear();

        renderer->SetLightDir(m_currentLevel.lightDir);
        renderer->SetLightColor(m_currentLevel.lightColor);
        renderer->SetSkyColor(m_currentLevel.skyColor);

        // Apply local point and spot lights
        renderer->ClearLocalLights();
        for (const auto& pl : m_currentLevel.pointLights) {
            renderer->AddPointLight(pl);
        }
        for (const auto& sl : m_currentLevel.spotLights) {
            renderer->AddSpotLight(sl);
        }

        for (const auto& e : m_currentLevel.entities) {
            if (!e.filepath.empty()) {
                renderer->SpawnDynamicRenderModel(e.filepath.c_str(), e.position);
                if (!dynModels.empty()) {
                    auto& dm = dynModels.back();
                    dm.name = e.name;
                    dm.rotation = e.rotation;
                    dm.scale = e.scale;
                    dm.visible = e.visible;
                    dm.hasCollision = e.hasCollision;
                    dm.material.preset = static_cast<Renderer::MaterialPreset>(e.materialPreset);
                    dm.material.albedo = e.albedo;
                    dm.material.roughness = e.roughness;
                    dm.material.metallic = e.metallic;
                    dm.material.reflectance = e.reflectance;
                    dm.material.emission = e.emission;
                    dm.material.ior = e.ior;
                    dm.material.uvTiling = e.uvTiling;
                    dm.material.albedoPath = e.albedoPath;
                    dm.material.normalPath = e.normalPath;
                    dm.material.rmaPath = e.rmaPath;
                    dm.material.aoPath = e.aoPath;
                    dm.material.emissivePath = e.emissivePath;
                    dm.collider = e.collider; dm.ragdollSettings = e.ragdollSettings;dm.vehicle=e.vehicle;
                    dm.physicsBodyType = e.physicsBodyType;
                    dm.physicsShapeType = e.physicsShapeType;
                    dm.physicsMass = e.physicsMass; dm.physicsLinearDamping = e.physicsLinearDamping; dm.physicsAngularDamping = e.physicsAngularDamping;
                    dm.physicsFriction = e.physicsFriction;
                    dm.physicsRestitution = e.physicsRestitution;
                    dm.physicsEnableGravity = e.physicsEnableGravity;
                    dm.physicsEnableCCD = e.physicsEnableCCD;
                }
            } else {
                auto geoInfo = renderer->GetPrimitiveGeometry(e.name, e.primitiveType);
                if (geoInfo.valid && geoInfo.indexCount > 0) {
                    models.emplace_back(e.name, geoInfo.indexCount, geoInfo.startIndex, geoInfo.baseVertex, geoInfo.vertexCount);
                    auto& m = models.back();
                    m.numLODs = geoInfo.numLODs;
                    for (uint32_t l = 0; l < geoInfo.numLODs; ++l) {
                        m.lods[l] = geoInfo.lods[l];
                    }
                    m.position = e.position;
                    m.rotation = e.rotation;
                    m.scale = e.scale;
                    m.visible = e.visible;
                    m.hasCollision = e.hasCollision;
                    m.material.preset = static_cast<Renderer::MaterialPreset>(e.materialPreset);
                    m.material.albedo = e.albedo;
                    m.material.roughness = e.roughness;
                    m.material.metallic = e.metallic;
                    m.material.reflectance = e.reflectance;
                    m.material.emission = e.emission;
                    m.material.ior = e.ior;
                    m.material.uvTiling = e.uvTiling;
                    m.material.albedoPath = e.albedoPath;
                    m.material.normalPath = e.normalPath;
                    m.material.rmaPath = e.rmaPath;
                    m.material.aoPath = e.aoPath;
                    m.material.emissivePath = e.emissivePath;
                    m.collider = e.collider; m.ragdollSettings = e.ragdollSettings;m.vehicle=e.vehicle;
                    m.physicsBodyType = e.physicsBodyType;
                    m.physicsShapeType = e.physicsShapeType;
                    m.physicsMass = e.physicsMass; m.physicsLinearDamping = e.physicsLinearDamping; m.physicsAngularDamping = e.physicsAngularDamping;
                    m.physicsFriction = e.physicsFriction;
                    m.physicsRestitution = e.physicsRestitution;
                    m.physicsEnableGravity = e.physicsEnableGravity;
                    m.physicsEnableCCD = e.physicsEnableCCD;
                }
            }
        }

        // Build meshlet ranges for all loaded models so mesh shaders have valid buffers
        renderer->UpdateModelMeshletRanges();
        // Invalidate raytracing scene to trigger safe BLAS/TLAS rebuild next frame
        renderer->InvalidateRaytracingScene();
    }

    if (camera) {
        camera->SetPosition(m_currentLevel.spawnPosition);
        camera->SetYaw(m_currentLevel.spawnYaw);
    }
#else
    (void)renderer;
    (void)camera;
#endif
}

bool LevelSystem::LoadLevel(const std::string& levelNameOrPath, Renderer* renderer, Camera* camera) {
    if (levelNameOrPath.size() >= 4 && levelNameOrPath.substr(levelNameOrPath.size() - 4) == ".pak") {
        if (LoadWorldPak(levelNameOrPath, renderer, camera)) {
            return true;
        }
    }

    bool inPak = false;
    std::string resolved = ResolveLevelPath(levelNameOrPath, inPak);

    std::string levelText;
    if (!inPak) {
        std::ifstream file(LevelPath(resolved));
        if (file.is_open()) {
            std::stringstream ss;
            ss << file.rdbuf();
            levelText = ss.str();
            m_isLoadedFromPak = false;
            m_currentLevelPath = resolved;
        }
    }

#ifndef ENGINE_SERVER
    // If not found in loose files, try reading decrypted bytes from .PAK archives
    if (levelText.empty()) {
        std::wstring wpath=LevelPath(resolved).wstring();
        std::vector<uint8_t> pakBytes = Renderer::LoadFromPak(wpath);
        if (pakBytes.empty()) {
            // Also try with "Content/" prefix or "Levels/"
            std::string alt = "Content/Levels/" + levelNameOrPath;
            if (alt.size() < 6 || alt.substr(alt.size() - 6) != ".level") alt += ".level";
            std::wstring walt=LevelPath(alt).wstring();
            pakBytes = Renderer::LoadFromPak(walt);
        }

        if (!pakBytes.empty()) {
            levelText.assign((const char*)pakBytes.data(), pakBytes.size());
            m_isLoadedFromPak = true;
            m_currentLevelPath = resolved + " [Encrypted .PAK]";
        }
    }
#endif

    if (levelText.empty()) {
        std::cerr << "[LevelSystem] Failed to find or load level: " << levelNameOrPath << std::endl;
        m_statusMessage = "Error: Level file not found: " + levelNameOrPath;
        m_statusTimer = 5.0f;
        m_statusIsError = true;
        return false;
    }

    if (!m_currentLevel.Deserialize(levelText)) {
        std::cerr << "[LevelSystem] Failed to parse level data: " << levelNameOrPath << std::endl;
        m_statusMessage = "Error: Failed to parse level data!";
        m_statusTimer = 5.0f;
        m_statusIsError = true;
        return false;
    }

    ApplyLevelToScene(renderer, camera);

    std::cout << "[LevelSystem] Successfully loaded level: '" << m_currentLevel.levelName << "' ("
              << m_currentLevel.entities.size() << " entities) from " << m_currentLevelPath << std::endl;

    m_statusMessage = "Loaded level: " + m_currentLevel.levelName + " (" + std::to_string(m_currentLevel.entities.size()) + " entities)";
    m_statusTimer = 4.0f;
    m_statusIsError = false;
    return true;
}

void LevelSystem::RequestLoadLevel(const std::string& levelNameOrPath) {
    if (levelNameOrPath.empty()) return;
    m_pendingLevelName = levelNameOrPath;
    m_hasPendingLoad = true;
    std::cout << "[LevelSystem] Queued runtime map load for: " << levelNameOrPath << std::endl;
}

bool LevelSystem::ProcessPendingLoad(Renderer* renderer, Camera* camera) {
    if (!m_hasPendingLoad || m_pendingLevelName.empty()) return false;

    std::string target = m_pendingLevelName;
    m_hasPendingLoad = false;
    m_pendingLevelName.clear();

    std::cout << "[LevelSystem] Processing runtime map switch to: " << target << std::endl;

#ifndef ENGINE_SERVER
    // 1. Ensure GPU is completely idle before changing buffers
    if (renderer) {
        renderer->WaitForPreviousFrame();
    }

    // 2. Tear down previous physics actors cleanly to prevent ghost collisions or memory leaks
    EnginePhysics::PhysicsSystem::Get().ClearAllActors();
    EnginePhysics::ClothSystem::Get().ResetAllCloths();
    EnginePhysics::HairSystem::Get().ResetAll();
#endif

    // 3. Load and parse the new level
    bool success = LoadLevel(target, renderer, camera);

#ifndef ENGINE_SERVER
    if (success && renderer) {
        // 4. Synchronize fresh PhysX actors from scene entities
        EnginePhysics::PhysicsSystem::Get().SyncAllActorsFromScene(renderer);

        // 5. Invalidate Raytracing TLAS/BLAS
        renderer->InvalidateRaytracingScene();
    }
#endif

    if (success) {
        m_statusMessage = "Active Map: " + m_currentLevel.levelName + " (Switched Runtime)";
        m_statusTimer = 5.0f;
        m_statusIsError = false;
        std::cout << "[LevelSystem] Successfully switched to map '" << m_currentLevel.levelName << "' during gameplay!\n";
    } else {
        m_statusMessage = "Failed to load map: " + target;
        m_statusTimer = 5.0f;
        m_statusIsError = true;
    }

    return success;
}

void LevelSystem::CycleToNextLevel(Renderer* renderer, Camera* camera) {
    (void)renderer;
    (void)camera;
    auto levels = GetAvailableLevels();
    if (levels.empty()) return;

    size_t currentIdx = 0;
    for (size_t i = 0; i < levels.size(); ++i) {
        if (levels[i] == m_currentLevel.levelName) {
            currentIdx = i;
            break;
        }
    }

    size_t nextIdx = (currentIdx + 1) % levels.size();
    RequestLoadLevel(levels[nextIdx]);
}

void LevelSystem::SyncSceneToLevelData(Renderer* renderer, Camera* camera) {
#ifndef ENGINE_SERVER
    if (renderer) {
        m_currentLevel.entities.clear();
        m_currentLevel.lightDir = renderer->GetLightDir();
        m_currentLevel.lightColor = renderer->GetLightColor();
        m_currentLevel.skyColor = renderer->GetSkyColor();

        // Capture local lights
        m_currentLevel.pointLights = renderer->GetPointLights();
        m_currentLevel.spotLights = renderer->GetSpotLights();

        // Capture static models
        for (const auto& m : renderer->GetModels()) {
            LevelEntity e;
            e.name = m.name;
            std::string lower = m.name;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c){ return (char)::tolower(c); });
            if (lower.find("floor") != std::string::npos) e.primitiveType = 3;
            else if (lower.find("cube") != std::string::npos) e.primitiveType = 0;
            else if (lower.find("sphere") != std::string::npos) e.primitiveType = 1;
            else if (lower.find("plane") != std::string::npos) e.primitiveType = 2;
            else if (lower.find("triangle") != std::string::npos) e.primitiveType = 4;
            else e.primitiveType = 0;
            e.position = m.position;
            e.rotation = m.rotation;
            e.scale = m.scale;
            e.visible = m.visible;
            e.hasCollision = m.hasCollision;
            e.materialPreset = static_cast<int>(m.material.preset);
            e.albedo = m.material.albedo;
            e.roughness = m.material.roughness;
            e.metallic = m.material.metallic;
            e.reflectance = m.material.reflectance;
            e.emission = m.material.emission;
            e.ior = m.material.ior;
            e.uvTiling = m.material.uvTiling;
            e.albedoPath = m.material.albedoPath;
            e.normalPath = m.material.normalPath;
            e.rmaPath = m.material.rmaPath;
            e.aoPath = m.material.aoPath;
            e.emissivePath = m.material.emissivePath;
            e.collider = m.collider; e.ragdollSettings = m.ragdollSettings;e.vehicle=m.vehicle;
            e.physicsBodyType = m.physicsBodyType;
            e.physicsShapeType = m.physicsShapeType;
            e.physicsMass = m.physicsMass; e.physicsLinearDamping = m.physicsLinearDamping; e.physicsAngularDamping = m.physicsAngularDamping;
            e.physicsFriction = m.physicsFriction;
            e.physicsRestitution = m.physicsRestitution;
            e.physicsEnableGravity = m.physicsEnableGravity;
            e.physicsEnableCCD = m.physicsEnableCCD;
            m_currentLevel.entities.push_back(e);
        }

        // Capture dynamic models
        for (const auto& dm : renderer->GetDynamicModels()) {
            LevelEntity e;
            e.name = dm.name;
            e.filepath = dm.filepath;
            e.position = dm.position;
            e.rotation = dm.rotation;
            e.scale = dm.scale;
            e.visible = dm.visible;
            e.hasCollision = dm.hasCollision;
            e.materialPreset = static_cast<int>(dm.material.preset);
            e.albedo = dm.material.albedo;
            e.roughness = dm.material.roughness;
            e.metallic = dm.material.metallic;
            e.reflectance = dm.material.reflectance;
            e.emission = dm.material.emission;
            e.ior = dm.material.ior;
            e.uvTiling = dm.material.uvTiling;
            e.albedoPath = dm.material.albedoPath; e.normalPath = dm.material.normalPath;
            e.rmaPath = dm.material.rmaPath; e.aoPath = dm.material.aoPath; e.emissivePath = dm.material.emissivePath;
            e.collider = dm.collider; e.ragdollSettings = dm.ragdollSettings;e.vehicle=dm.vehicle;
            e.physicsBodyType = dm.physicsBodyType; e.physicsShapeType = dm.physicsShapeType;
            e.physicsMass = dm.physicsMass; e.physicsLinearDamping = dm.physicsLinearDamping; e.physicsAngularDamping = dm.physicsAngularDamping; e.physicsFriction = dm.physicsFriction; e.physicsRestitution = dm.physicsRestitution;
            e.physicsEnableGravity = dm.physicsEnableGravity; e.physicsEnableCCD = dm.physicsEnableCCD;
            m_currentLevel.entities.push_back(e);
        }
    }

    if (camera) {
        m_currentLevel.spawnPosition = camera->GetPosition();
        m_currentLevel.spawnYaw = camera->GetYaw();
    }
#else
    (void)renderer;
    (void)camera;
#endif
}

bool LevelSystem::SaveLevel(const std::string& levelNameOrPath, Renderer* renderer, Camera* camera) {
    SyncSceneToLevelData(renderer, camera);

    namespace fs = std::filesystem;
    std::error_code ec;

    std::string name = levelNameOrPath;
    if (name.empty()) name = m_currentLevel.levelName;
    if (name.empty()) name = "main";

    if (name.size() < 6 || name.substr(name.size() - 6) != ".level") {
        name += ".level";
    }

    std::string levelsDir = GetLevelsDirectory();
    std::string savePath = levelsDir + "/" + name;
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
        savePath = name;
    }

    fs::create_directories(LevelPath(savePath).parent_path(), ec);

    std::ofstream file(LevelPath(savePath));
    if (!file.is_open()) {
        std::cerr << "[LevelSystem] Failed to write level file: " << savePath << std::endl;
        m_statusMessage = "Error: Failed to save " + savePath;
        m_statusTimer = 5.0f;
        m_statusIsError = true;
        return false;
    }

    file << m_currentLevel.Serialize();
    m_currentLevelPath = savePath;
    m_isLoadedFromPak = false;

    std::cout << "[LevelSystem] Successfully saved level to: " << savePath << " ("
              << m_currentLevel.entities.size() << " entities)" << std::endl;

    m_statusMessage = "Saved level: " + m_currentLevel.levelName + " -> " + savePath;
    m_statusTimer = 4.0f;
    m_statusIsError = false;
    return true;
}

void LevelSystem::NewLevel(Renderer* renderer, Camera* camera) {
    m_currentLevel = LevelData();
    m_currentLevel.levelName = "untitled";
    m_currentLevelPath = GetLevelsDirectory() + "/untitled.level";
    m_isLoadedFromPak = false;

#ifndef ENGINE_SERVER
    if (renderer) {
        auto& models = renderer->GetModels();
        models.clear();

        auto& dynModels = renderer->GetDynamicModels();
        dynModels.clear();

        // Default Floor
        auto geoInfo = renderer->GetPrimitiveGeometry("Floor", 3);
        if (geoInfo.valid && geoInfo.indexCount > 0) {
            models.emplace_back("Floor", geoInfo.indexCount, geoInfo.startIndex, geoInfo.baseVertex, geoInfo.vertexCount);
            auto& f = models.back();
            f.scale = { 50.0f, 1.0f, 50.0f };
            f.material.preset = Renderer::MaterialPreset::Concrete;
        }

        renderer->SetSkyColor({ 0.2f, 0.4f, 0.8f });
        renderer->SetLightDir({ 0.577f, -0.577f, 0.577f });
        renderer->SetLightColor({ 1.0f, 0.98f, 0.95f });
        renderer->UpdateModelMeshletRanges();
        renderer->InvalidateRaytracingScene();
    }

    if (camera) {
        camera->SetPosition({ 0.0f, 2.0f, -5.0f });
        camera->SetYaw(0.0f);
        camera->SetPitch(0.0f);
    }
#endif

    m_statusMessage = "Created new empty level.";
    m_statusTimer = 3.0f;
    m_statusIsError = false;
}

bool LevelSystem::LoadLevelFromMemory(const uint8_t* data, size_t size, Renderer* renderer, Camera* camera) {
    if (!data || size == 0) return false;
    std::string text((const char*)data, size);
    if (!m_currentLevel.Deserialize(text)) return false;

    ApplyLevelToScene(renderer, camera);

    m_isLoadedFromPak = true;
    return true;
}

std::vector<std::string> LevelSystem::GetAvailableLevels() {
    std::vector<std::string> levels;
    namespace fs = std::filesystem;
    std::error_code ec;

    std::string levelsDir = GetLevelsDirectory();
    std::vector<fs::path> searchDirs = {
        LevelPath(levelsDir),
        "src/Content/Levels",
        "../src/Content/Levels",
        "Content/Levels",
        "Contents/Levels",
        "Levels",
        "worlds"
    };

    for (const auto& dir : searchDirs) {
        if (fs::exists(dir, ec) && fs::is_directory(dir, ec)) {
            for (const auto& entry : fs::directory_iterator(dir, ec)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == ".level" || ext == ".map" || ext == ".pak") {
                        std::string stem = LevelString(entry.path().stem());
                        if (std::find(levels.begin(), levels.end(), stem) == levels.end()) {
                            levels.push_back(stem);
                        }
                    }
                }
            }
        }
    }

    if (std::find(levels.begin(), levels.end(), "main") == levels.end()) {
        levels.insert(levels.begin(), "main");
    }

    return levels;
}

bool LevelSystem::ExportWorldPak(const std::string& pakFilename,
                                const std::vector<std::string>& destinationFolders,
                                Renderer* renderer,
                                Camera* camera) {
    if (renderer || camera) {
        SyncSceneToLevelData(renderer, camera);
    }

    std::string levelText = m_currentLevel.Serialize();
    if (levelText.empty()) {
        m_statusMessage = "Error: Level data is empty, cannot export!";
        m_statusIsError = true;
        m_statusTimer = 4.0f;
        return false;
    }

    WorldPakHeader header;
    std::memset(&header, 0, sizeof(header));
    header.magic[0] = 'W';
    header.magic[1] = 'P';
    header.magic[2] = 'A';
    header.magic[3] = 'K';
    header.version = 1;
    std::snprintf(header.worldName, sizeof(header.worldName), "%s", m_currentLevel.levelName.c_str());
    std::snprintf(header.author, sizeof(header.author), "%s", m_currentLevel.author.c_str());
    header.entityCount = static_cast<uint32_t>(m_currentLevel.entities.size());
    header.spawnX = m_currentLevel.spawnPosition.x;
    header.spawnY = m_currentLevel.spawnPosition.y;
    header.spawnZ = m_currentLevel.spawnPosition.z;
    header.spawnYaw = m_currentLevel.spawnYaw;
    header.levelDataSize = static_cast<uint32_t>(levelText.size());
    header.flags = 0;
    uint32_t sum = 0;
    for (unsigned char c : levelText) sum += c;
    header.checksum = sum;

    std::vector<uint8_t> pakBuffer;
    pakBuffer.resize(sizeof(header) + levelText.size());
    std::memcpy(pakBuffer.data(), &header, sizeof(header));
    std::memcpy(pakBuffer.data() + sizeof(header), levelText.data(), levelText.size());

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path mapsDir = GetServerMapsDirectory();

    std::vector<fs::path> resolvedDestinations;
    if (destinationFolders.empty()) {
        resolvedDestinations.push_back(mapsDir); // D:\MyEngine\ServerMaps
        fs::path serverBuild = GetProjectRootDir() / "ServerBuild";
        if (fs::exists(serverBuild, ec)) resolvedDestinations.push_back(serverBuild);
        fs::path serverBuildLinux = GetProjectRootDir() / "ServerBuild_Linux";
        if (fs::exists(serverBuildLinux, ec)) resolvedDestinations.push_back(serverBuildLinux);
    } else {
        for (const auto& dir : destinationFolders) {
            fs::path p(dir);
            if (p.is_absolute()) {
                resolvedDestinations.push_back(p);
            } else if (dir == "ServerMaps" || dir == "." || dir.empty()) {
                resolvedDestinations.push_back(mapsDir);
            } else {
                resolvedDestinations.push_back(GetProjectRootDir() / dir);
            }
        }
    }

    std::string finalName = pakFilename.empty() ? (m_currentLevel.levelName + ".pak") : pakFilename;
    if (finalName.size() < 4 || finalName.substr(finalName.size() - 4) != ".pak") {
        finalName += ".pak";
    }

    int successCount = 0;
    for (const auto& dir : resolvedDestinations) {
        if (!fs::exists(dir, ec)) {
            fs::create_directories(dir, ec);
        }
        fs::path outPath = dir / finalName;
        std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
        if (out.is_open()) {
            out.write(reinterpret_cast<const char*>(pakBuffer.data()), pakBuffer.size());
            out.flush();
            out.close();
            successCount++;
            std::cout << "[LevelSystem] Exported " << finalName << " to: " << outPath.string()
                      << " (" << pakBuffer.size() << " bytes)\n";
        } else {
            std::cerr << "[LevelSystem] Warning: Failed to write " << finalName << " to: " << outPath.string() << std::endl;
        }
    }

    if (successCount > 0) {
        m_statusMessage = "Exported " + finalName + " -> D:/MyEngine/ServerMaps/" + finalName;
        m_statusIsError = false;
        m_statusTimer = 5.0f;
        return true;
    } else {
        m_statusMessage = "Error: Failed to write " + finalName + " to ServerMaps.";
        m_statusIsError = true;
        m_statusTimer = 5.0f;
        return false;
    }
}

bool LevelSystem::ExportLevelFileToWorldPak(const std::string& levelFilePath, const std::string& pakFilename) {
    namespace fs = std::filesystem;
    std::error_code ec;

    fs::path inPath(levelFilePath);
    if (!fs::exists(inPath, ec) || !fs::is_regular_file(inPath, ec)) {
        bool inPak = false;
        std::string resolved = ResolveLevelPath(levelFilePath, inPak);
        inPath = fs::path(resolved);
        if (!fs::exists(inPath, ec) || !fs::is_regular_file(inPath, ec)) {
            std::cerr << "[LevelSystem] Level file not found for export: " << levelFilePath << std::endl;
            m_statusMessage = "Error: Level file not found: " + levelFilePath;
            m_statusIsError = true;
            m_statusTimer = 4.0f;
            return false;
        }
    }

    std::ifstream in(inPath);
    if (!in.is_open()) {
        m_statusMessage = "Error: Cannot open " + inPath.string();
        m_statusIsError = true;
        m_statusTimer = 4.0f;
        return false;
    }

    std::stringstream ss;
    ss << in.rdbuf();
    std::string levelText = ss.str();
    in.close();

    if (levelText.empty()) {
        m_statusMessage = "Error: Level data is empty: " + inPath.string();
        m_statusIsError = true;
        m_statusTimer = 4.0f;
        return false;
    }

    LevelData tempLevel;
    if (!tempLevel.Deserialize(levelText)) {
        tempLevel.levelName = inPath.stem().string();
        tempLevel.author = "Developer";
    }

    std::string finalName = pakFilename;
    if (finalName.empty()) {
        finalName = tempLevel.levelName + ".pak";
    }
    if (finalName.size() < 4 || finalName.substr(finalName.size() - 4) != ".pak") {
        finalName += ".pak";
    }

    WorldPakHeader header;
    std::memset(&header, 0, sizeof(header));
    header.magic[0] = 'W';
    header.magic[1] = 'P';
    header.magic[2] = 'A';
    header.magic[3] = 'K';
    header.version = 1;
    std::snprintf(header.worldName, sizeof(header.worldName), "%s", tempLevel.levelName.c_str());
    std::snprintf(header.author, sizeof(header.author), "%s", tempLevel.author.c_str());
    header.entityCount = static_cast<uint32_t>(tempLevel.entities.size());
    header.spawnX = tempLevel.spawnPosition.x;
    header.spawnY = tempLevel.spawnPosition.y;
    header.spawnZ = tempLevel.spawnPosition.z;
    header.spawnYaw = tempLevel.spawnYaw;
    header.levelDataSize = static_cast<uint32_t>(levelText.size());
    header.flags = 0;
    uint32_t sum = 0;
    for (unsigned char c : levelText) sum += c;
    header.checksum = sum;

    std::vector<uint8_t> pakBuffer;
    pakBuffer.resize(sizeof(header) + levelText.size());
    std::memcpy(pakBuffer.data(), &header, sizeof(header));
    std::memcpy(pakBuffer.data() + sizeof(header), levelText.data(), levelText.size());

    fs::path mapsDir = GetServerMapsDirectory();
    std::vector<fs::path> targetDirs = { mapsDir };
    fs::path serverBuild = GetProjectRootDir() / "ServerBuild";
    if (fs::exists(serverBuild, ec)) targetDirs.push_back(serverBuild);
    fs::path serverBuildLinux = GetProjectRootDir() / "ServerBuild_Linux";
    if (fs::exists(serverBuildLinux, ec)) targetDirs.push_back(serverBuildLinux);

    int successCount = 0;
    for (const auto& dir : targetDirs) {
        fs::path outPath = dir / finalName;
        std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
        if (out.is_open()) {
            out.write(reinterpret_cast<const char*>(pakBuffer.data()), pakBuffer.size());
            out.flush();
            out.close();
            successCount++;
            std::cout << "[LevelSystem] Exported " << finalName << " to: " << outPath.string()
                      << " (" << pakBuffer.size() << " bytes)\n";
        }
    }

    if (successCount > 0) {
        m_statusMessage = "Exported " + tempLevel.levelName + " -> ServerMaps/" + finalName;
        m_statusIsError = false;
        m_statusTimer = 5.0f;
        return true;
    } else {
        std::cerr << "[LevelSystem] Warning: Failed to write " << finalName << " to: ServerMaps" << std::endl;
        m_statusMessage = "Error: Failed to write " + finalName + " to ServerMaps";
        m_statusIsError = true;
        m_statusTimer = 5.0f;
        return false;
    }
}

bool LevelSystem::LoadWorldPak(const std::string& pakFilePath, Renderer* renderer, Camera* camera) {
    namespace fs = std::filesystem;
    std::error_code ec;
    std::string actualPath = pakFilePath;
    if (!fs::exists(LevelPath(actualPath), ec)) {
        fs::path root = GetProjectRootDir();
        fs::path mapsDir = GetServerMapsDirectory();
        std::string pakWithExt = pakFilePath;
        if (pakWithExt.size() < 4 || pakWithExt.substr(pakWithExt.size() - 4) != ".pak") {
            pakWithExt += ".pak";
        }

        std::vector<std::string> candidates = {
            LevelString(mapsDir / LevelPath(pakFilePath)),
            LevelString(mapsDir / LevelPath(pakWithExt)),
            "ServerMaps/" + pakFilePath,
            "ServerMaps/" + pakWithExt,
            "../ServerMaps/" + pakFilePath,
            "../ServerMaps/" + pakWithExt,
            pakFilePath,
            "./" + pakFilePath,
            pakWithExt,
            "./" + pakWithExt,
            "../" + pakFilePath,
            "Contents/" + pakFilePath,
            "../Contents/" + pakFilePath,
            "src/Content/Levels/" + pakFilePath,
            "../src/Content/Levels/" + pakFilePath
        };
        for (const auto& c : candidates) {
            if (fs::exists(LevelPath(c), ec) && fs::is_regular_file(LevelPath(c), ec)) {
                actualPath = c;
                break;
            }
        }
    }

    std::ifstream in(LevelPath(actualPath), std::ios::binary);
    if (!in.is_open()) return false;

    in.seekg(0, std::ios::end);
    size_t fileSize = static_cast<size_t>(in.tellg());
    in.seekg(0, std::ios::beg);

    if (fileSize >= sizeof(WorldPakHeader)) {
        WorldPakHeader header;
        in.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (header.magic[0] == 'W' && header.magic[1] == 'P' && header.magic[2] == 'A' && header.magic[3] == 'K') {
            if (header.levelDataSize > 0 && sizeof(header) + header.levelDataSize <= fileSize + 1024) {
                std::string levelText;
                levelText.resize(header.levelDataSize);
                in.read(&levelText[0], header.levelDataSize);

                if (LoadLevelFromMemory(reinterpret_cast<const uint8_t*>(levelText.data()), levelText.size(), renderer, camera)) {
                    m_currentLevelPath = actualPath + " [WPAK Archive]";
                    m_isLoadedFromPak = true;
                    std::cout << "[LevelSystem] Successfully loaded WPAK level: '" << m_currentLevel.levelName
                              << "' (" << m_currentLevel.entities.size() << " entities)\n";
                    return true;
                }
            }
        }
    }

    // Fallback: check if plain level text was renamed or saved as .pak
    in.seekg(0, std::ios::beg);
    std::string levelText((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (levelText.find("FORMAT") != std::string::npos || levelText.find("SPAWN") != std::string::npos) {
        if (LoadLevelFromMemory(reinterpret_cast<const uint8_t*>(levelText.data()), levelText.size(), renderer, camera)) {
            m_currentLevelPath = actualPath;
            m_isLoadedFromPak = true;
            std::cout << "[LevelSystem] Loaded level from plain text PAK: '" << m_currentLevel.levelName << "'\n";
            return true;
        }
    }

    return false;
}

void LevelSystem::DrawUI(Renderer* renderer, Camera* camera) {
#if !defined(ENGINE_RELEASE) && !defined(ENGINE_SERVER)
    if (!m_isOpen) return;

    ImGui::SetNextWindowSize(ImVec2(440, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(" Map & Level Editor", &m_isOpen)) {
        ImGui::End();
        return;
    }

    // Status Banner
    if (m_statusTimer > 0.0f) {
        ImVec4 col = m_statusIsError ? ImVec4(0.9f, 0.2f, 0.2f, 1.0f) : ImVec4(0.2f, 0.8f, 0.3f, 1.0f);
        ImGui::TextColored(col, "%s", m_statusMessage.c_str());
        ImGui::Separator();
    }

    // Header info
    ImGui::Text("Active Level: %s", m_currentLevel.levelName.c_str());
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Source: %s", m_currentLevelPath.c_str());
    if (m_isLoadedFromPak) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "[PAK Archive]");
    }
    ImGui::Separator();

    // Quick Action Buttons
    if (ImGui::Button(" New Level")) {
        NewLevel(renderer, camera);
    }
    ImGui::SameLine();
    if (ImGui::Button(" Save (.level)")) {
        SaveLevel(m_currentLevel.levelName, renderer, camera);
    }
    ImGui::SameLine();
    if (ImGui::Button(" Load")) {
        RequestLoadLevel(m_currentLevel.levelName);
    }
    ImGui::SameLine();
    if (ImGui::Button(" Cycle Map (F7)")) {
        CycleToNextLevel(renderer, camera);
    }
    ImGui::SameLine();
    if (ImGui::Button(" Export to ServerMaps")) {
        ExportWorldPak(m_serverPakFilenameBuffer, {}, renderer, camera);
    }

    // Level selector combo
    auto available = GetAvailableLevels();
    if (!available.empty()) {
        int currentIdx = -1;
        std::vector<const char*> comboItems;
        for (size_t i = 0; i < available.size(); ++i) {
            comboItems.push_back(available[i].c_str());
            if (available[i] == m_currentLevel.levelName) currentIdx = (int)i;
        }

        if (ImGui::Combo("Select Level", &currentIdx, comboItems.data(), (int)comboItems.size())) {
            if (currentIdx >= 0 && currentIdx < (int)available.size()) {
                RequestLoadLevel(available[currentIdx]);
            }
        }
    }

    ImGui::Separator();

    // Level Properties Collapsible
    if (ImGui::CollapsingHeader("Level Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
        static std::string s_lastTrackedLevel;
        if (s_lastTrackedLevel != m_currentLevel.levelName) {
            strncpy_s(m_levelNameBuffer, m_currentLevel.levelName.c_str(), sizeof(m_levelNameBuffer) - 1);
            strncpy_s(m_authorBuffer, m_currentLevel.author.c_str(), sizeof(m_authorBuffer) - 1);
            s_lastTrackedLevel = m_currentLevel.levelName;
        }

        if (ImGui::InputText("Level Name", m_levelNameBuffer, sizeof(m_levelNameBuffer))) {
            m_currentLevel.levelName = m_levelNameBuffer;
            s_lastTrackedLevel = m_currentLevel.levelName;
        }

        if (ImGui::InputText("Author", m_authorBuffer, sizeof(m_authorBuffer))) {
            m_currentLevel.author = m_authorBuffer;
        }
    }

    // Player Spawn Settings
    if (ImGui::CollapsingHeader("Player Spawn Point", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Spawn Position", &m_currentLevel.spawnPosition.x, 0.1f);
        ImGui::SliderFloat("Spawn Yaw", &m_currentLevel.spawnYaw, -3.14159f, 3.14159f);

        if (camera) {
            if (ImGui::Button(" Set Spawn at Camera Position")) {
                m_currentLevel.spawnPosition = camera->GetPosition();
                m_currentLevel.spawnYaw = camera->GetYaw();
                m_statusMessage = "Spawn updated to current camera position!";
                m_statusTimer = 3.0f;
                m_statusIsError = false;
            }
            ImGui::SameLine();
            if (ImGui::Button(" Teleport to Spawn")) {
                camera->SetPosition(m_currentLevel.spawnPosition);
                camera->SetYaw(m_currentLevel.spawnYaw);
            }
        }
    }

    // Environment & Atmosphere
    if (ImGui::CollapsingHeader("Environment & Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::DragFloat3("Sun Direction", &m_currentLevel.lightDir.x, 0.01f, -1.0f, 1.0f)) {
            if (renderer) renderer->SetLightDir(m_currentLevel.lightDir);
        }
        if (ImGui::ColorEdit3("Sunlight Color", &m_currentLevel.lightColor.x)) {
            if (renderer) renderer->SetLightColor(m_currentLevel.lightColor);
        }
        if (ImGui::ColorEdit3("Sky / Ambient Color", &m_currentLevel.skyColor.x)) {
            if (renderer) renderer->SetSkyColor(m_currentLevel.skyColor);
        }
        ImGui::SliderFloat("Fog Density", &m_currentLevel.fogDensity, 0.0f, 0.05f, "%.4f");
    }

    // Local Light Sources (Point & Spot Lights)
    if (ImGui::CollapsingHeader(" Local Light Sources (Point & Spot Lights)", ImGuiTreeNodeFlags_DefaultOpen)) {
        std::vector<EngineRender::PointLight>& plList = renderer ? renderer->GetPointLights() : m_currentLevel.pointLights;
        std::vector<EngineRender::SpotLight>& slList = renderer ? renderer->GetSpotLights() : m_currentLevel.spotLights;

        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.4f, 1.0f), "Point Lights (%d / %d):", (int)plList.size(), (int)EngineRender::MAX_POINT_LIGHTS);
        ImGui::SameLine();
        if (ImGui::Button(" Add Point Light") && plList.size() < EngineRender::MAX_POINT_LIGHTS) {
            EngineRender::PointLight newPl;
            newPl.name = "PointLight_" + std::to_string(plList.size() + 1);
            if (camera) newPl.position = camera->GetPosition();
            else newPl.position = XMFLOAT3(0.0f, 2.0f, 0.0f);
            newPl.color = XMFLOAT3(1.0f, 0.85f, 0.6f);
            newPl.intensity = 3.0f;
            newPl.range = 12.0f;
            newPl.enabled = true;
            plList.push_back(newPl);
            m_currentLevel.pointLights = plList;
        }

        int removePlIdx = -1;
        for (int i = 0; i < (int)plList.size(); ++i) {
            auto& pl = plList[i];
            ImGui::PushID(i + 1000);
            char headerBuf[64];
            snprintf(headerBuf, sizeof(headerBuf), " %s##%d", pl.name.c_str(), i);
            if (ImGui::TreeNode(headerBuf)) {
                ImGui::Checkbox("Enabled", &pl.enabled);
                ImGui::SameLine();
                if (ImGui::Button(" Remove")) removePlIdx = i;

                ImGui::ColorEdit3("Color", &pl.color.x);
                ImGui::DragFloat3("Position", &pl.position.x, 0.1f);
                if (camera) {
                    ImGui::SameLine();
                    if (ImGui::Button(" To Cam")) pl.position = camera->GetPosition();
                }
                ImGui::SliderFloat("Intensity", &pl.intensity, 0.0f, 20.0f, "%.1f");
                ImGui::SliderFloat("Range (m)", &pl.range, 0.5f, 40.0f, "%.1f");
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (removePlIdx >= 0 && removePlIdx < (int)plList.size()) {
            plList.erase(plList.begin() + removePlIdx);
            m_currentLevel.pointLights = plList;
        }

        ImGui::Separator();

        ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "Spot Lights (%d / %d):", (int)slList.size(), (int)EngineRender::MAX_SPOT_LIGHTS);
        ImGui::SameLine();
        if (ImGui::Button(" Add Spot Light") && slList.size() < EngineRender::MAX_SPOT_LIGHTS) {
            EngineRender::SpotLight newSl;
            newSl.name = "SpotLight_" + std::to_string(slList.size() + 1);
            if (camera) {
                newSl.position = camera->GetPosition();
                newSl.direction = camera->GetForward();
            } else {
                newSl.position = XMFLOAT3(0.0f, 5.0f, 0.0f);
                newSl.direction = XMFLOAT3(0.0f, -1.0f, 0.0f);
            }
            newSl.color = XMFLOAT3(1.0f, 1.0f, 1.0f);
            newSl.intensity = 5.0f;
            newSl.range = 20.0f;
            newSl.innerAngle = 20.0f;
            newSl.outerAngle = 35.0f;
            newSl.enabled = true;
            slList.push_back(newSl);
            m_currentLevel.spotLights = slList;
        }

        int removeSlIdx = -1;
        for (int i = 0; i < (int)slList.size(); ++i) {
            auto& sl = slList[i];
            ImGui::PushID(i + 2000);
            char headerBuf[64];
            snprintf(headerBuf, sizeof(headerBuf), " %s##%d", sl.name.c_str(), i);
            if (ImGui::TreeNode(headerBuf)) {
                ImGui::Checkbox("Enabled", &sl.enabled);
                ImGui::SameLine();
                if (ImGui::Button(" Remove")) removeSlIdx = i;

                ImGui::ColorEdit3("Color", &sl.color.x);
                ImGui::DragFloat3("Position", &sl.position.x, 0.1f);
                if (camera) {
                    ImGui::SameLine();
                    if (ImGui::Button(" To Cam")) sl.position = camera->GetPosition();
                }
                ImGui::DragFloat3("Direction", &sl.direction.x, 0.02f, -1.0f, 1.0f);
                if (camera) {
                    ImGui::SameLine();
                    if (ImGui::Button(" Cam Dir")) sl.direction = camera->GetForward();
                }
                ImGui::SliderFloat("Intensity", &sl.intensity, 0.0f, 30.0f, "%.1f");
                ImGui::SliderFloat("Range (m)", &sl.range, 1.0f, 60.0f, "%.1f");
                ImGui::SliderFloat("Inner Angle (deg)", &sl.innerAngle, 2.0f, 75.0f, "%.1f");
                ImGui::SliderFloat("Outer Angle (deg)", &sl.outerAngle, sl.innerAngle, 85.0f, "%.1f");
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (removeSlIdx >= 0 && removeSlIdx < (int)slList.size()) {
            slList.erase(slList.begin() + removeSlIdx);
            m_currentLevel.spotLights = slList;
        }

        ImGui::Spacing();
        if (ImGui::Button(" Cyberpunk Neon Set (Demo)")) {
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
            sl1.name = "High-Intensity Arena Spot";
            sl1.position = XMFLOAT3(0.0f, 9.0f, 0.0f);
            sl1.direction = XMFLOAT3(0.0f, -1.0f, 0.0f);
            sl1.color = XMFLOAT3(1.0f, 0.95f, 0.9f);
            sl1.intensity = 6.0f;
            sl1.range = 28.0f;
            sl1.innerAngle = 25.0f;
            sl1.outerAngle = 42.0f;
            sl1.enabled = true;
            slList.push_back(sl1);

            m_currentLevel.pointLights = plList;
            m_currentLevel.spotLights = slList;
        }
    }

    // Entities in Level Summary
    if (ImGui::CollapsingHeader("Level Entities")) {
        ImGui::Text("Total Objects in Level: %d", (int)m_currentLevel.entities.size());
        if (renderer) {
            ImGui::Text("Scene Models: %d | Dynamic Models: %d",
                        (int)renderer->GetModels().size(),
                        (int)renderer->GetDynamicModels().size());
        }
    }

    // Dedicated Server Maps Section
    if (ImGui::CollapsingHeader(" Dedicated Server Maps (ServerMaps/*.pak)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextColored(ImVec4(0.2f, 0.85f, 1.0f, 1.0f), "Build Server World Archive (.pak):");
        ImGui::InputText("PAK Filename", m_serverPakFilenameBuffer, sizeof(m_serverPakFilenameBuffer));

        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "Target Directory: D:/MyEngine/ServerMaps/");

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.55f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.7f, 0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.45f, 0.2f, 1.0f));

        if (ImGui::Button("Build .pak into ServerMaps", ImVec2(-1, 32))) {
            ExportWorldPak(m_serverPakFilenameBuffer, {}, renderer, camera);
        }
        ImGui::PopStyleColor(3);

        ImGui::TextDisabled("When server.cfg has 'world=world' (or world=world1), the server loads ServerMaps/<world>.pak automatically.");
    }

    ImGui::End();
#else
    (void)renderer;
    (void)camera;
#endif
}

} // namespace EngineLevel
