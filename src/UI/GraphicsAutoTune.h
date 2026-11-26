#pragma once

#include "Renderer/Renderer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace EngineUI {
class GraphicsAutoTune {
    struct Profile {
        UpscaleQuality quality = UpscaleQuality::Native;
        bool gi = false, fog = false, ao = false, vsm = false, reflections = false, shadows = false;
    };

public:
    bool Running() const { return running; }
    size_t Step() const { return step + 1; }
    size_t Steps() const { return profiles.size(); }
    const std::string& Status() const { return status; }
    float ResultFps() const { return resultFps; }
    static bool Supported(const Renderer& renderer, UpscaleTechnique technique) {
        switch (technique) {
        case UpscaleTechnique::DLSS: return renderer.IsDLSSSupported();
        case UpscaleTechnique::FSR: return renderer.IsFSRSupported();
        case UpscaleTechnique::XeSS: return renderer.IsXeSSSupported();
        default: return true;
        }
    }

    bool Start(Renderer& renderer, UpscaleTechnique technique, int target) {
        if (running || !Supported(renderer, technique)) return false;
        originalTechnique = renderer.GetUpscaleTechnique();
        original = {renderer.GetUpscaleQuality(), renderer.IsSSGIEnabled(), renderer.IsVolumetricFogEnabled(),
                    renderer.IsSSAOEnabled(), renderer.IsVSMEnabled(), renderer.m_rtReflectionsEnabled, renderer.m_rtShadowsEnabled};
        originalVSync = renderer.IsVSyncEnabled();
        originalDLSSG = renderer.IsDLSSGRequested();
        originalFSRFG = renderer.IsFSRFrameGenEnabled();
        originalXeSSFG = renderer.IsXeSSFrameGenEnabled();
        selected = technique;
        targetFps = (std::clamp)(target, 30, 360);
        profiles.clear();
        auto profile = original;
        profile.quality = UpscaleQuality::Native;
        profiles.push_back(profile);
        auto add = [&](const Profile& next) {
            const auto& last = profiles.back();
            if (next.quality != last.quality || next.gi != last.gi || next.fog != last.fog ||
                next.ao != last.ao || next.vsm != last.vsm || next.reflections != last.reflections ||
                next.shadows != last.shadows) profiles.push_back(next);
        };
        if (selected != UpscaleTechnique::None) { profile.quality = UpscaleQuality::Quality; add(profile); }
        if (renderer.m_dxrEnabled && !renderer.IsPathTracingEnabled()) profile.reflections = false;
        profile.gi = false;
        add(profile);
        if (selected != UpscaleTechnique::None) { profile.quality = UpscaleQuality::Balanced; add(profile); }
        profile.fog = false;
        add(profile);
        if (selected != UpscaleTechnique::None) { profile.quality = UpscaleQuality::Performance; add(profile); }
        profile.ao = false;
        profile.vsm = false;
        if (renderer.m_dxrEnabled && !renderer.IsPathTracingEnabled()) profile.shadows = false;
        add(profile);
        if (selected != UpscaleTechnique::None) { profile.quality = UpscaleQuality::UltraPerformance; add(profile); }

        renderer.SetDLSSGActive(false);
        renderer.SetFSRFrameGenEnabled(false);
        renderer.SetXeSSFrameGenEnabled(false);
        renderer.SetVSyncEnabled(false);
        running = true;
        step = bestStep = 0;
        bestFps = resultFps = 0;
        status.clear();
        BeginProfile(renderer);
        return true;
    }

    // Sample the elapsed time of complete rendered frames, including the CPU workload.
    bool Update(Renderer& renderer, float seconds) {
        if (!running) return false;
        if (!std::isfinite(seconds) || seconds <= 0) return false;
        if (warmup > 0) { --warmup; return false; }
        if (!UpscalerActive(renderer)) {
            if (++inactiveFrames >= 45) Cancel(renderer, "Selected upscaler did not activate. Previous settings restored.");
            return false;
        }
        inactiveFrames = 0;
        samples[sampleCount++ % samples.size()] = seconds;
        measuredSeconds += seconds;
        if (sampleCount < samples.size() || measuredSeconds < .75f) return false;
        std::sort(samples.begin(), samples.end());
        const float fps = 1.f / samples[samples.size() * 3 / 4];
        if (fps > bestFps) { bestFps = fps; bestStep = step; }
        if (fps >= targetFps || step + 1 == profiles.size()) {
            const bool reached = fps >= targetFps;
            Apply(renderer, selected, profiles[reached ? step : bestStep]);
            resultFps = reached ? fps : bestFps;
            RestoreTiming(renderer);
            status = reached ? "Target reached. Settings applied." : "Target unavailable in this view. Fastest measured profile applied.";
            return true;
        }
        ++step;
        BeginProfile(renderer);
        return false;
    }

    void Cancel(Renderer& renderer, const char* reason = "Cancelled. Previous settings restored.") {
        if (!running) return;
        Apply(renderer, originalTechnique, original);
        RestoreTiming(renderer);
        status = reason;
    }

private:
    bool UpscalerActive(const Renderer& renderer) const {
        switch (selected) {
        case UpscaleTechnique::DLSS: return renderer.IsDLSSActive();
        case UpscaleTechnique::FSR: return renderer.IsFSRActive();
        case UpscaleTechnique::XeSS: return renderer.IsXeSSActive();
        default: return true;
        }
    }
    static void Apply(Renderer& renderer, UpscaleTechnique technique, const Profile& profile) {
        renderer.SetUpscaleTechnique(technique);
        renderer.SetUpscaleQuality(profile.quality);
        renderer.SetSSGIEnabled(profile.gi);
        renderer.SetVolumetricFogEnabled(profile.fog);
        renderer.SetSSAOEnabled(profile.ao);
        renderer.SetVSMEnabled(profile.vsm);
        renderer.m_rtReflectionsEnabled = profile.reflections;
        renderer.m_rtShadowsEnabled = profile.shadows;
    }
    void BeginProfile(Renderer& renderer) {
        Apply(renderer, selected, profiles[step]);
        warmup = 30;
        sampleCount = inactiveFrames = 0;
        measuredSeconds = 0;
    }
    void RestoreTiming(Renderer& renderer) {
        renderer.SetVSyncEnabled(originalVSync);
        if (originalDLSSG) renderer.SetDLSSGActive(true);
        if (originalFSRFG) renderer.SetFSRFrameGenEnabled(true);
        if (originalXeSSFG) renderer.SetXeSSFrameGenEnabled(true);
        running = false;
    }

    Profile original;
    UpscaleTechnique originalTechnique = UpscaleTechnique::None, selected = UpscaleTechnique::None;
    std::vector<Profile> profiles;
    std::array<float, 48> samples{};
    size_t step = 0, bestStep = 0, warmup = 0, sampleCount = 0, inactiveFrames = 0;
    int targetFps = 60;
    bool running = false, originalVSync = false, originalDLSSG = false, originalFSRFG = false, originalXeSSFG = false;
    float bestFps = 0, resultFps = 0, measuredSeconds = 0;
    std::string status;
};
}
