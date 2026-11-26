#pragma once

#include <algorithm>
#include <cmath>

namespace EngineRender {
class DynamicResolution {
public:
    void Reset() { scale = 100; elapsed = 0; gpuAverage = 0; }
    int Scale() const { return scale; }

    bool Update(double gpuMilliseconds, float dt, int targetFps, int minimumScale) {
        if (!std::isfinite(gpuMilliseconds) || gpuMilliseconds <= 0 || gpuMilliseconds > 500 ||
            !std::isfinite(dt) || dt <= 0 || targetFps <= 0) return false;
        minimumScale = (std::clamp)(minimumScale, 50, 100);
        elapsed += (std::min)(dt, .1f);
        gpuAverage = gpuAverage == 0 ? gpuMilliseconds : gpuAverage + (gpuMilliseconds - gpuAverage) * .1;
        if (elapsed < 1.25f && scale >= minimumScale) return false;
        elapsed = 0;
        const double budget = 1000.0 / targetFps;
        int desired = scale;
        if (gpuAverage > budget * 1.06) desired -= gpuAverage > budget * 1.28 ? 10 : 5;
        else if (gpuAverage < budget * .84) desired += 5;
        desired = (std::clamp)(desired, minimumScale, 100);
        if (desired == scale) return false;
        scale = desired;
        gpuAverage = 0;
        return true;
    }
private:
    int scale = 100;
    float elapsed = 0;
    double gpuAverage = 0;
};
}
