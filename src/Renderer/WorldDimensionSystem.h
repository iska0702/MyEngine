#pragma once

#include <DirectXMath.h>
#include <string>
#include <vector>

class Renderer;
class Camera;

enum class DimensionStage {
    World1_Surface = 0,    // Surface world with house & floor, small piece of World 2 visible under map
    Transitioning = 1,     // Portal entered, dissolving World 1
    World2_Underworld = 2  // World 2 fully expanded and loaded, World 1 vanished
};

class WorldDimensionSystem {
public:
    static WorldDimensionSystem& Get();

    void Initialize(Renderer* renderer, Camera* camera);
    void EnsureInitialized(Renderer* renderer, Camera* camera);
    void Update(float dt, Renderer* renderer, Camera* camera);

    // Called automatically when player walks through the portal
    void OnPlayerEnteredPortal(Renderer* renderer, Camera* camera);

    // Manual or automatic world transition
    void TriggerTransitionToWorld2(Renderer* renderer, Camera* camera);
    void ResetToWorld1(Renderer* renderer, Camera* camera);

    DimensionStage GetCurrentStage() const { return m_stage; }
    bool IsInWorld2() const { return m_stage == DimensionStage::World2_Underworld; }

    // UI & In-game HUD
    void DrawUI(Renderer* renderer, Camera* camera);
    void DrawHUDOverlay();

    bool IsWindowOpen() const { return m_showUI; }
    void SetWindowOpen(bool open) { m_showUI = open; }

private:
    WorldDimensionSystem() = default;
    ~WorldDimensionSystem() = default;

    void SpawnUnderworldInitialChunk(Renderer* renderer);
    void SpawnNetherPortalFrame(Renderer* renderer, const DirectX::XMFLOAT3& basePos, const std::string& prefix);
    void UnloadWorld1(Renderer* renderer);
    void ExpandFullWorld2(Renderer* renderer);

    DimensionStage m_stage = DimensionStage::World1_Surface;
    bool m_initialized = false;
    bool m_showUI = true;

    // Subterranean underworld elevation
    const float m_underworldY = -120.0f;

    // Cinematic banner timer & message
    float m_bannerTimer = 0.0f;
    std::string m_bannerTitle;
    std::string m_bannerSubtitle;

    // Track model indices or names created for each world
    std::vector<std::string> m_world1ModelNames;
    std::vector<std::string> m_world2ModelNames;
};
