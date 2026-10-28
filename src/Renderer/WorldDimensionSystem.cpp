#include "WorldDimensionSystem.h"
#include "Renderer.h"
#include "Camera.h"
#include "PortalSystem.h"
#include <cmath>
#include <algorithm>

#ifndef ENGINE_RELEASE
#include "imgui.h"
#endif

using namespace DirectX;
using RenderModel = Renderer::RenderModel;

WorldDimensionSystem& WorldDimensionSystem::Get() {
    static WorldDimensionSystem instance;
    return instance;
}

void WorldDimensionSystem::Initialize(Renderer* renderer, Camera* camera) {
    if (!renderer || !camera) return;

    m_stage = DimensionStage::World1_Surface;
    m_bannerTimer = 0.0f;
    camera->SetFloorY(0.0f);
    camera->SetKillPlaneY(-500.0f);

    // 1. Register World 1 initial surface models
    m_world1ModelNames = { "Floor", "House Walls", "House Roof" };

    // 2. Spawn preliminary subterranean chunk of World 2 under the map
    SpawnUnderworldInitialChunk(renderer);

    // 3. Place Portal A on surface right in front of player spawn
    PortalSystem::Get().PlacePortal(PortalID::Blue_A, 
        XMFLOAT3(0.0f, 1.2f, 4.5f), 
        XMFLOAT3(0.0f, 0.0f, -1.0f));

    // 4. Place Portal B inside the subterranean Underworld chunk
    PortalSystem::Get().PlacePortal(PortalID::Orange_B, 
        XMFLOAT3(0.0f, m_underworldY + 1.2f, 4.5f), 
        XMFLOAT3(0.0f, 0.0f, -1.0f));

    m_initialized = true;
}

void WorldDimensionSystem::SpawnUnderworldInitialChunk(Renderer* renderer) {
    if (!renderer) return;

    auto& models = renderer->GetModels();
    auto geomPlane = renderer->GetPrimitiveGeometry("Plane");
    auto geomCube = renderer->GetPrimitiveGeometry("Cube");

    // A. Subterranean Floating Obsidian Island / Platform
    if (geomPlane.valid) {
        RenderModel platform("Underworld_Chunk_Platform", geomPlane.indexCount, geomPlane.startIndex, geomPlane.baseVertex, geomPlane.vertexCount);
        platform.numLODs = geomPlane.numLODs;
        for (uint32_t i = 0; i < geomPlane.numLODs; ++i) platform.lods[i] = geomPlane.lods[i];
        platform.position = XMFLOAT3(0.0f, m_underworldY, 4.5f);
        platform.scale = XMFLOAT3(24.0f, 1.0f, 24.0f);
        platform.rotation = XMFLOAT3(0.0f, 0.0f, 0.0f);
        platform.material.albedo = XMFLOAT3(0.08f, 0.03f, 0.14f); // Dark obsidian violet
        platform.material.roughness = 0.20f;
        platform.material.metallic = 0.85f;
        platform.material.reflectance = 0.10f;
        platform.visible = true;
        platform.hasCollision = true;
        models.push_back(platform);
        m_world2ModelNames.push_back(platform.name);
    }

    // B. Glowing Alien Crystal Pillars at the corners of the platform
    if (geomCube.valid) {
        float offsets[4][2] = {
            { -9.0f, -6.0f },
            {  9.0f, -6.0f },
            { -9.0f, 15.0f },
            {  9.0f, 15.0f }
        };

        for (int i = 0; i < 4; ++i) {
            char buf[64];
            sprintf_s(buf, "Underworld_Chunk_Pillar_%d", i + 1);
            RenderModel pillar(buf, geomCube.indexCount, geomCube.startIndex, geomCube.baseVertex, geomCube.vertexCount);
            pillar.numLODs = geomCube.numLODs;
            for (uint32_t l = 0; l < geomCube.numLODs; ++l) pillar.lods[l] = geomCube.lods[l];
            pillar.position = XMFLOAT3(offsets[i][0], m_underworldY + 3.5f, offsets[i][1]);
            pillar.scale = XMFLOAT3(1.2f, 7.0f, 1.2f);
            pillar.rotation = XMFLOAT3(0.0f, 15.0f * i, 0.0f);
            pillar.material.albedo = XMFLOAT3(0.1f, 0.85f, 0.95f); // Glowing cyan crystal
            pillar.material.emission = 3.5f;
            pillar.material.roughness = 0.15f;
            pillar.material.metallic = 0.50f;
            models.push_back(pillar);
            m_world2ModelNames.push_back(pillar.name);
        }
    }

    renderer->UpdateModelMeshletRanges();
    renderer->InvalidateRaytracingScene();
}

