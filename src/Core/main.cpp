#include "Application.h"
#include "DiagnosticPaths.h"
#include "EditorConsole.h"
#include "ProjectLauncher.h"
#include "ProjectBuilder.h"
#if defined(_WIN32) && defined(ENGINE_RELEASE)
#include "PackagedProject.h"
#endif
#include <fstream>
#include <iostream>
#include <sstream>
#include <filesystem>
#include "Renderer.h"

#if defined(_WIN32)
#include <windows.h>
// D3D12 loads the matching signed Agility runtime before device creation.
extern "C" {
__declspec(dllexport) extern const UINT D3D12SDKVersion=619;
__declspec(dllexport) extern const char* D3D12SDKPath=".\\ThirdParty\\DirectX\\Agility\\";
}
#include "Security/SecuritySystem.h"
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
#if defined(ENGINE_EDITOR)
    static EngineLog::EditorConsole editorConsole;
#endif
    // Initialize COM on the main thread in STA mode (required by Microsoft WebView2 & UI Shell)
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
#if defined(ENGINE_EDITOR)
    bool addVehicle=false;
    bool startupCheck = false, benchmarkNoVSync = false, benchmarkNoRayTracing = false, benchmarkNative = false;
    unsigned benchmarkFrames = 0;
    bool benchmarkRain = false;
    bool benchmarkEditorPreview = false,benchmarkEditorWorkflow=false,benchmarkTessellation=false;
    bool benchmarkLightingFixture = false;
    float benchmarkGI=-1;
    int benchmarkAntialiasing = -1;
    int benchmarkLatency = -1;
    bool benchmarkFSRFG=false,benchmarkXeFG=false;
    bool benchmarkRR=false;
    int benchmarkUpscaler = -1, benchmarkQuality = 1;
    bool benchmarkPathTracing = false;
    float benchmarkClouds = -1.0f;
    float benchmarkSunElevation=-1000,benchmarkSunAzimuth=180,benchmarkSkyYaw=0;
    std::wistringstream arguments(pCmdLine ? pCmdLine : L"");
    std::wstring argument;
    while (arguments >> argument) {
        if(argument==L"--add-vehicle")addVehicle=true;
        else if (argument == L"--startup-check") startupCheck = true;
        else if (argument == L"--benchmark") {
            arguments >> benchmarkFrames;
            benchmarkFrames = (std::min)(benchmarkFrames, 10000u);
        } else if (argument == L"--benchmark-no-vsync") benchmarkNoVSync = true;
        else if (argument == L"--benchmark-no-raytracing") benchmarkNoRayTracing = true;
        else if (argument == L"--benchmark-native") benchmarkNative = true;
        else if (argument == L"--benchmark-rain") benchmarkRain = true;
        else if(argument==L"--benchmark-tessellation")benchmarkTessellation=true;
        else if(argument==L"--benchmark-editor-workflow")benchmarkEditorWorkflow=true;
        else if (argument == L"--benchmark-editor-ui") benchmarkEditorPreview = true;
        else if (argument == L"--benchmark-editor-lighting") benchmarkLightingFixture = true;
        else if (argument == L"--benchmark-gi") arguments >> benchmarkGI;
        else if (argument == L"--benchmark-aa") arguments >> benchmarkAntialiasing;
        else if (argument == L"--benchmark-rr") benchmarkRR=true;
        else if (argument == L"--benchmark-xess-fg") benchmarkXeFG=true;
        else if (argument == L"--benchmark-fsr-fg") benchmarkFSRFG=true;
        else if (argument == L"--benchmark-latency") arguments >> benchmarkLatency;
        else if (argument == L"--benchmark-upscale") arguments >> benchmarkUpscaler;
        else if (argument == L"--benchmark-quality") arguments >> benchmarkQuality;
        else if (argument == L"--benchmark-pathtracing") benchmarkPathTracing=true;
        else if(argument==L"--benchmark-sky") {arguments>>benchmarkSunElevation>>benchmarkSunAzimuth>>benchmarkSkyYaw;}
        else if (argument == L"--benchmark-clouds") {
            arguments >> benchmarkClouds;
            benchmarkClouds = (std::clamp)(benchmarkClouds, 0.0f, 3.0f);
        }
    }
    if (startupCheck) {
        EngineSecurity::SecurityReport report;
        bool passed = EngineSecurity::SecuritySystem::Get().CollectStartupReport(report);
        wchar_t executable[32768] {};
        GetModuleFileNameW(nullptr, executable, 32768);
        std::ofstream output(EngineDiagnostics::Directory() / L"startup_check.txt");
        output << report.summary << '\n';
        for (const auto& violation : report.violations) output << " - " << violation << '\n';
        output.close();
        CoUninitialize();
        return passed ? 0 : 2;
    }
