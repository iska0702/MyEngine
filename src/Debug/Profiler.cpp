#include "Profiler.h"
#include <numeric>
#include <algorithm>
#include <vector>

#if !defined(ENGINE_RELEASE) && !defined(ENGINE_SERVER)
#include <imgui.h>
#endif

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#endif

namespace EngineDebug {

Profiler& Profiler::Get() {
    static Profiler instance;
    return instance;
}

void Profiler::Initialize() {
    if (m_initialized) return;
    m_initialized = true;
    m_frameStartTime = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < HISTORY_SIZE; ++i) {
        m_frameTimeHistory[i] = 16.6f;
        m_fpsHistory[i] = 60.0f;
    }
}

void Profiler::Shutdown() {
    m_initialized = false;
}

void Profiler::BeginFrame() {
    m_frameStartTime = std::chrono::high_resolution_clock::now();

    // Transfer accumulators to current subsystem times and clear
    for (size_t i = 0; i < static_cast<size_t>(Subsystem::Count); ++i) {
        m_subsystemTimes[i] = m_subsystemAccum[i];
        m_subsystemAccum[i] = 0.0f;
    }

#if defined(_WIN32)
    // Query system memory usage periodically
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        m_gpuTelemetry.sysRamUsageBytes = pmc.WorkingSetSize;
    }
#endif
}

void Profiler::EndFrame() {
    auto frameEndTime = std::chrono::high_resolution_clock::now();
    m_currentFrameTimeMs = std::chrono::duration<float, std::milli>(frameEndTime - m_frameStartTime).count();
    if (m_currentFrameTimeMs <= 0.001f) m_currentFrameTimeMs = 0.001f;

    m_currentFPS = 1000.0f / m_currentFrameTimeMs;

    // Store in ring buffer
    m_frameTimeHistory[m_historyIndex] = m_currentFrameTimeMs;
    m_fpsHistory[m_historyIndex] = m_currentFPS;
    m_historyIndex = (m_historyIndex + 1) % HISTORY_SIZE;

    // Calculate rolling average
    float sumFt = 0.0f;
    for (size_t i = 0; i < HISTORY_SIZE; ++i) {
        sumFt += m_frameTimeHistory[i];
    }
    m_avgFrameTimeMs = sumFt / static_cast<float>(HISTORY_SIZE);
    m_avgFPS = 1000.0f / (std::max)(0.001f, m_avgFrameTimeMs);

    // Calculate 1% Low FPS
    std::vector<float> sortedFt(m_frameTimeHistory, m_frameTimeHistory + HISTORY_SIZE);
    std::sort(sortedFt.begin(), sortedFt.end());
    size_t onePercentIdx = static_cast<size_t>(static_cast<float>(HISTORY_SIZE) * 0.99f);
    float onePercentLowFt = sortedFt[(std::min)(onePercentIdx, sortedFt.size() - 1)];
    m_onePercentLowFPS = 1000.0f / (std::max)(0.001f, onePercentLowFt);
}

void Profiler::RecordSubsystemTime(Subsystem sys, float milliseconds) {
    size_t idx = static_cast<size_t>(sys);
    if (idx < static_cast<size_t>(Subsystem::Count)) {
        m_subsystemAccum[idx] += milliseconds;
    }
}

void Profiler::DrawUI() {
#if !defined(ENGINE_RELEASE) && !defined(ENGINE_SERVER)
    if (!m_uiOpen) return;

    ImGui::SetNextWindowSize(ImVec2(700, 480), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(" Engine Profiler & Telemetry Studio", &m_uiOpen)) {
        ImGui::End();
        return;
    }

    // Top metrics dashboard
    ImGui::Columns(4, "ProfilerMetricsColumns", false);
    ImGui::Text("Frame Time:");
    ImGui::TextColored(m_currentFrameTimeMs < 16.6f ? ImVec4(0.2f, 1.0f, 0.4f, 1.0f) : ImVec4(1.0f, 0.4f, 0.2f, 1.0f),
                      "%.2f ms", m_currentFrameTimeMs);

    ImGui::NextColumn();
    ImGui::Text("Current FPS:");
    ImGui::TextColored(m_currentFPS >= 59.0f ? ImVec4(0.2f, 1.0f, 0.4f, 1.0f) : ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                      "%.1f FPS", m_currentFPS);

    ImGui::NextColumn();
    ImGui::Text("Avg (120f):");
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%.1f FPS (%.2f ms)", m_avgFPS, m_avgFrameTimeMs);

    ImGui::NextColumn();
    ImGui::Text("1%% Low:");
    ImGui::TextColored(m_onePercentLowFPS >= 30.0f ? ImVec4(0.4f, 0.8f, 1.0f, 1.0f) : ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                      "%.1f FPS", m_onePercentLowFPS);

    ImGui::Columns(1);
    ImGui::Separator();

    // Frametime Plot
    ImGui::Text("Frame Time History (ms):");
    char overlay[64];
    sprintf_s(overlay, "Current: %.2f ms", m_currentFrameTimeMs);
    ImGui::PlotLines("##FrameTimePlot", m_frameTimeHistory, HISTORY_SIZE, static_cast<int>(m_historyIndex), overlay, 0.0f, 33.3f, ImVec2(ImGui::GetContentRegionAvail().x, 70.0f));

    ImGui::Separator();

    // Subsystem Breakdown
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "CPU Subsystem Time Breakdown:");
    if (ImGui::BeginTable("SubsystemBreakdownTable", 3, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Subsystem", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Time (ms)", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("% of Frame", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < static_cast<size_t>(Subsystem::Count); ++i) {
            Subsystem sys = static_cast<Subsystem>(i);
            float timeMs = m_subsystemTimes[i];
            float pct = (m_currentFrameTimeMs > 0.001f) ? (timeMs / m_currentFrameTimeMs * 100.0f) : 0.0f;

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%s", SubsystemToString(sys));

            ImGui::TableNextColumn();
            ImGui::Text("%.3f ms", timeMs);

            ImGui::TableNextColumn();
            if (pct > 40.0f) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%.1f%%", pct);
            } else {
                ImGui::Text("%.1f%%", pct);
            }
        }
        ImGui::EndTable();
    }

    ImGui::Separator();

    // GPU & Scene Telemetry
    ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "GPU & Scene Telemetry:");
    ImGui::Columns(3, "GpuTelemetryCols", false);
    ImGui::BulletText("Draw Calls: %u", m_gpuTelemetry.drawCalls);
    ImGui::BulletText("Active Particles: %u", m_gpuTelemetry.activeParticles);

    ImGui::NextColumn();
    ImGui::BulletText("Point Lights: %u", m_gpuTelemetry.pointLights);
    ImGui::BulletText("Spot Lights: %u", m_gpuTelemetry.spotLights);

    ImGui::NextColumn();
    if (m_gpuTelemetry.sysRamUsageBytes > 0) {
        ImGui::BulletText("RAM Working Set: %.1f MB", m_gpuTelemetry.sysRamUsageBytes / (1024.0f * 1024.0f));
    }

    ImGui::Columns(1);
    ImGui::End();
#endif
}

} // namespace EngineDebug
