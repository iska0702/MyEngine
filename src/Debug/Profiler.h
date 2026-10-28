#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <cstdint>
#include <algorithm>
#include <array>

namespace EngineDebug {

enum class Subsystem : uint32_t {
    Input = 0,
    Physics,
    Script,
    Navigation,
    VFX,
    Terrain,
    SceneGraph,
    Render,
    Present,
    UI,
    Count
};

inline const char* SubsystemToString(Subsystem sys) {
    switch (sys) {
        case Subsystem::Input:      return "Input & Events";
        case Subsystem::Physics:    return "Physics & CCT";
        case Subsystem::Script:     return "Lua 5.4 Scripting";
        case Subsystem::Navigation: return "NavMesh & AI";
        case Subsystem::VFX:        return "Particle Systems";
        case Subsystem::Terrain:    return "Terrain & Foliage";
        case Subsystem::SceneGraph: return "Scene Hierarchy";
        case Subsystem::Render:     return "Render Recording";
        case Subsystem::Present:    return "GPU Present / VSync";
        case Subsystem::UI:         return "ImGui & WebMenu";
        default:                    return "Unknown";
    }
}

struct GpuTelemetry {
    uint32_t lod0Models=0,lod1Models=0,lod2Models=0,lodCulledModels=0;
    std::array<double,10> gpuPassMs{};
    double gpuFrameMs=0,gpuWaitMs=0;
    uint32_t drawCalls = 0;
    uint32_t triangles = 0;
    uint32_t meshletsTotal = 0;
    uint32_t meshletsCulled = 0;
    uint32_t pointLights = 0;
    uint32_t spotLights = 0;
    uint32_t activeParticles = 0;
    uint64_t vramUsageBytes = 0;
    uint64_t sysRamUsageBytes = 0;
};

class Profiler {
public:
    static Profiler& Get();

    void Initialize();
    void Shutdown();

    void BeginFrame();
    void EndFrame();

    void RecordSubsystemTime(Subsystem sys, float milliseconds);
    void SetGpuTelemetry(const GpuTelemetry& telemetry) { m_gpuTelemetry = telemetry; }
    const GpuTelemetry& GetGpuTelemetry() const { return m_gpuTelemetry; }

    // Metrics
    float GetCurrentFrameTimeMs() const { return m_currentFrameTimeMs; }
    float GetAverageFrameTimeMs() const { return m_avgFrameTimeMs; }
    float GetCurrentFPS() const { return m_currentFPS; }
    float GetAverageFPS() const { return m_avgFPS; }
    float GetOnePercentLowFPS() const { return m_onePercentLowFPS; }
    float GetSubsystemTime(Subsystem sys) const { return m_subsystemTimes[static_cast<size_t>(sys)]; }

    // Ring buffers for ImGui plotting
    static constexpr size_t HISTORY_SIZE = 120;
    const float* GetFrameTimeHistory() const { return m_frameTimeHistory; }
    const float* GetFpsHistory() const { return m_fpsHistory; }
    size_t GetHistoryOffset() const { return m_historyIndex; }

    // UI
    void DrawUI();
    void ToggleUI() { m_uiOpen = !m_uiOpen; }
    void SetUIOpen(bool open) { m_uiOpen = open; }
    bool IsUIOpen() const { return m_uiOpen; }

private:
    Profiler() = default;
    ~Profiler() = default;
    Profiler(const Profiler&) = delete;
    Profiler& operator=(const Profiler&) = delete;

    std::chrono::high_resolution_clock::time_point m_frameStartTime;
    float m_currentFrameTimeMs = 0.0f;
    float m_avgFrameTimeMs = 0.0f;
    float m_currentFPS = 0.0f;
    float m_avgFPS = 0.0f;
    float m_onePercentLowFPS = 0.0f;

    float m_frameTimeHistory[HISTORY_SIZE] = { 0.0f };
    float m_fpsHistory[HISTORY_SIZE] = { 0.0f };
    size_t m_historyIndex = 0;

    float m_subsystemTimes[static_cast<size_t>(Subsystem::Count)] = { 0.0f };
    float m_subsystemAccum[static_cast<size_t>(Subsystem::Count)] = { 0.0f };

    GpuTelemetry m_gpuTelemetry;

    bool m_initialized = false;
    bool m_uiOpen = false;
};

class ScopedTimer {
public:
    ScopedTimer(Subsystem sys)
        : m_sys(sys), m_start(std::chrono::high_resolution_clock::now()) {}

    ~ScopedTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        float ms = std::chrono::duration<float, std::milli>(end - m_start).count();
        Profiler::Get().RecordSubsystemTime(m_sys, ms);
    }

private:
    Subsystem m_sys;
    std::chrono::high_resolution_clock::time_point m_start;
};

} // namespace EngineDebug

#define PROFILE_STAGE(sys) EngineDebug::ScopedTimer _prof_timer_##sys(EngineDebug::Subsystem::sys)
