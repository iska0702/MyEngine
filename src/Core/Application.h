#pragma once
#include <string>
#include <memory>
#include "EditorHistory.h"

namespace EngineScene { class GameplayRuntime; }
class Window;
class Renderer;
class Camera;
class Sequencer;

class Application {
public:
    Application(int width, int height, const std::wstring& title);
    ~Application();

    bool Initialize();
    int Run();
    void SetBenchmark(unsigned frameLimit, bool disableVSync = false, bool disableRayTracing = false, bool nativeResolution = false) {
        m_benchmarkFrameLimit = frameLimit;
        m_benchmarkDisableVSync = disableVSync;
        m_benchmarkDisableRayTracing = disableRayTracing;
        m_benchmarkNativeResolution = nativeResolution;
    }

    bool SaveScene();
    void AddVehicleToProject();
    std::string CaptureScene();
    void ApplyScene(const std::string& sceneData);
    void SetBenchmarkEditorWorkflow(bool enabled){m_benchmarkEditorWorkflow=enabled;}
    void SetBenchmarkTessellation(bool value){m_benchmarkTessellation=value;}
    bool m_benchmarkTessellation=false;
    bool RunEditorWorkflowChecks();
    void SetBenchmarkEditorPreview(bool enabled) { m_benchmarkEditorPreview = enabled; }
    void SetBenchmarkLightingFixture(bool enabled) { m_benchmarkLightingFixture = enabled; }
    void SetBenchmarkGI(float intensity){m_benchmarkGI=intensity;}
    void SetBenchmarkAntialiasing(int mode) { m_benchmarkAntialiasing = mode; }
    void SetBenchmarkXeFG(bool enabled) { m_benchmarkXeFG=enabled; }
    void SetBenchmarkRR(bool enabled) { m_benchmarkRR=enabled; }
    void SetBenchmarkFSRFG(bool enabled) { m_benchmarkFSRFG=enabled; }
    void SetBenchmarkLatency(int mode) { m_benchmarkLatency=mode; }
    void SetBenchmarkUpscaler(int technique, int quality, bool pathTracing) {
        m_benchmarkUpscaler=technique; m_benchmarkQuality=quality; m_benchmarkPathTracing=pathTracing;
    }
    void SetBenchmarkWeather(bool rain, float cloudDensity) {
        m_benchmarkRain = rain;
        m_benchmarkCloudDensity = cloudDensity;
    }
    void SetBenchmarkSky(float elevation,float azimuth,float yaw){m_benchmarkSunElevation=elevation;m_benchmarkSunAzimuth=azimuth;m_benchmarkSkyYaw=yaw;}
    void LoadScene();

private:
    std::unique_ptr<EngineScene::GameplayRuntime> m_gameplay;
    EngineEditor::History m_editHistory;
    std::string m_playSnapshot;
    bool m_wasEditing=true;
    bool m_historyChanged=false;
    int m_recoverySlot=0;
    int m_autosaveSeconds=60;
    int m_autosaveElapsed=0;
    bool m_recoveryChecked=false;
    bool m_showRecovery=false;
    int m_width;
    int m_height;
    std::wstring m_title;

    std::unique_ptr<Window> m_window;
    std::unique_ptr<Renderer> m_renderer;
    std::unique_ptr<Camera> m_camera;
    std::unique_ptr<Sequencer> m_sequencer;
    float m_fps = 0.0f;
    unsigned m_benchmarkFrameLimit = 0;
    bool m_benchmarkDisableVSync = false;
    bool m_benchmarkDisableRayTracing = false;
    bool m_loadedCleanWorld = false;
    bool m_benchmarkNativeResolution = false;
    bool m_benchmarkRain = false;
    float m_benchmarkCloudDensity = -1.0f;
    float m_benchmarkSunElevation=-1000,m_benchmarkSunAzimuth=180,m_benchmarkSkyYaw=0;
    bool m_benchmarkEditorPreview = false;
    bool m_benchmarkEditorWorkflow=false;
    bool m_benchmarkLightingFixture = false;
    float m_benchmarkGI=-1;
    int m_benchmarkAntialiasing = -1;
    int m_benchmarkLatency = -1;
    bool m_benchmarkFSRFG=false,m_benchmarkXeFG=false;
    bool m_benchmarkRR=false;
    int m_benchmarkUpscaler = -1, m_benchmarkQuality = 1;
    bool m_benchmarkPathTracing = false;
};
