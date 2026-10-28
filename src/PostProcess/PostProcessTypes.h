#pragma once

#include <DirectXMath.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

namespace EnginePostProcess {

using namespace DirectX;

enum class TonemapOperator : int {
    ACES = 0,
    Filmic,
    Reinhard,
    AgX,
    Neutral
};

struct PostProcessSettings {
    // Exposure & Tonemapping
    float exposure = 1.0f;
    TonemapOperator tonemapper = TonemapOperator::ACES;

    // Bloom
    bool  bloomEnabled = true;
    float bloomIntensity = 0.15f;
    float bloomThreshold = 0.85f;
    float bloomScatter = 0.7f;

    // Color Grading
    float contrast = 1.05f;
    float saturation = 1.0f;
    XMFLOAT3 colorFilter = { 1.0f, 1.0f, 1.0f };
    float colorTemperature = 0.0f; // -1.0 (cool blue) to +1.0 (warm amber)
    float tint = 0.0f;             // -1.0 (green) to +1.0 (magenta)
    XMFLOAT3 shadowTint = { 0.98f, 0.99f, 1.02f };
    XMFLOAT3 highlightTint = { 1.03f, 1.01f, 0.96f };

    // Optics & Lens
    float chromaticAberration = 0.0035f;
    float vignetteIntensity = 0.35f;
    float vignetteSmoothness = 0.45f;
    float filmGrainIntensity = 0.04f;
    float lensDistortion = 0.0f;

    // Depth of Field
    bool  dofEnabled = false;
    float focusDistance = 5.0f;
    float focalLength = 50.0f;
    float aperture = 2.8f;
    float maxBlurRadius = 8.0f;

    static PostProcessSettings Lerp(const PostProcessSettings& a, const PostProcessSettings& b, float t) {
        PostProcessSettings res = b;
        res.exposure = std::lerp(a.exposure, b.exposure, t);
        res.bloomIntensity = std::lerp(a.bloomIntensity, b.bloomIntensity, t);
        res.bloomThreshold = std::lerp(a.bloomThreshold, b.bloomThreshold, t);
        res.bloomScatter   = std::lerp(a.bloomScatter, b.bloomScatter, t);

        res.contrast   = std::lerp(a.contrast, b.contrast, t);
        res.saturation = std::lerp(a.saturation, b.saturation, t);
        res.colorTemperature = std::lerp(a.colorTemperature, b.colorTemperature, t);
        res.tint = std::lerp(a.tint, b.tint, t);

        res.colorFilter.x = std::lerp(a.colorFilter.x, b.colorFilter.x, t);
        res.colorFilter.y = std::lerp(a.colorFilter.y, b.colorFilter.y, t);
        res.colorFilter.z = std::lerp(a.colorFilter.z, b.colorFilter.z, t);

        res.shadowTint.x = std::lerp(a.shadowTint.x, b.shadowTint.x, t);
        res.shadowTint.y = std::lerp(a.shadowTint.y, b.shadowTint.y, t);
        res.shadowTint.z = std::lerp(a.shadowTint.z, b.shadowTint.z, t);

        res.highlightTint.x = std::lerp(a.highlightTint.x, b.highlightTint.x, t);
        res.highlightTint.y = std::lerp(a.highlightTint.y, b.highlightTint.y, t);
        res.highlightTint.z = std::lerp(a.highlightTint.z, b.highlightTint.z, t);

        res.chromaticAberration = std::lerp(a.chromaticAberration, b.chromaticAberration, t);
        res.vignetteIntensity   = std::lerp(a.vignetteIntensity, b.vignetteIntensity, t);
        res.vignetteSmoothness  = std::lerp(a.vignetteSmoothness, b.vignetteSmoothness, t);
        res.filmGrainIntensity  = std::lerp(a.filmGrainIntensity, b.filmGrainIntensity, t);

        res.focusDistance = std::lerp(a.focusDistance, b.focusDistance, t);
        res.aperture      = std::lerp(a.aperture, b.aperture, t);
        return res;
    }
};

struct PostProcessVolume {
    std::string name = "LocalVolume";
    bool isGlobal = false;
    XMFLOAT3 center = { 0.0f, 0.0f, 0.0f };
    XMFLOAT3 extents = { 10.0f, 5.0f, 10.0f };
    float blendDistance = 3.0f;
    float priority = 1.0f;
    PostProcessSettings settings;

    float CalculateWeight(const XMFLOAT3& pos) const {
        if (isGlobal) return 1.0f;

        float dx = std::abs(pos.x - center.x) - extents.x;
        float dy = std::abs(pos.y - center.y) - extents.y;
        float dz = std::abs(pos.z - center.z) - extents.z;

        float distOutside = (std::max)({ dx, dy, dz, 0.0f });
        if (distOutside <= 0.0f) return 1.0f;
        if (distOutside >= blendDistance) return 0.0f;

        return 1.0f - (distOutside / blendDistance);
    }
};

struct PostProcessConstantBuffer {
    XMFLOAT4 exposureBloom;    // x: exposure, y: bloomIntensity, z: bloomThreshold, w: bloomScatter
    XMFLOAT4 colorGrading;     // x: contrast, y: saturation, z: colorTemperature, w: tint
    XMFLOAT4 colorFilter;      // xyz: filter tint, w: tonemapper operator index
    XMFLOAT4 lensEffects;      // x: chromaticAberration, y: vignetteIntensity, z: vignetteSmoothness, w: filmGrain
    XMFLOAT4 shadowTint;       // xyz: shadow tint, w: unused
    XMFLOAT4 highlightTint;    // xyz: highlight tint, w: unused
    XMFLOAT4 dofParams;        // x: dofEnabled (0/1), y: focusDistance, z: focalLength, w: maxBlurRadius
    XMFLOAT4 padding;
};

} // namespace EnginePostProcess