#endif


#if defined(ENGINE_EDITOR)
    // Native project packaging does not initialize the renderer or invoke a compiler.
    {
        int count=0;auto args=CommandLineToArgvW(GetCommandLineW(),&count);
        std::filesystem::path buildProject,runtimeSource,cooker;std::string buildPlatform;
        bool buildCommand=false,installCommand=false,invalid=false;
        for(int i=1;args&&i<count;++i){std::wstring arg=args[i];
            if(arg==L"--build-project"){buildCommand=true;if(i+1<count)buildProject=args[++i];else invalid=true;}
            else if(arg==L"--install-runtime"){installCommand=true;if(i+1<count)runtimeSource=args[++i];else invalid=true;}
            else if(arg==L"--build-platform"){if(i+1<count)buildPlatform=EngineProject::Utf8(std::filesystem::path(args[++i]));else invalid=true;}
            else if(arg==L"--texture-cooker"){if(i+1<count)cooker=args[++i];else invalid=true;}
        }
        if(args)LocalFree(args);
        if(buildCommand||installCommand){
            int result=2;
            try{
                if(invalid||(buildCommand&&installCommand))throw std::runtime_error("Invalid project build command line");
                if(installCommand){
                    if(runtimeSource.empty()||cooker.empty())throw std::runtime_error("--install-runtime requires a game build directory and --texture-cooker");
                    EngineProject::ProjectBuilder::InstallRuntime(runtimeSource,buildPlatform.empty()?"standalone":buildPlatform,cooker);result=0;
                }else{
                    if(buildProject.empty())throw std::runtime_error("--build-project requires an .idkp file");
                    result=EngineProject::ProjectBuilder::Get().BuildBlocking(buildProject,buildPlatform)?0:2;
                }
            }catch(const std::exception& error){std::cerr<<"[Build] "<<error.what()<<std::endl;}
            CoUninitialize();return result;
        }
    }
