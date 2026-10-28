#pragma once

#include "PostProcessTypes.h"
#include <vector>
#include <string>
#include <memory>
#if defined(_WIN32)
#include <DirectXMath.h>
#else
#include "Platform/DirectXMath/DirectXMath.h"
#endif

namespace EnginePostProcess {

class PostProcessSystem {
public:
    static PostProcessSystem& Get();

    bool Initialize();
    void Shutdown();

    void Update(float dt, const XMFLOAT3& cameraPos);

    // Settings & Volumes
    const PostProcessSettings& GetActiveSettings() const { return m_activeSettings; }
    PostProcessSettings& GetGlobalSettings() { return m_globalSettings; }
    const PostProcessSettings& GetGlobalSettings() const { return m_globalSettings; }

    PostProcessConstantBuffer GetConstantBufferData() const;

    void AddVolume(const PostProcessVolume& vol) { m_volumes.push_back(vol); }
    void ClearVolumes() { m_volumes.clear(); }
    const std::vector<PostProcessVolume>& GetVolumes() const { return m_volumes; }

    // Presets
    void ApplyPresetCinematic();
    void ApplyPresetHorror();
    void ApplyPresetCyberpunk();
    void ApplyPresetRetroVHS();
    void ApplyPresetCleanNeutral();

    // Editor UI
    void DrawUI(const XMFLOAT3& cameraPos);
    bool IsUIOpen() const { return m_uiOpen; }
    void SetUIOpen(bool open) { m_uiOpen = open; }
    void ToggleUI() { m_uiOpen = !m_uiOpen; }

private:
    PostProcessSystem();
    ~PostProcessSystem();
    PostProcessSystem(const PostProcessSystem&) = delete;
    PostProcessSystem& operator=(const PostProcessSystem&) = delete;

    PostProcessSettings m_globalSettings;
    PostProcessSettings m_activeSettings;
    std::vector<PostProcessVolume> m_volumes;

    bool m_initialized = false;
    bool m_uiOpen = false;
};

} // namespace EnginePostProcess
