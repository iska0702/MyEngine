#pragma once
#include "Renderer.h"
#include "imgui.h"

namespace EngineUI {
inline bool ApplyAntiAliasing(Renderer& renderer, int mode) {
    if (mode < 0 || mode > 4 || (mode == 2 && !renderer.IsDLSSSupported()) ||
        (mode == 3 && !renderer.IsXeSSSupported()) || (mode == 4 && !renderer.IsFSRSupported())) return false;
    renderer.SetTAAEnabled(mode == 1);
    renderer.SetUpscaleTechnique(mode == 2 ? UpscaleTechnique::DLSS :
        mode == 3 ? UpscaleTechnique::XeSS : mode == 4 ? UpscaleTechnique::FSR : UpscaleTechnique::None);
    if (mode >= 2) renderer.SetUpscaleQuality(UpscaleQuality::Native);
    return true;
}
inline void ApplyGraphicsPreset(Renderer& renderer, int preset) {
    // 0 = Low, 1 = Medium, 2 = High (Hybrid RT), 3 = Ultra (RTX + AI Upscale), 4 = Path Tracing Overdrive
    switch (preset) {
        case 0: // Low / Max FPS
            renderer.m_dxrEnabled = false;
            renderer.SetPathTracingEnabled(false);
            renderer.SetSSAOEnabled(false);
            renderer.SetSSGIEnabled(false);
            renderer.SetVSMEnabled(false);
            renderer.SetVolumetricFogEnabled(false);
            renderer.SetCloudDensity(0.0f);
            renderer.SetAnisotropy(2);
            renderer.SetTAAEnabled(false);
            break;
        case 1: // Medium
            renderer.m_dxrEnabled = false;
            renderer.SetPathTracingEnabled(false);
            renderer.SetSSAOEnabled(true);
            renderer.SetSSGIEnabled(false);
            renderer.SetVSMEnabled(true);
            renderer.SetVolumetricFogEnabled(false);
            renderer.SetCloudDensity(0.6f);
            renderer.SetAnisotropy(4);
            renderer.SetTAAEnabled(true);
            break;
        case 2: // High (Hybrid RT)
            renderer.m_dxrEnabled = true;
            renderer.SetPathTracingEnabled(false);
            renderer.m_rtShadowsEnabled = true;
            renderer.m_rtReflectionsEnabled = true;
            renderer.SetSSAOEnabled(true);
            renderer.SetSSGIEnabled(true);
            renderer.SetVSMEnabled(true);
            renderer.SetVolumetricFogEnabled(true);
            renderer.SetCloudDensity(1.0f);
            renderer.SetAnisotropy(8);
            renderer.SetTAAEnabled(true);
            break;
        case 3: // Ultra RTX + Quality Upscaling
            renderer.m_dxrEnabled = true;
            renderer.SetPathTracingEnabled(false);
            renderer.m_rtShadowsEnabled = true;
            renderer.m_rtReflectionsEnabled = true;
            renderer.SetSSAOEnabled(true);
            renderer.SetSSGIEnabled(true);
            renderer.SetVSMEnabled(true);
            renderer.SetVolumetricFogEnabled(true);
            renderer.SetCloudDensity(1.2f);
            renderer.SetAnisotropy(16);
            if (renderer.IsDLSSSupported()) {
                renderer.SetUpscaleTechnique(UpscaleTechnique::DLSS);
                renderer.SetUpscaleQuality(UpscaleQuality::Quality);
            } else if (renderer.IsFSRSupported()) {
                renderer.SetUpscaleTechnique(UpscaleTechnique::FSR);
                renderer.SetUpscaleQuality(UpscaleQuality::Quality);
            }
            break;
        case 4: // Path Tracing Cinema Overdrive
            renderer.m_dxrEnabled = true;
            renderer.SetPathTracingEnabled(true);
            renderer.SetPathTracingBounces(3);
            renderer.SetVolumetricFogEnabled(true);
            renderer.SetCloudDensity(1.25f);
            renderer.SetAnisotropy(16);
            if (renderer.IsDLSSSupported()) {
                renderer.SetUpscaleTechnique(UpscaleTechnique::DLSS);
                renderer.SetUpscaleQuality(UpscaleQuality::Balanced);
            } else if (renderer.IsFSRSupported()) {
                renderer.SetUpscaleTechnique(UpscaleTechnique::FSR);
                renderer.SetUpscaleQuality(UpscaleQuality::Balanced);
            }
            break;
    }
    renderer.InvalidateRaytracingScene();
}
inline void DrawAntiAliasingControls(Renderer& renderer) {
    bool rrEnabled = renderer.GetDLSSRayReconstructionVersion() != Renderer::DLSSRRVersion::Off;
    ImGui::BeginDisabled(!renderer.IsDLSSRayReconstructionSupported());
    if (ImGui::Checkbox("DLSS Ray Reconstruction", &rrEnabled)) {
        if (rrEnabled) {
            renderer.m_dxrEnabled = true;
            renderer.SetTAAEnabled(false);
            renderer.SetUpscaleTechnique(UpscaleTechnique::DLSS);
            renderer.InvalidateRaytracingScene();
        }
        renderer.SetDLSSRayReconstructionActive(rrEnabled);
    }
    if (rrEnabled) {
        int model = renderer.GetDLSSRayReconstructionVersion() == Renderer::DLSSRRVersion::Version1_0 ? 0 : 1;
        const char* presets[] = {"Preset D", "Preset E"};
        if (ImGui::Combo("RR model preset", &model, presets, 2))
            renderer.SetDLSSRayReconstructionVersion(model == 0 ? Renderer::DLSSRRVersion::Version1_0 : Renderer::DLSSRRVersion::Version2_0);
        ImGui::TextWrapped("%s", renderer.IsDLSSRayReconstructionActive() ? "RR: active" :
            "RR: waiting for DXR + DLSS/DLAA and successful SDK evaluation");
    }
    ImGui::EndDisabled();
    if (!renderer.IsDLSSRayReconstructionSupported()) ImGui::TextDisabled("Ray Reconstruction is unavailable on this GPU/driver.");

    const auto technique = renderer.GetUpscaleTechnique();
    const bool native = renderer.GetUpscaleQuality() == UpscaleQuality::Native;
    int selected = -1;
    const char* preview = "AA provided by upscaler";
    if (technique == UpscaleTechnique::None) {
        selected = renderer.IsTAAEnabled() ? 1 : 0;
        preview = selected ? "Temporal AA (TAA)" : "Off";
    } else if (native && technique == UpscaleTechnique::DLSS) {
        selected = 2; preview = "NVIDIA DLAA (native resolution)";
    } else if (native && technique == UpscaleTechnique::XeSS) {
        selected = 3; preview = "Intel XeSS Native AA";
    } else if (native && technique == UpscaleTechnique::FSR) {
        selected = 4; preview = "AMD FSR Native AA";
    }
    if (ImGui::BeginCombo("Anti-aliasing", preview)) {
        const char* names[] = {"Off", "Temporal AA (TAA)",
            "NVIDIA DLAA (native resolution)", "Intel XeSS Native AA", "AMD FSR Native AA"};
        for (int mode = 0; mode < 5; ++mode) {
            const bool available = mode < 2 || (mode == 2 ? renderer.IsDLSSSupported() : mode == 3 ? renderer.IsXeSSSupported() : renderer.IsFSRSupported());
            ImGui::BeginDisabled(!available);
            if (ImGui::Selectable(names[mode], selected == mode)) {
                ApplyAntiAliasing(renderer, mode);
            }
            ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
    if (selected >= 2) {
        ImGui::TextWrapped("Native AA renders at display resolution. It improves edges without reducing render resolution.");
        const bool active = selected == 2 ? renderer.IsDLSSActive() : selected == 3 ? renderer.IsXeSSActive() : renderer.IsFSRActive();
        ImGui::Text("SDK evaluation: %s", active ? "active" : "pending / unavailable");
    }
    if (renderer.GetUpscaleTechnique() == UpscaleTechnique::DLSS) {
        int quality = static_cast<int>(renderer.GetUpscaleQuality());
        if (quality < 0 || quality > 4) quality = 1;
        const char* modes[] = {"DLAA (native)", "Quality", "Balanced", "Performance", "Ultra Performance"};
        if (ImGui::Combo("DLSS quality", &quality, modes, IM_ARRAYSIZE(modes)))
            renderer.SetUpscaleQuality(static_cast<UpscaleQuality>(quality));
        ImGui::TextDisabled("Changing quality recreates the render targets.");
    }
}
}