#endif

    bool projectCheck=false;
    try {
        int count=0;auto args=CommandLineToArgvW(GetCommandLineW(),&count);
        std::filesystem::path projectPath,createPath;
        for(int i=1;args&&i<count;++i){
            std::wstring arg=args[i];
            if((arg==L"--project"||arg==L"--create-project")&&i+1<count){if(arg==L"--project")projectPath=args[++i];else createPath=args[++i];}
            else if(arg==L"--project-check")projectCheck=true;
            else if(std::filesystem::path(arg).extension()==L".idkp")projectPath=arg;
        }
        if(args)LocalFree(args);
#if defined(ENGINE_EDITOR)
        if(!createPath.empty())EngineProject::Create(createPath,EngineProject::ExecutableDirectory().parent_path()/"Templates/Default/Content");
#endif
        if(!projectPath.empty())EngineProject::Open(projectPath);
#if defined(ENGINE_EDITOR)
        if(!EngineProject::active&&!projectCheck&&!EngineProject::LaunchProjectPicker())return 0;
        if(!projectCheck)EngineProject::RegisterFileType();
#else
        if(!EngineProject::active)EngineProject::OpenPackagedProject();
#endif
        if(!EngineProject::active)throw std::runtime_error("A project .idkp file is required");
        std::cout<<"[Project] "<<EngineProject::current.name<<" | "<<EngineProject::Utf8(EngineProject::current.file)<<std::endl;
        if(projectCheck)return 0;
        std::filesystem::current_path(EngineProject::current.Root());
    }catch(const std::exception& e){
        std::cerr<<"[Project] "<<e.what()<<std::endl;
        if(!projectCheck)MessageBoxA(nullptr,e.what(),"Cannot open project",MB_OK|MB_ICONERROR);
        return 2;
    }
    // 0. Security Boot: Pre-flight anti-tamper, debugger, foreign DLL, process & driver integrity scan
    if (!EngineSecurity::SecuritySystem::Get().InitializeSecurityBoot()) {
#ifdef ENGINE_RELEASE
        CoUninitialize();
        return -1;
#endif
    }

    // Restored DLL search paths for restructured build
    // AddDllDirectory requires ABSOLUTE paths
    WCHAR exeDir[MAX_PATH];
    GetModuleFileNameW(NULL, exeDir, MAX_PATH);
    PathRemoveFileSpecW(exeDir);
    
    std::wstring dllDir = std::wstring(exeDir) + L"\\ThirdParty";
    std::wstring steamDir = std::wstring(exeDir) + L"\\ThirdParty\\Steamworks";
    std::wstring epicDir = std::wstring(exeDir) + L"\\ThirdParty\\EpicOnlineServices";
    std::wstring discordDir = std::wstring(exeDir) + L"\\ThirdParty\\Discord";
    std::wstring nvDir = std::wstring(exeDir) + L"\\ThirdParty\\NVIDIA\\Streamline";
    std::wstring dxDir = std::wstring(exeDir) + L"\\ThirdParty\\DirectX";
    std::wstring pxDir = std::wstring(exeDir) + L"\\ThirdParty\\NVIDIA\\PhysX";
    std::wstring amdDir = std::wstring(exeDir) + L"\\ThirdParty\\AMD";
    std::wstring intelDir = std::wstring(exeDir) + L"\\ThirdParty\\Intel";
    std::wstring dsDir = std::wstring(exeDir) + L"\\ThirdParty\\DirectStorage";

    // Enable Per-Monitor V2 DPI awareness so fullscreen and window resolutions match physical display pixels exactly
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS);
    AddDllDirectory(dllDir.c_str());
    AddDllDirectory(steamDir.c_str());
    AddDllDirectory(epicDir.c_str());
    AddDllDirectory(discordDir.c_str());
    AddDllDirectory(nvDir.c_str());
    AddDllDirectory(dxDir.c_str());
    AddDllDirectory(pxDir.c_str());
    AddDllDirectory(amdDir.c_str());
    AddDllDirectory(intelDir.c_str());
    AddDllDirectory(dsDir.c_str());

    // Pre-load Discord Social SDK DLL
    LoadLibraryExW((discordDir + L"\\discord_partner_sdk.dll").c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);