void WorldDimensionSystem::Update(float dt, Renderer* renderer, Camera* camera) {
    if (m_bannerTimer > 0.0f) {
        m_bannerTimer -= dt;
    }
}

void WorldDimensionSystem::OnPlayerEnteredPortal(Renderer* renderer, Camera* camera) {
    if (m_stage == DimensionStage::World1_Surface) {
        TriggerTransitionToWorld2(renderer, camera);
    }
}

void WorldDimensionSystem::TriggerTransitionToWorld2(Renderer* renderer, Camera* camera) {
    if (!renderer || !camera) return;

    m_stage = DimensionStage::World2_Underworld;

    // 1. Immediately close and destroy both portals so the portal link vanishes
    PortalSystem::Get().ResetPortals();

    // 2. Adjust Camera ground physics to the Underworld elevation (-120m)
    camera->SetFloorY(m_underworldY);
    camera->SetKillPlaneY(m_underworldY - 400.0f);

    // 3. Make World 1 completely disappear
    UnloadWorld1(renderer);

    // 4. Fully expand and spawn the complete World 2 realm
    ExpandFullWorld2(renderer);

    // 5. Transform atmosphere to deep cosmic alien void
    renderer->SetSkyColor(XMFLOAT3(0.03f, 0.01f, 0.07f)); // Deep violet void
    renderer->SetLightColor(XMFLOAT3(0.95f, 0.28f, 0.52f)); // Magenta-crimson alien nebula star

    // 6. Trigger cinematic HUD banner
    m_bannerTitle = "PORTAL COLLAPSED - WORLD 2 (THE UNDERWORLD) FULLY MATERIALIZED";
    m_bannerSubtitle = "Surface Dimension (World 1) has dissolved. The portal is permanently sealed.";
    m_bannerTimer = 6.5f;
}

void WorldDimensionSystem::UnloadWorld1(Renderer* renderer) {
    if (!renderer) return;

    auto& models = renderer->GetModels();
    for (auto& m : models) {
        if (m.name == "Floor" || m.name == "House Walls" || m.name == "House Roof" ||
            m.name.find("House") != std::string::npos || m.name.find("Surface") != std::string::npos) {
            m.visible = false;
            m.hasCollision = false;
        }
    }

    // Also hide any dynamic models on the surface
    auto& dynModels = renderer->GetDynamicModels();
    for (auto& dm : dynModels) {
        if (dm.position.y > -50.0f) {
            dm.visible = false;
            dm.hasCollision = false;
        }
    }

    renderer->UpdateModelMeshletRanges();
    renderer->InvalidateRaytracingScene();
}

