#include "PostProcessSystem.h"
#include <algorithm>
#include <iostream>

#if !defined(ENGINE_RELEASE) && !defined(ENGINE_SERVER)
#include <imgui.h>
#endif

namespace EnginePostProcess {

PostProcessSystem& PostProcessSystem::Get() {
    static PostProcessSystem instance;
    return instance;
}

PostProcessSystem::PostProcessSystem() {
    ApplyPresetCleanNeutral();
}

PostProcessSystem::~PostProcessSystem() {
    Shutdown();
}

bool PostProcessSystem::Initialize() {
    if (m_initialized) return true;
    m_initialized = true;

    // Create a demo local horror volume placed in the scene
    PostProcessVolume cryptVolume;
    cryptVolume.name = "Dark Crypt Volume";
    cryptVolume.center = XMFLOAT3(-15.0f, 0.0f, -15.0f);
    cryptVolume.extents = XMFLOAT3(12.0f, 8.0f, 12.0f);
    cryptVolume.blendDistance = 4.0f;
    cryptVolume.priority = 2.0f;

    // Horror settings for crypt
    cryptVolume.settings.exposure = 0.65f;
    cryptVolume.settings.tonemapper = TonemapOperator::Reinhard;
    cryptVolume.settings.contrast = 1.3f;
    cryptVolume.settings.saturation = 0.45f;
    cryptVolume.settings.colorTemperature = -0.4f;
    cryptVolume.settings.tint = -0.2f;
    cryptVolume.settings.vignetteIntensity = 0.65f;
    cryptVolume.settings.filmGrainIntensity = 0.10f;
    cryptVolume.settings.chromaticAberration = 0.007f;

    AddVolume(cryptVolume);
    return true;
}

void PostProcessSystem::Shutdown() {
    m_volumes.clear();
    m_initialized = false;
}

void PostProcessSystem::Update(float dt, const XMFLOAT3& cameraPos) {
    (void)dt;
    PostProcessSettings blended = m_globalSettings;

    // Blend local volumes based on camera location
    for (const auto& vol : m_volumes) {
        float w = vol.CalculateWeight(cameraPos);
        if (w > 0.001f) {
            blended = PostProcessSettings::Lerp(blended, vol.settings, w);
        }
    }

    m_activeSettings = blended;
}

PostProcessConstantBuffer PostProcessSystem::GetConstantBufferData() const {
    PostProcessConstantBuffer cb = {};
    const auto& s = m_activeSettings;

    cb.exposureBloom = XMFLOAT4(s.exposure, s.bloomIntensity, s.bloomThreshold, s.bloomScatter);
    cb.colorGrading  = XMFLOAT4(s.contrast, s.saturation, s.colorTemperature, s.tint);
    cb.colorFilter   = XMFLOAT4(s.colorFilter.x, s.colorFilter.y, s.colorFilter.z, static_cast<float>(s.tonemapper));
    cb.lensEffects   = XMFLOAT4(s.chromaticAberration, s.vignetteIntensity, s.vignetteSmoothness, s.filmGrainIntensity);
    cb.shadowTint    = XMFLOAT4(s.shadowTint.x, s.shadowTint.y, s.shadowTint.z, 0.0f);
    cb.highlightTint = XMFLOAT4(s.highlightTint.x, s.highlightTint.y, s.highlightTint.z, 0.0f);
    cb.dofParams     = XMFLOAT4(s.dofEnabled ? 1.0f : 0.0f, s.focusDistance, s.focalLength, s.maxBlurRadius);

    return cb;
}

void PostProcessSystem::ApplyPresetCinematic() {
    auto& s = m_globalSettings;
    s.exposure = 1.05f;
    s.tonemapper = TonemapOperator::ACES;
    s.bloomEnabled = true;
    s.bloomIntensity = 0.14f;
    s.bloomThreshold = 0.82f;
    s.bloomScatter = 0.7f;
    s.contrast = 1.12f;
    s.saturation = 1.05f;
    s.colorTemperature = 0.08f;
    s.tint = 0.0f;
    s.colorFilter = XMFLOAT3(1.0f, 1.0f, 1.0f);
    s.shadowTint = XMFLOAT3(0.96f, 0.98f, 1.03f);
    s.highlightTint = XMFLOAT3(1.04f, 1.01f, 0.95f);
    s.chromaticAberration = 0.0025f;
    s.vignetteIntensity = 0.32f;
    s.vignetteSmoothness = 0.45f;
    s.filmGrainIntensity = 0.035f;
    s.dofEnabled = false;
    m_activeSettings = m_globalSettings;
}

void PostProcessSystem::ApplyPresetHorror() {
    auto& s = m_globalSettings;
    s.exposure = 0.72f;
    s.tonemapper = TonemapOperator::Reinhard;
    s.bloomEnabled = true;
    s.bloomIntensity = 0.08f;
    s.bloomThreshold = 0.95f;
    s.contrast = 1.35f;
    s.saturation = 0.52f;
    s.colorTemperature = -0.35f;
    s.tint = -0.15f;
    s.colorFilter = XMFLOAT3(0.88f, 0.95f, 0.85f);
    s.shadowTint = XMFLOAT3(0.85f, 0.92f, 0.85f);
    s.highlightTint = XMFLOAT3(0.9f, 0.95f, 0.95f);
    s.chromaticAberration = 0.007f;
    s.vignetteIntensity = 0.65f;
    s.vignetteSmoothness = 0.55f;
    s.filmGrainIntensity = 0.12f;
    s.dofEnabled = false;
    m_activeSettings = m_globalSettings;
}

void PostProcessSystem::ApplyPresetCyberpunk() {
    auto& s = m_globalSettings;
    s.exposure = 1.22f;
    s.tonemapper = TonemapOperator::AgX;
    s.bloomEnabled = true;
    s.bloomIntensity = 0.32f;
    s.bloomThreshold = 0.68f;
    s.contrast = 1.25f;
    s.saturation = 1.42f;
    s.colorTemperature = 0.15f;
    s.tint = 0.22f;
    s.colorFilter = XMFLOAT3(1.0f, 0.95f, 1.05f);
    s.shadowTint = XMFLOAT3(0.7f, 0.9f, 1.1f);
    s.highlightTint = XMFLOAT3(1.15f, 0.85f, 1.05f);
    s.chromaticAberration = 0.006f;
    s.vignetteIntensity = 0.28f;
    s.filmGrainIntensity = 0.025f;
    s.dofEnabled = false;
    m_activeSettings = m_globalSettings;
}

void PostProcessSystem::ApplyPresetRetroVHS() {
    auto& s = m_globalSettings;
    s.exposure = 1.12f;
    s.tonemapper = TonemapOperator::Filmic;
    s.bloomEnabled = true;
    s.bloomIntensity = 0.22f;
    s.bloomThreshold = 0.75f;
    s.contrast = 1.15f;
    s.saturation = 0.88f;
    s.colorTemperature = 0.35f;
    s.tint = 0.1f;
    s.shadowTint = XMFLOAT3(1.05f, 0.95f, 0.9f);
    s.highlightTint = XMFLOAT3(1.1f, 1.05f, 0.9f);
    s.chromaticAberration = 0.012f;
    s.vignetteIntensity = 0.45f;
    s.filmGrainIntensity = 0.16f;
    s.dofEnabled = false;
    m_activeSettings = m_globalSettings;
}

void PostProcessSystem::ApplyPresetCleanNeutral() {
    auto& s = m_globalSettings;
    s.exposure = 1.0f;
    s.tonemapper = TonemapOperator::Neutral;
    s.bloomEnabled = false;
    s.bloomIntensity = 0.0f;
    s.contrast = 1.0f;
    s.saturation = 1.0f;
    s.colorTemperature = 0.0f;
    s.tint = 0.0f;
    s.colorFilter = XMFLOAT3(1.0f, 1.0f, 1.0f);
    s.shadowTint = XMFLOAT3(1.0f, 1.0f, 1.0f);
    s.highlightTint = XMFLOAT3(1.0f, 1.0f, 1.0f);
    s.chromaticAberration = 0.0f;
    s.vignetteIntensity = 0.0f;
    s.filmGrainIntensity = 0.0f;
    s.dofEnabled = false;
    m_activeSettings = m_globalSettings;
}

void PostProcessSystem::DrawUI(const XMFLOAT3& cameraPos) {
#if !defined(ENGINE_RELEASE) && !defined(ENGINE_SERVER)
    if (!m_uiOpen) return;

    ImGui::SetNextWindowSize(ImVec2(480, 580), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(" Post-Processing & Color Grading Studio", &m_uiOpen)) {
        ImGui::End();
        return;
    }

    ImGui::TextColored(ImVec4(0.3f, 0.9f, 1.0f, 1.0f), "Cinematic Presets:");
    if (ImGui::Button(" Cinematic")) ApplyPresetCinematic();
    ImGui::SameLine();
    if (ImGui::Button(" Horror"))    ApplyPresetHorror();
    ImGui::SameLine();
    if (ImGui::Button(" Cyberpunk")) ApplyPresetCyberpunk();
    ImGui::SameLine();
    if (ImGui::Button(" Retro VHS")) ApplyPresetRetroVHS();
    ImGui::SameLine();
    if (ImGui::Button(" Clean"))     ApplyPresetCleanNeutral();

    ImGui::Separator();

    auto& s = m_globalSettings;

    // Tonemapper
    const char* tonemapNames[] = { "ACES Fitted", "Filmic Curve", "Reinhard", "AgX PBR", "Neutral Linear" };
    int curTonemap = static_cast<int>(s.tonemapper);
    if (ImGui::Combo("Tonemapper", &curTonemap, tonemapNames, IM_ARRAYSIZE(tonemapNames))) {
        s.tonemapper = static_cast<TonemapOperator>(curTonemap);
    }

    ImGui::SliderFloat("Exposure", &s.exposure, 0.1f, 4.0f, "%.2f");

    // Bloom
    if (ImGui::CollapsingHeader(" Physically-Based Bloom & Glare", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Bloom Enabled", &s.bloomEnabled);
        if (s.bloomEnabled) {
            ImGui::SliderFloat("Intensity##Bloom", &s.bloomIntensity, 0.0f, 1.5f, "%.3f");
            ImGui::SliderFloat("Threshold##Bloom", &s.bloomThreshold, 0.2f, 2.5f, "%.2f");
            ImGui::SliderFloat("Scatter##Bloom", &s.bloomScatter, 0.1f, 1.0f, "%.2f");
        }
    }

    // Color Grading
    if (ImGui::CollapsingHeader(" Color Grading & White Balance", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Contrast", &s.contrast, 0.5f, 2.0f, "%.2f");
        ImGui::SliderFloat("Saturation", &s.saturation, 0.0f, 2.5f, "%.2f");
        ImGui::SliderFloat("Color Temperature (Cool/Warm)", &s.colorTemperature, -1.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Tint (Green/Magenta)", &s.tint, -1.0f, 1.0f, "%.2f");
        ImGui::ColorEdit3("Color Filter", &s.colorFilter.x);
        ImGui::ColorEdit3("Shadow Tint", &s.shadowTint.x);
        ImGui::ColorEdit3("Highlight Tint", &s.highlightTint.x);
    }

    // Optics & Camera Lens
    if (ImGui::CollapsingHeader(" Optics, Vignette & Film Grain", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Chromatic Aberration", &s.chromaticAberration, 0.0f, 0.02f, "%.4f");
        ImGui::SliderFloat("Vignette Intensity", &s.vignetteIntensity, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Vignette Smoothness", &s.vignetteSmoothness, 0.1f, 1.0f, "%.2f");
        ImGui::SliderFloat("Film Grain Intensity", &s.filmGrainIntensity, 0.0f, 0.3f, "%.3f");
    }

    // Depth of Field
    if (ImGui::CollapsingHeader(" Depth of Field (DoF)")) {
        ImGui::Checkbox("DoF Enabled", &s.dofEnabled);
        if (s.dofEnabled) {
            ImGui::SliderFloat("Focus Distance", &s.focusDistance, 0.5f, 100.0f, "%.1fm");
            ImGui::SliderFloat("Focal Length", &s.focalLength, 15.0f, 200.0f, "%.1fmm");
            ImGui::SliderFloat("Aperture", &s.aperture, 1.2f, 16.0f, "f/%.1f");
            ImGui::SliderFloat("Max Blur Radius", &s.maxBlurRadius, 1.0f, 20.0f, "%.1fpx");
        }
    }

    // Post-Process Volumes
    if (ImGui::CollapsingHeader(" Post-Processing Volumes")) {
        ImGui::Text("Active Volumes: %zu", m_volumes.size());

        if (ImGui::Button(" Add Volume at Camera")) {
            PostProcessVolume newVol;
            newVol.name = "Volume_" + std::to_string(m_volumes.size() + 1);
            newVol.center = cameraPos;
            newVol.extents = XMFLOAT3(8.0f, 5.0f, 8.0f);
            newVol.blendDistance = 3.0f;
            newVol.settings = s; // Inherit current settings
            AddVolume(newVol);
        }
        ImGui::SameLine();
        if (ImGui::Button(" Clear Volumes")) {
            ClearVolumes();
        }

        for (size_t i = 0; i < m_volumes.size(); ++i) {
            auto& vol = m_volumes[i];
            ImGui::PushID(static_cast<int>(i + 7000));
            float weight = vol.CalculateWeight(cameraPos);
            char header[128];
            snprintf(header, sizeof(header), "[%zu] %s (Weight: %.2f)##%zu", i, vol.name.c_str(), weight, i);

            if (ImGui::TreeNode(header)) {
                ImGui::DragFloat3("Center", &vol.center.x, 0.2f);
                ImGui::DragFloat3("Extents", &vol.extents.x, 0.2f);
                ImGui::SliderFloat("Blend Distance", &vol.blendDistance, 0.5f, 10.0f, "%.1fm");
                ImGui::SliderFloat("Volume Exposure", &vol.settings.exposure, 0.1f, 4.0f, "%.2f");
                ImGui::SliderFloat("Volume Contrast", &vol.settings.contrast, 0.5f, 2.0f, "%.2f");
                ImGui::SliderFloat("Volume Vignette", &vol.settings.vignetteIntensity, 0.0f, 1.0f, "%.2f");
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }

    ImGui::End();
#endif
}

} // namespace EnginePostProcess