#if defined(ENGINE_PLATFORM_EPIC)
    // Pre-load EOSSDK-Win64-Shipping.dll exclusively from ThirdParty\EpicOnlineServices\ so delay-load succeeds flawlessly
    LoadLibraryExW((epicDir + L"\\EOSSDK-Win64-Shipping.dll").c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
#elif defined(ENGINE_PLATFORM_STEAM)
    // Pre-load steam_api64.dll exclusively from ThirdParty\Steamworks\ so delay-load succeeds flawlessly
    LoadLibraryExW((steamDir + L"\\steam_api64.dll").c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
#endif
    
    LOG("Application Started!");

    auto title=EngineProject::Path(EngineProject::current.name).wstring()+L" — Idontknow";
    Application app(EngineProject::current.width, EngineProject::current.height, title);
#if defined(ENGINE_RELEASE)
    // Explicit deployment smoke test: render frames and exit without saving player state.
    std::wistringstream smokeArguments(pCmdLine?pCmdLine:L"");std::wstring smokeArgument;
    while(smokeArguments>>smokeArgument)if(smokeArgument==L"--smoke-test")app.SetBenchmark(120,true,true,true);
#endif
#if defined(ENGINE_EDITOR)
    app.SetBenchmark(benchmarkFrames, benchmarkNoVSync, benchmarkNoRayTracing, benchmarkNative);
    app.SetBenchmarkWeather(benchmarkRain, benchmarkClouds);
    app.SetBenchmarkSky(benchmarkSunElevation,benchmarkSunAzimuth,benchmarkSkyYaw);
    app.SetBenchmarkEditorPreview(benchmarkEditorPreview);
    app.SetBenchmarkEditorWorkflow(benchmarkEditorWorkflow);
    app.SetBenchmarkTessellation(benchmarkTessellation);
    app.SetBenchmarkLightingFixture(benchmarkLightingFixture);
    app.SetBenchmarkGI(benchmarkGI);
    app.SetBenchmarkAntialiasing(benchmarkAntialiasing);
    app.SetBenchmarkLatency(benchmarkLatency);
    app.SetBenchmarkFSRFG(benchmarkFSRFG);app.SetBenchmarkXeFG(benchmarkXeFG);
    app.SetBenchmarkRR(benchmarkRR);
    app.SetBenchmarkUpscaler(benchmarkUpscaler, benchmarkQuality, benchmarkPathTracing);
#endif

    try {
        if (!app.Initialize()) {
            LOG("app.Initialize() failed!");
#if defined(ENGINE_EDITOR)
            if(benchmarkFrames||startupCheck)return -1;
#endif
            MessageBoxA(nullptr, "app.Initialize() failed!\nSee the Editor console and the Idontknow diagnostics directory.", "Engine Init Error", MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
            return -1;
        }

#if defined(ENGINE_EDITOR)
        if(addVehicle){app.AddVehicleToProject();return 0;}
#endif
        return app.Run();
    } catch (const std::exception& e) {
        EngineLog::Write(std::string("[FATAL] ")+e.what());
#if defined(ENGINE_EDITOR)
        if(benchmarkFrames||startupCheck)return -1;
#endif
        while (ShowCursor(TRUE) < 0);
        MessageBoxA(nullptr, e.what(), "Engine Crash / Runtime Error", MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
        return -1;
    } catch (...) {
        while (ShowCursor(TRUE) < 0);
        MessageBoxA(nullptr, "An unknown error occurred.", "Engine Crash", MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
        return -1;
    }
}

#elif defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)

#include <orbis/libkernel.h>
#include <orbis/UserService.h>
#include <orbis/Pad.h>
#include <orbis/AudioOut.h>
#include <orbis/VideoOut.h>
#include <orbis/Sysmodule.h>

static void ps4_printf(const char* fmt, ...) {
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    sceKernelDebugOutText(0, buf);
}

int main(int argc, char* argv[]) {
    ps4_printf("==================================================\n");
    ps4_printf("  Starting Idontknow on PlayStation 4 (Orbis OS)  \n");
    ps4_printf("==================================================\n");

    // 1. Initialize PS4 User Service
    ps4_printf("[Idontknow PS4] Initializing UserService...\n");
    OrbisUserServiceInitializeParams userParam = {};
    userParam.priority = ORBIS_KERNEL_PRIO_FIFO_LOWEST;
    int32_t userRes = sceUserServiceInitialize(&userParam);
    ps4_printf("[Idontknow PS4] UserService initialized with code: 0x%08x\n", userRes);

    try {
        ps4_printf("[Idontknow PS4] Creating Application (1920x1080)...\n");
        // Native PS4 1080p display
        Application app(1920, 1080, L"Idontknow PS4");

        ps4_printf("[Idontknow PS4] Calling app.Initialize()...\n");
        if (!app.Initialize()) {
            ps4_printf("[Idontknow PS4 ERROR] app.Initialize() failed!\n");
            return -1;
        }

        ps4_printf("[Idontknow PS4] Engine initialized. Entering main loop...\n");
        int exitCode = app.Run();
        ps4_printf("[Idontknow PS4] Application exited with code: %d\n", exitCode);
        return exitCode;
    } catch (const std::exception& e) {
        EngineLog::Write(std::string("[FATAL] ")+e.what());
        ps4_printf("[Idontknow PS4 FATAL] Exception: %s\n", e.what());
        return -1;
    } catch (...) {
        ps4_printf("[Idontknow PS4 FATAL] Unknown exception occurred.\n");
        return -1;
    }
}

#else

int main(int argc, char* argv[]) {
    try {
        std::cout << "[Idontknow] Starting on macOS Apple Silicon..." << std::endl;
        auto title=EngineProject::Path(EngineProject::current.name).wstring()+L" — Idontknow";
    Application app(EngineProject::current.width, EngineProject::current.height, title);

        if (!app.Initialize()) {
            std::cerr << "[Idontknow ERROR] app.Initialize() failed!" << std::endl;
            return -1;
        }

        return app.Run();
    } catch (const std::exception& e) {
        EngineLog::Write(std::string("[FATAL] ")+e.what());
        std::cerr << "[Idontknow FATAL] Runtime error: " << e.what() << std::endl;
        return -1;
    } catch (...) {
        std::cerr << "[Idontknow FATAL] An unknown error occurred." << std::endl;
        return -1;
    }
}

#endif