void WorldDimensionSystem::EnsureInitialized(Renderer* renderer, Camera* camera) {
    if (!renderer || !camera) return;

    auto& models = renderer->GetModels();
    bool hasChunkPlatform = false;
    for (const auto& m : models) {
        if (m.name == "Underworld_Chunk_Platform") {
            hasChunkPlatform = true;
            break;
        }
    }
    if (!hasChunkPlatform) {
        SpawnUnderworldInitialChunk(renderer);
    }

    if (m_stage == DimensionStage::World1_Surface) {
        if (!PortalSystem::Get().AreBothPortalsPlaced()) {
            PortalSystem::Get().PlacePortal(PortalID::Blue_A, 
                XMFLOAT3(0.0f, 1.2f, 4.5f), 
                XMFLOAT3(0.0f, 0.0f, -1.0f));

            PortalSystem::Get().PlacePortal(PortalID::Orange_B, 
                XMFLOAT3(0.0f, m_underworldY + 1.2f, 4.5f), 
                XMFLOAT3(0.0f, 0.0f, -1.0f));
        }
    }
    renderer->UpdatePortalVisuals();
}

void WorldDimensionSystem::ExpandFullWorld2(Renderer* renderer) {
    if (!renderer) return;

    auto& models = renderer->GetModels();

    // Check if expanded World 2 models already exist from a previous transition
    bool continentExists = false;
    for (auto& m : models) {
        if (m.name == "Underworld_Continent" ||
            m.name.find("Underworld_Monolith_") == 0 ||
            m.name.find("Underworld_Energy_Core_") == 0) {
            m.visible = true;
            m.hasCollision = true;
            continentExists = true;
        }
    }

    if (continentExists) {
        renderer->UpdateModelMeshletRanges();
        renderer->InvalidateRaytracingScene();
        return;
    }

    auto geomPlane = renderer->GetPrimitiveGeometry("Plane");
    auto geomCube = renderer->GetPrimitiveGeometry("Cube");
    auto geomSphere = renderer->GetPrimitiveGeometry("Sphere");

    // 1. Vast Obsidian Continent (160m x 160m)
    if (geomPlane.valid) {
        RenderModel continent("Underworld_Continent", geomPlane.indexCount, geomPlane.startIndex, geomPlane.baseVertex, geomPlane.vertexCount);
        continent.numLODs = geomPlane.numLODs;
        for (uint32_t i = 0; i < geomPlane.numLODs; ++i) continent.lods[i] = geomPlane.lods[i];
        continent.position = XMFLOAT3(0.0f, m_underworldY - 0.05f, 4.5f);
        continent.scale = XMFLOAT3(160.0f, 1.0f, 160.0f);
        continent.material.albedo = XMFLOAT3(0.04f, 0.015f, 0.08f); // Ultra-dark obsidian
        continent.material.roughness = 0.25f;
        continent.material.metallic = 0.90f;
        continent.material.reflectance = 0.12f;
        continent.visible = true;
        continent.hasCollision = true;
        models.push_back(continent);
        m_world2ModelNames.push_back(continent.name);
    }

    // 2. Giant Monolithic Megaliths circling the underworld (12 monoliths)
    if (geomCube.valid) {
        const int numMonoliths = 12;
        const float radius = 55.0f;
        for (int i = 0; i < numMonoliths; ++i) {
            float angle = (float)i * (6.2831853f / (float)numMonoliths);
            float mx = cosf(angle) * radius;
            float mz = sinf(angle) * radius + 4.5f;

            char buf[64];
            sprintf_s(buf, "Underworld_Monolith_%d", i + 1);
            RenderModel monolith(buf, geomCube.indexCount, geomCube.startIndex, geomCube.baseVertex, geomCube.vertexCount);
            monolith.numLODs = geomCube.numLODs;
            for (uint32_t l = 0; l < geomCube.numLODs; ++l) monolith.lods[l] = geomCube.lods[l];
            monolith.position = XMFLOAT3(mx, m_underworldY + 9.0f, mz);
            monolith.scale = XMFLOAT3(2.5f, 18.0f, 2.5f);
            monolith.rotation = XMFLOAT3(0.0f, DirectX::XMConvertToDegrees(angle), 0.0f);
            
            // Alternating neon emission (Cyan vs Crimson)
            if (i % 2 == 0) {
                monolith.material.albedo = XMFLOAT3(0.1f, 0.8f, 1.0f);
                monolith.material.emission = 3.0f;
            } else {
                monolith.material.albedo = XMFLOAT3(1.0f, 0.15f, 0.45f);
                monolith.material.emission = 3.2f;
            }
            monolith.material.roughness = 0.2f;
            monolith.material.metallic = 0.7f;
            monolith.visible = true;
            monolith.hasCollision = true;
            models.push_back(monolith);
            m_world2ModelNames.push_back(monolith.name);
        }
    }

    // 3. Floating Void Monoliths and Mystic Energy Spheres in the sky above the underworld
    if (geomSphere.valid) {
        for (int s = 0; s < 5; ++s) {
            char buf[64];
            sprintf_s(buf, "Underworld_Energy_Core_%d", s + 1);
            RenderModel sphere(buf, geomSphere.indexCount, geomSphere.startIndex, geomSphere.baseVertex, geomSphere.vertexCount);
            sphere.numLODs = geomSphere.numLODs;
            for (uint32_t l = 0; l < geomSphere.numLODs; ++l) sphere.lods[l] = geomSphere.lods[l];
            
            float sx = (float)((s - 2) * 22);
            float sz = 4.5f + (float)((s % 2 == 0 ? 1 : -1) * 25);
            sphere.position = XMFLOAT3(sx, m_underworldY + 22.0f + (s * 3.0f), sz);
            sphere.scale = XMFLOAT3(3.0f, 3.0f, 3.0f);
            sphere.material.albedo = XMFLOAT3(0.9f, 0.2f, 0.8f);
            sphere.material.emission = 4.5f;
            sphere.material.roughness = 0.05f;
            sphere.material.metallic = 0.95f;
            sphere.visible = true;
            sphere.hasCollision = false;
            models.push_back(sphere);
            m_world2ModelNames.push_back(sphere.name);
        }
    }

    renderer->UpdateModelMeshletRanges();
    renderer->InvalidateRaytracingScene();
}

