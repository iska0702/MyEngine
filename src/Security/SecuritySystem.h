#pragma once

#if defined(_WIN32)
#include <windows.h>
#else
#include "Platform/PlatformTypes.h"
#endif
#include <cstdint>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <functional>
#include <utility>

namespace EngineSecurity {

enum class SecurityViolationType {
    None = 0,
    DebuggerDetected,
    HardwareBreakpointDetected,
    BlacklistedProcessDetected,
    BlacklistedServiceDetected,
    BlacklistedWindowDetected,
    InjectedModuleDetected,
    ApiHookDetected,
    CodeIntegrityViolation,
    SteamLicenseTampered,
    DllTamperedOrSubstituted,
    DllHijackDetected,
    DllSignatureInvalid,
    HardwareMismatchDetected,
    LicenseTampered,
    HypervisorSandboxDetected,
    TimingAnomalyDetected,
    MemoryProtectionTampered
};

struct SecurityReport {
    bool passed = true;
    std::vector<std::string> violations;
    std::string summary;
};

class SecuritySystem {
public:
    static SecuritySystem& Get();

    // Runs full comprehensive Security Boot check prior to engine initialization
    bool InitializeSecurityBoot();

    // Runs a complete on-demand integrity audit across processes, services, modules & memory
    bool PerformIntegrityCheck(SecurityReport& outReport);
    // Bounded local code check for network challenges; avoids disk/process scans on the game thread.
    bool PerformNetworkIntegrityCheck(uint32_t& flags, uint64_t& codeHash);
    // Collect the complete startup report before deciding whether to launch.
    bool CollectStartupReport(SecurityReport& outReport);

    // Starts background watchdog heartbeat thread
    void StartWatchdog();

    // Stops background watchdog
    void Shutdown();

    bool IsWatchdogRunning() const { return m_watchdogRunning.load(); }
    SecurityReport GetLastReport() const {
        std::lock_guard<std::mutex> lock(m_reportMutex);
        return m_lastReport;
    }

    // Register custom callback on violation
    void SetViolationCallback(std::function<void(SecurityViolationType, const std::string&)> callback) {
        std::lock_guard<std::mutex> lock(m_reportMutex);
        m_violationCallback = std::move(callback);
    }

private:
    SecuritySystem();
    ~SecuritySystem();

    SecuritySystem(const SecuritySystem&) = delete;
    SecuritySystem& operator=(const SecuritySystem&) = delete;

    // Security Checks
    bool CheckDebuggers(SecurityReport& report);
    bool CheckHardwareBreakpoints(SecurityReport& report);
    bool CheckRunningProcesses(SecurityReport& report);
    bool CheckSystemServicesAndDrivers(SecurityReport& report);
    bool CheckLoadedModules(SecurityReport& report);
    bool CheckApiHooks(SecurityReport& report);
    bool CheckCodeIntegrity(SecurityReport& report);
    bool CheckSteamLicenseIntegrity(SecurityReport& report);
    bool CheckDllIntegrity(SecurityReport& report);

    // Advanced Anti-Tamper & HWID/License Checks
    bool CheckContinuousMemoryIntegrity(SecurityReport& report);
    bool CheckHypervisorAndTiming(SecurityReport& report);
    bool CheckHardwareLicense(SecurityReport& report);

    bool CheckRootDllHijacking(const std::wstring& exeDir, SecurityReport& report);
    bool CheckDllManifest(const std::wstring& exeDir, SecurityReport& report);
    bool CheckLoadedDllHooksAndPaths(SecurityReport& report);

    static bool VerifyAuthenticodeSignature(const std::wstring& filePath, std::string* outSubject = nullptr);
    static bool ComputeFileSha256(const std::wstring& filePath, std::string& outHex);

    void InitializeTextSectionBaseline();
    void HideThreadFromDebugger(HANDLE hThread);

    void HandleViolation(SecurityViolationType type, const std::string& description);
    void WatchdogWorker();

    std::atomic<bool> m_watchdogRunning{ false };
    std::atomic<bool> m_collectingBootReport{ false };
    std::thread m_watchdogThread;
    mutable std::mutex m_reportMutex;
    std::mutex m_integrityMutex;
    SecurityReport m_lastReport;
    std::function<void(SecurityViolationType, const std::string&)> m_violationCallback;

    // Baseline code hash for integrity verification
    uint64_t m_baselineCodeHash = 0;
    uintptr_t m_codeSectionBase = 0;
    size_t m_codeSectionSize = 0;

    // Block-wise .text section CRC verification table
    struct CodeBlockCrc {
        uintptr_t offset;
        size_t size;
        uint32_t expectedCrc;
    };
    std::vector<CodeBlockCrc> m_codeBlockCrcs;
    bool m_baselineInitialized = false;
};

} // namespace EngineSecurity