void WorldDimensionSystem::ResetToWorld1(Renderer* renderer, Camera* camera) {
    if (!renderer || !camera) return;

    m_stage = DimensionStage::World1_Surface;
    m_bannerTimer = 0.0f;

    // 1. Reset Camera position and floor back to surface
    camera->SetFloorY(0.0f);
    camera->SetKillPlaneY(-500.0f);
    camera->SetPosition(XMFLOAT3(0.0f, 1.6f, -2.0f));
    camera->SetYaw(0.0f);
    camera->SetPitch(0.0f);

    // 2. Restore World 1 surface visibility & collision
    auto& models = renderer->GetModels();
    for (auto& m : models) {
        if (m.name == "Floor" || m.name == "House Walls" || m.name == "House Roof" ||
            m.name.find("House") != std::string::npos || m.name.find("Surface") != std::string::npos) {
            m.visible = true;
            m.hasCollision = true;
        }
    }

    // 3. Safely hide expanded World 2 full continent/monoliths without destroying D3D12 resources
    for (auto& m : models) {
        if (m.name == "Underworld_Continent" ||
            m.name.find("Underworld_Monolith_") == 0 ||
            m.name.find("Underworld_Energy_Core_") == 0) {
            m.visible = false;
            m.hasCollision = false;
        }
    }

    // Also restore any surface dynamic models
    auto& dynModels = renderer->GetDynamicModels();
    for (auto& dm : dynModels) {
        if (dm.position.y > -50.0f) {
            dm.visible = true;
            dm.hasCollision = true;
        }
    }

    // 4. Restore sunny surface lighting
    renderer->SetSkyColor(XMFLOAT3(0.2f, 0.4f, 0.8f));
    renderer->SetLightColor(XMFLOAT3(1.0f, 0.98f, 0.95f));

    // 5. Ensure portal visuals exist and re-place both portals
    renderer->InitPortalVisuals();

    PortalSystem::Get().PlacePortal(PortalID::Blue_A, 
        XMFLOAT3(0.0f, 1.2f, 4.5f), 
        XMFLOAT3(0.0f, 0.0f, -1.0f));

    PortalSystem::Get().PlacePortal(PortalID::Orange_B, 
        XMFLOAT3(0.0f, m_underworldY + 1.2f, 4.5f), 
        XMFLOAT3(0.0f, 0.0f, -1.0f));

    renderer->UpdatePortalVisuals();
    renderer->UpdateModelMeshletRanges();
    renderer->InvalidateRaytracingScene();
}

void WorldDimensionSystem::DrawUI(Renderer* renderer, Camera* camera) {
#ifndef ENGINE_RELEASE
    if (!m_showUI) return;

    ImGui::SetNextWindowSize(ImVec2(440, 320), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("World Dimensions & Portal Transition", &m_showUI, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    ImGui::TextColored(ImVec4(0.8f, 0.4f, 1.0f, 1.0f), "DIMENSION TRANSITION SYSTEM");
    ImGui::Separator();

    if (m_stage == DimensionStage::World1_Surface) {
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "CURRENT REALM: WORLD 1 (Surface)");
        ImGui::Text("Portal A is waiting on the surface. Walk through to enter World 2.");
        ImGui::Text("Subterranean Underworld chunk is located at Y = %.1f m", m_underworldY);

        ImGui::Spacing();
        if (ImGui::Button("Trigger Transition to World 2 Now", ImVec2(280, 30))) {
            TriggerTransitionToWorld2(renderer, camera);
        }
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.7f, 1.0f), "CURRENT REALM: WORLD 2 (Underworld / Nether)");
        ImGui::Text("World 1 has dissolved. Both portals are closed.");
        ImGui::Text("Vast obsidian continent & monoliths materialized at Y = %.1f m", m_underworldY);

        ImGui::Spacing();
        if (ImGui::Button("Reset Back to World 1 (Surface)", ImVec2(280, 30))) {
            ResetToWorld1(renderer, camera);
        }
    }

    ImGui::Separator();
    if (camera) {
        XMFLOAT3 pos = camera->GetPosition();
        ImGui::Text("Player Position: (%.1f, %.1f, %.1f)", pos.x, pos.y, pos.z);
        ImGui::Text("Floor Elevation: %.1f m | Kill Plane: %.1f m", camera->GetFloorY(), camera->GetKillPlaneY());
    }

    ImGui::End();
#endif
}

void WorldDimensionSystem::DrawHUDOverlay() {
#ifndef ENGINE_RELEASE
    if (m_bannerTimer > 0.0f) {
        ImGuiIO& io = ImGui::GetIO();
        float width = io.DisplaySize.x;
        ImGui::SetNextWindowPos(ImVec2(width * 0.5f - 350.0f, 80.0f));
        ImGui::SetNextWindowSize(ImVec2(700.0f, 90.0f));
        ImGui::Begin("##DimensionBanner", nullptr, 
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | 
            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);

        ImGui::SetCursorPosX((700.0f - ImGui::CalcTextSize(m_bannerTitle.c_str()).x) * 0.5f);
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.85f, 1.0f), "%s", m_bannerTitle.c_str());

        ImGui::Spacing();
        ImGui::SetCursorPosX((700.0f - ImGui::CalcTextSize(m_bannerSubtitle.c_str()).x) * 0.5f);
        ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.95f, 1.0f), "%s", m_bannerSubtitle.c_str());

        ImGui::End();
    }

    if (m_stage == DimensionStage::World2_Underworld) {
        ImGuiIO& io = ImGui::GetIO();
        float height = io.DisplaySize.y;
        ImGui::SetNextWindowPos(ImVec2(10.0f, height - 40.0f));
        ImGui::Begin("##UnderworldReturnHint", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoBackground);
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.8f, 1.0f), "[F6] Return to World 1 (Surface)");
        ImGui::End();
    }
#endif
}
