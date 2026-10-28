#include "SecuritySystem.h"
#include "Core/DiagnosticPaths.h"
#if defined(_WIN32)
#include <tlhelp32.h>
#include <psapi.h>
#include <winternl.h>
#include <shlwapi.h>
#include <filesystem>
#include <algorithm>
#include <cwctype>
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <unordered_set>
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>
#include <bcrypt.h>
#include <intrin.h>

#include "MBAObfuscator.h"
#include "VirtualMachine.h"
#include "HardwareID.h"
#include "LicenseSystem.h"
#include "Storage/ContentFile.h"
#include "Storage/ArchiveAuthentication.h"
#include "Storage/Crypto.h"
#include "Storage/PakCompression.h"

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "wintrust.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "bcrypt.lib")

namespace EngineSecurity {
static void AppendSecurityLog(const std::string& message) {
    wchar_t executable[32768] {};
    if (GetModuleFileNameW(nullptr, executable, 32768) == 0) return;
    std::ofstream log(EngineDiagnostics::Directory() / L"security.log", std::ios::app);
    log << message << std::endl;
}

// Typedef for NtQueryInformationProcess
typedef NTSTATUS(NTAPI* pfnNtQueryInformationProcess)(
    HANDLE ProcessHandle,
    ULONG ProcessInformationClass,
    PVOID ProcessInformation,
    ULONG ProcessInformationLength,
    PULONG ReturnLength
);

// Typedef for NtSetInformationThread
typedef NTSTATUS(NTAPI* pfnNtSetInformationThread)(
    HANDLE ThreadHandle,
    ULONG ThreadInformationClass,
    PVOID ThreadInformation,
    ULONG ThreadInformationLength
);

// FNV-1a 64-bit hash
static uint64_t Fnv1a64(const uint8_t* data, size_t length) {
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < length; ++i) {
        hash ^= data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static std::string ToLower(const std::string& str) {
    std::string out = str;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

static std::wstring ToLowerW(const std::wstring& str) {
    std::wstring out = str;
    std::transform(out.begin(), out.end(), out.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(::towlower(c));
    });
    return out;
}

SecuritySystem& SecuritySystem::Get() {
    static SecuritySystem s_instance;
    return s_instance;
}

SecuritySystem::SecuritySystem() {
    InitializeTextSectionBaseline();
}

void SecuritySystem::InitializeTextSectionBaseline() {
    if (m_baselineInitialized) return;

    HMODULE hExe = GetModuleHandleW(nullptr);
    if (!hExe) return;

    MODULEINFO moduleInfo{};
    if (!GetModuleInformation(GetCurrentProcess(), hExe, &moduleInfo, sizeof(moduleInfo))) return;
    const size_t imageSize = moduleInfo.SizeOfImage;
    if (imageSize < sizeof(IMAGE_DOS_HEADER)) return;
    PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)hExe;
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) return;

    if (dosHeader->e_lfanew < 0 || static_cast<size_t>(dosHeader->e_lfanew) > imageSize ||
        sizeof(IMAGE_NT_HEADERS) > imageSize - static_cast<size_t>(dosHeader->e_lfanew)) return;
    PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)((uintptr_t)hExe + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) return;

    PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(ntHeaders);
    const size_t sectionOffset = reinterpret_cast<uintptr_t>(section) - reinterpret_cast<uintptr_t>(hExe);
    if (sectionOffset > imageSize || ntHeaders->FileHeader.NumberOfSections >
        (imageSize - sectionOffset) / sizeof(IMAGE_SECTION_HEADER)) return;
    for (WORD i = 0; i < ntHeaders->FileHeader.NumberOfSections; ++i, ++section) {
        char secName[9] = {};
        memcpy(secName, section->Name, 8);
        if (strcmp(secName, ".text") == 0) {
            if (section->VirtualAddress > imageSize || section->Misc.VirtualSize > imageSize - section->VirtualAddress) return;
            m_codeSectionBase = (uintptr_t)hExe + section->VirtualAddress;
            m_codeSectionSize = section->Misc.VirtualSize;
            if (m_codeSectionBase && m_codeSectionSize > 0) {
                m_baselineCodeHash = MBAObfuscator::FNV1a64((const uint8_t*)m_codeSectionBase, m_codeSectionSize);

                // Build block-wise CRC table (4KB blocks)
                const size_t blockSize = 4096;
                m_codeBlockCrcs.clear();
                for (size_t offset = 0; offset < m_codeSectionSize; offset += blockSize) {
                    size_t curBlockSize = (std::min)(blockSize, m_codeSectionSize - offset);
                    uint32_t blockCrc = MBAObfuscator::CRC32((const uint8_t*)(m_codeSectionBase + offset), curBlockSize);
                    m_codeBlockCrcs.push_back({ offset, curBlockSize, blockCrc });
                }
                m_baselineInitialized = true;
            }
            break;
        }
    }
}

void SecuritySystem::HideThreadFromDebugger(HANDLE hThread) {
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (hNtdll) {
        auto pfnSet = (pfnNtSetInformationThread)GetProcAddress(hNtdll, "NtSetInformationThread");
        if (pfnSet) {
            pfnSet(hThread, 0x11 /* ThreadHideFromDebugger */, nullptr, 0);
        }
    }
}

SecuritySystem::~SecuritySystem() {
    Shutdown();
}

bool SecuritySystem::InitializeSecurityBoot() {
    SecurityReport report;
    bool secure = CollectStartupReport(report);
    AppendSecurityLog(report.summary);
    for (const auto& violation : report.violations) AppendSecurityLog(" - " + violation);

    {
        std::lock_guard<std::mutex> lock(m_reportMutex);
        m_lastReport = report;
    }

    if (!secure) {
        std::string errMessage = "Security Boot Integrity Violation:\n";
        for (const auto& v : report.violations) {
            errMessage += " - " + v + "\n";
        }
        
#ifdef ENGINE_RELEASE
        // In Release builds, enforce immediate termination on tamper/crack attempt
        while (ShowCursor(TRUE) < 0);
        MessageBoxA(nullptr, errMessage.c_str(), "Security Boot Failure - Anti-Tamper", MB_OK | MB_ICONSTOP | MB_TOPMOST);
        ExitProcess(0xC0000005);
        return false;
#else
        std::ofstream logFile(EngineDiagnostics::Directory() / "log.txt", std::ios::app);
        logFile << "[SecuritySystem Warning] " << errMessage << std::endl;
        OutputDebugStringA(errMessage.c_str());
#endif
    }

    StartWatchdog();
    return secure;
}

bool SecuritySystem::PerformIntegrityCheck(SecurityReport& outReport) {
    std::lock_guard<std::mutex> auditLock(m_integrityMutex);
    outReport.passed = true;
    outReport.violations.clear();

    CheckDebuggers(outReport);
    CheckHardwareBreakpoints(outReport);
    CheckRunningProcesses(outReport);
    CheckSystemServicesAndDrivers(outReport);
    CheckLoadedModules(outReport);
    CheckDllIntegrity(outReport);
    CheckApiHooks(outReport);
    CheckCodeIntegrity(outReport);
    CheckContinuousMemoryIntegrity(outReport);
    CheckHypervisorAndTiming(outReport);
    CheckHardwareLicense(outReport);
    CheckSteamLicenseIntegrity(outReport);

    if (!outReport.violations.empty()) {
        outReport.passed = false;
        std::ostringstream ss;
        ss << outReport.violations.size() << " security threat(s) detected.";
        outReport.summary = ss.str();
    } else {
        outReport.summary = "All integrity and security boot checks passed.";
    }

    return outReport.passed;
}

bool SecuritySystem::PerformNetworkIntegrityCheck(uint32_t& flags, uint64_t& codeHash) {
    std::lock_guard<std::mutex> auditLock(m_integrityMutex);
    flags = 0;
    codeHash = 0;
    SecurityReport report;
    if (!CheckContinuousMemoryIntegrity(report)) flags |= 8u;
    if (!m_baselineInitialized) flags |= 0x80000000u;
    else codeHash = m_baselineCodeHash;
    return flags == 0;
}

bool SecuritySystem::CollectStartupReport(SecurityReport& outReport) {
    m_collectingBootReport.store(true);
    struct Reset {
        std::atomic<bool>& collecting;
        ~Reset() { collecting.store(false); }
    } reset { m_collectingBootReport };
    return PerformIntegrityCheck(outReport);
}

bool SecuritySystem::CheckDebuggers(SecurityReport& report) {
    bool detected = false;

    // 1. Standard Win32 API Check
    if (IsDebuggerPresent()) {
        report.violations.push_back("Active User-Mode Debugger Attached (IsDebuggerPresent)");
        detected = true;
    }

    // 2. CheckRemoteDebuggerPresent
    BOOL isRemote = FALSE;
    if (CheckRemoteDebuggerPresent(GetCurrentProcess(), &isRemote) && isRemote) {
        report.violations.push_back("Remote Debugger Attached (CheckRemoteDebuggerPresent)");
        detected = true;
    }

    // 3. Direct PEB Checks (x64)
#if defined(_M_X64) || defined(__x86_64__)
    uint8_t* pPeb = (uint8_t*)__readgsqword(0x60);
    if (pPeb) {
        uint8_t beingDebugged = pPeb[0x02];
        if (beingDebugged != 0) {
            report.violations.push_back("Direct PEB BeingDebugged flag set");
            detected = true;
        }

        DWORD ntGlobalFlag = *(DWORD*)(pPeb + 0xBC);
        if (ntGlobalFlag & 0x70) { // FLG_HEAP_ENABLE_TAIL_CHECK | FLG_HEAP_ENABLE_FREE_CHECK | FLG_HEAP_VALIDATE_PARAMETERS
            report.violations.push_back("PEB NtGlobalFlag indicates debugger heap instrumentation");
            detected = true;
        }

        // ProcessHeap Debug Flags & ForceFlags
        uint8_t* pProcessHeap = *(uint8_t**)(pPeb + 0x30);
        if (pProcessHeap) {
            DWORD heapFlags = *(DWORD*)(pProcessHeap + 0x70);
            DWORD heapForceFlags = *(DWORD*)(pProcessHeap + 0x74);
            // In clean execution heapFlags == 0x2 (HEAP_GROWABLE) and ForceFlags == 0.
            if ((heapFlags & ~0x00000002) != 0 || heapForceFlags != 0) {
                report.violations.push_back("PEB ProcessHeap indicates active debugger heap tracking (Flags/ForceFlags)");
                detected = true;
            }
        }
    }
#endif

    // 4. NtQueryInformationProcess (ProcessDebugPort = 7, ProcessDebugFlags = 0x1F, ProcessDebugObjectHandle = 0x1E)
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (hNtdll) {
        auto pfnQuery = (pfnNtQueryInformationProcess)GetProcAddress(hNtdll, "NtQueryInformationProcess");
        if (pfnQuery) {
            DWORD_PTR debugPort = 0;
            ULONG retLen = 0;
            if (NT_SUCCESS(pfnQuery(GetCurrentProcess(), 7 /* ProcessDebugPort */, &debugPort, sizeof(debugPort), &retLen))) {
                if (debugPort != 0) {
                    report.violations.push_back("NtQueryInformationProcess detected active ProcessDebugPort");
                    detected = true;
                }
            }

            HANDLE hDebugObject = nullptr;
            if (NT_SUCCESS(pfnQuery(GetCurrentProcess(), 0x1E /* ProcessDebugObjectHandle */, &hDebugObject, sizeof(hDebugObject), &retLen))) {
                if (hDebugObject != nullptr) {
                    report.violations.push_back("NtQueryInformationProcess detected active DebugObjectHandle");
                    detected = true;
                }
            }

            DWORD debugFlags = 0;
            if (NT_SUCCESS(pfnQuery(GetCurrentProcess(), 0x1F /* ProcessDebugFlags */, &debugFlags, sizeof(debugFlags), &retLen))) {
                if (debugFlags == 0) { // 0 means debugger is attached
                    report.violations.push_back("NtQueryInformationProcess ProcessDebugFlags indicates attached debugger");
                    detected = true;
                }
            }
        }

        // 5. Check DbgBreakPoint hooking in ntdll
        FARPROC pDbgBreakPoint = GetProcAddress(hNtdll, "DbgBreakPoint");
        if (pDbgBreakPoint) {
            const uint8_t* pB = (const uint8_t*)pDbgBreakPoint;
            if (pB[0] == 0xE9 || (pB[0] == 0xFF && pB[1] == 0x25)) {
                report.violations.push_back("ntdll!DbgBreakPoint hook detected (Anti-Anti-Debug evasion)");
                detected = true;
            }
        }
    }

    return !detected;
}

bool SecuritySystem::CheckHardwareBreakpoints(SecurityReport& report) {
    bool detected = false;
    CONTEXT ctx = {};
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    HANDLE hThread = GetCurrentThread();

    if (GetThreadContext(hThread, &ctx)) {
        if (ctx.Dr0 != 0 || ctx.Dr1 != 0 || ctx.Dr2 != 0 || ctx.Dr3 != 0 || (ctx.Dr7 & 0xFF) != 0) {
            report.violations.push_back("Hardware Breakpoint (DR0-DR3/DR7) detected in thread context");
            detected = true;
        }
    }

    return !detected;
}

struct EnumWindowsContext {
    std::vector<std::string>* pViolations;
    bool detected;
};

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    EnumWindowsContext* ctx = reinterpret_cast<EnumWindowsContext*>(lParam);
    if (!ctx || !IsWindowVisible(hwnd)) return TRUE;

    char title[256] = {};
    if (GetWindowTextA(hwnd, title, sizeof(title)) > 0) {
        std::string lowTitle = ToLower(title);
        static const char* s_blacklistedTitles[] = {
            "cheat engine", "x64dbg", "x32dbg", "ida pro", "process hacker",
            "scylla", "dnspy", "ollydbg", "hyperdbg", "reclass.net", "extreme injector",
            "cheatengine", "megadumper", "artmoney", "hxd hex editor"
        };

        for (const char* pattern : s_blacklistedTitles) {
            if (lowTitle.find(pattern) != std::string::npos) {
                ctx->pViolations->push_back("Blacklisted Reverse Engineering Tool Window Active: \"" + std::string(title) + "\"");
                ctx->detected = true;
                break;
            }
        }
    }
    return TRUE;
}

bool SecuritySystem::CheckRunningProcesses(SecurityReport& report) {
    bool detected = false;

    // 1. Process Snapshot Scan
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe = {};
        pe.dwSize = sizeof(pe);

        static const std::wstring s_blacklistedProcesses[] = {
            L"cheatengine.exe", L"cheatengine-x86_64.exe", L"cheatengine-x86_64-sse4-avx2.exe",
            L"x64dbg.exe", L"x32dbg.exe", L"ida.exe", L"ida64.exe", L"idaw.exe", L"idaw64.exe",
            L"idag.exe", L"idag64.exe", L"ghidra.exe", L"processhacker.exe", L"scylla.exe",
            L"scylla_x64.exe", L"scylla_x86.exe", L"dnspy.exe", L"dnspy.console.exe",
            L"wireshark.exe", L"fiddler.exe", L"artmoney.exe", L"artmoneypro.exe",
            L"ollydbg.exe", L"hyperdbg.exe", L"hxd.exe", L"hxd64.exe", L"pestudio.exe",
            L"die.exe", L"megadumper.exe", L"reclass.net.exe", L"reclass64.exe",
            L"extremeinjector.exe", L"xenos.exe", L"xenos64.exe", L"injector.exe",
            L"goldberg.exe", L"creamapi.exe", L"greenluma.exe", L"koaloader.exe", L"smokeapi.exe"
        };

        if (Process32FirstW(hSnap, &pe)) {
            do {
                std::wstring exeName = ToLowerW(pe.szExeFile);
                for (const auto& badProcess : s_blacklistedProcesses) {
                    if (exeName == badProcess) {
                        char buf[MAX_PATH];
                        WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, buf, sizeof(buf), nullptr, nullptr);
                        report.violations.push_back("Prohibited Piracy / Tampering Process Detected: " + std::string(buf));
                        detected = true;
                        break;
                    }
                }
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }

    // 2. Active Window Title Scan
    EnumWindowsContext winCtx;
    winCtx.pViolations = &report.violations;
    winCtx.detected = false;
    EnumWindows(EnumWindowsProc, (LPARAM)&winCtx);
    if (winCtx.detected) {
        detected = true;
    }

    return !detected;
}

bool SecuritySystem::CheckSystemServicesAndDrivers(SecurityReport& report) {
    bool detected = false;

    // Check known cheat / kernel bypass driver device handles
    static const char* s_driverDeviceNames[] = {
        "\\\\.\\DBK64",
        "\\\\.\\KProcessHacker",
        "\\\\.\\KProcessHacker2",
        "\\\\.\\KProcessHacker3",
        "\\\\.\\BlackBone",
        "\\\\.\\Capcom",
        "\\\\.\\GDRV"
    };

    for (const char* devName : s_driverDeviceNames) {
        HANDLE hDev = CreateFileA(devName, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (hDev != INVALID_HANDLE_VALUE) {
            CloseHandle(hDev);
            report.violations.push_back("Unauthorized Kernel Tamper Driver Device Detected: " + std::string(devName));
            detected = true;
        }
    }

    return !detected;
}

bool SecuritySystem::CheckLoadedModules(SecurityReport& report) {
    bool detected = false;
    DWORD pid = GetCurrentProcessId();
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);

    if (hSnap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W me = {};
        me.dwSize = sizeof(me);

        WCHAR exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        PathRemoveFileSpecW(exePath);
        std::wstring exeDir = ToLowerW(exePath);

        WCHAR winDir[MAX_PATH] = {};
        GetWindowsDirectoryW(winDir, MAX_PATH);
        std::wstring sysDir = ToLowerW(winDir);

        static const std::wstring s_blacklistedModules[] = {
            L"cream_api.dll", L"cream_api64.dll", L"goldberg.dll", L"smokeapi.dll",
            L"koaloader.dll", L"speedhack.dll", L"speedhack-x86_64.dll",
            L"vehdebug-x86_64.dll", L"cheatengine-x86_64.dll", L"x64dbg.dll",
            L"minhook.dll", L"minhook.x64.dll", L"proxy_steam.dll", L"steam_api64_emu.dll"
        };

        if (Module32FirstW(hSnap, &me)) {
            do {
                std::wstring modName = ToLowerW(me.szModule);
                std::wstring modPath = ToLowerW(me.szExePath);

                // 1. Blacklisted module names
                for (const auto& badMod : s_blacklistedModules) {
                    if (modName == badMod) {
                        char buf[MAX_PATH];
                        WideCharToMultiByte(CP_UTF8, 0, me.szModule, -1, buf, sizeof(buf), nullptr, nullptr);
                        report.violations.push_back("Prohibited Injected Tamper DLL Detected: " + std::string(buf));
                        detected = true;
                        break;
                    }
                }

                // 2. DLL Hijacking detection (version.dll, dinput8.dll, dxgi.dll, winmm.dll placed in local game folder)
                if (modName == L"version.dll" || modName == L"winmm.dll") {
                    if (modPath.find(exeDir) != std::wstring::npos && modPath.find(sysDir) == std::wstring::npos) {
                        char buf[MAX_PATH];
                        WideCharToMultiByte(CP_UTF8, 0, me.szExePath, -1, buf, sizeof(buf), nullptr, nullptr);
                        report.violations.push_back("Hijacked Proxy DLL Detected (Local Game Directory): " + std::string(buf));
                        detected = true;
                    }
                }
            } while (Module32NextW(hSnap, &me));
        }
        CloseHandle(hSnap);
    }

    return !detected;
}

static bool IsAddressInValidSystemModule(uintptr_t addr) {
    if (addr == 0) return false;

    HMODULE hMod = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)addr, &hMod) && hMod != nullptr) {
        WCHAR modPath[MAX_PATH] = {};
        if (GetModuleFileNameW(hMod, modPath, MAX_PATH) > 0) {
            WCHAR winDir[MAX_PATH] = {};
            GetWindowsDirectoryW(winDir, MAX_PATH);

            std::wstring pathStr = ToLowerW(modPath);
            std::wstring winDirStr = ToLowerW(winDir);

            // 1. If the module resides anywhere inside Windows system hierarchy (System32, SysWOW64, WinSxS, DriverStore)
            if (pathStr.find(winDirStr) != std::wstring::npos) {
                return true;
            }

            // 2. Allow legitimate Steam, Epic, GPU driver, DirectX Streamline, and engine modules
            static const std::wstring s_allowedModuleSubstrings[] = {
                L"gameoverlayrenderer", L"steam_api", L"steamclient",
                L"eossdk", L"epicgames", L"epiconlineservices",
                L"nvspcap", L"nvldumd", L"nvwgf2um", L"nvcuda", L"amd", L"ati", L"igd",
                L"discordhook", L"rtsshooks", L"idontknow", L"sl.interposer",
                L"sl.dlss", L"sl.reflex", L"sl.nis", L"sl.common",
                L"libxess", L"libxess_fg", L"libxell", L"libxess_dx11", L"igfx", L"ze_",
                L"dstorage", L"dstoragecore"
            };

            for (const auto& allowed : s_allowedModuleSubstrings) {
                if (pathStr.find(allowed) != std::wstring::npos) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool SecuritySystem::CheckApiHooks(SecurityReport& report) {
    bool detected = false;

    struct ApiTarget {
        const char* moduleName;
        const char* funcName;
        bool isNativeSyscall;
    };

    static const ApiTarget s_criticalApis[] = {
        { "kernel32.dll", "IsDebuggerPresent", false },
        { "kernel32.dll", "CheckRemoteDebuggerPresent", false },
        { "ntdll.dll",    "NtQueryInformationProcess", true },
        { "ntdll.dll",    "NtProtectVirtualMemory", true }
    };

    for (const auto& api : s_criticalApis) {
        HMODULE hMod = GetModuleHandleA(api.moduleName);
        if (!hMod) continue;

        uint8_t* pFunc = (uint8_t*)GetProcAddress(hMod, api.funcName);
        if (!pFunc) continue;

        // 1. Software Breakpoint (INT3 0xCC)
        if (pFunc[0] == 0xCC) {
            report.violations.push_back("Software Breakpoint (INT3) on API: " + std::string(api.moduleName) + "!" + std::string(api.funcName));
            detected = true;
            continue;
        }

        // 2. Relative JMP 0xE9
        if (pFunc[0] == 0xE9) {
            int32_t rel = *(int32_t*)(pFunc + 1);
            uintptr_t dest = (uintptr_t)pFunc + 5 + rel;
            if (!IsAddressInValidSystemModule(dest)) {
                report.violations.push_back("Malicious Inline Hook / Detour on API: " + std::string(api.moduleName) + "!" + std::string(api.funcName));
                detected = true;
            }
            continue;
        }

        // 3. Short JMP 0xEB
        if (pFunc[0] == 0xEB) {
            int8_t rel = *(int8_t*)(pFunc + 1);
            uintptr_t dest = (uintptr_t)pFunc + 2 + rel;
            if (!IsAddressInValidSystemModule(dest)) {
                report.violations.push_back("Malicious Short Jump Detour on API: " + std::string(api.moduleName) + "!" + std::string(api.funcName));
                detected = true;
            }
            continue;
        }

        // 4. Indirect JMP 0xFF 0x25
        if (pFunc[0] == 0xFF && pFunc[1] == 0x25) {
            int32_t ripOffset = *(int32_t*)(pFunc + 2);
            uintptr_t* pDest = (uintptr_t*)((uintptr_t)pFunc + 6 + ripOffset);
            if (pDest) {
                uintptr_t dest = *pDest;
                if (!IsAddressInValidSystemModule(dest)) {
                    report.violations.push_back("Malicious Indirect Jump Detour on API: " + std::string(api.moduleName) + "!" + std::string(api.funcName));
                    detected = true;
                }
            }
            continue;
        }

        // 5. Check native ntdll syscall stubs (unless legitimately hooked by recognized overlay/driver)
        if (api.isNativeSyscall) {
            // In x64 Windows, standard syscall starts with mov r10, rcx (4C 8B D1)
            if (pFunc[0] != 0x4C || pFunc[1] != 0x8B || pFunc[2] != 0xD1) {
                // If it's hooked, check if the hook destination is in a valid module
                bool validHook = false;
                if (pFunc[0] == 0xE9) {
                    int32_t rel = *(int32_t*)(pFunc + 1);
                    uintptr_t dest = (uintptr_t)pFunc + 5 + rel;
                    validHook = IsAddressInValidSystemModule(dest);
                } else if (pFunc[0] == 0xFF && pFunc[1] == 0x25) {
                    int32_t ripOffset = *(int32_t*)(pFunc + 2);
                    uintptr_t* pDest = (uintptr_t*)((uintptr_t)pFunc + 6 + ripOffset);
                    if (pDest) validHook = IsAddressInValidSystemModule(*pDest);
                }

                if (!validHook) {
                    report.violations.push_back("Tampered ntdll Syscall Stub on: " + std::string(api.funcName));
                    detected = true;
                }
            }
        }
    }

    return !detected;
}

bool SecuritySystem::CheckCodeIntegrity(SecurityReport& report) {
    if (m_codeSectionBase == 0 || m_codeSectionSize == 0) {
        return true;
    }

    // 1. Verify that the executable code section memory page protection is PAGE_EXECUTE_READ
    // and has not been altered to PAGE_EXECUTE_READWRITE by runtime memory patchers/cheats
    MEMORY_BASIC_INFORMATION mbi = {};
    if (VirtualQuery((LPCVOID)m_codeSectionBase, &mbi, sizeof(mbi))) {
        if (mbi.Protect == PAGE_EXECUTE_READWRITE || mbi.Protect == PAGE_READWRITE) {
            report.violations.push_back("Executable Code Section Memory Protection Compromised (PAGE_EXECUTE_READWRITE Patch Detected)");
            return false;
        }
    }

    // 2. Scan entry point of the code section for software debugger breakpoints (INT3 0xCC)
    const uint8_t* pCode = (const uint8_t*)m_codeSectionBase;
    for (size_t i = 0; i < 64 && i < m_codeSectionSize; ++i) {
        if (pCode[i] == 0xCC) {
            report.violations.push_back("Software Breakpoint (INT3) Detected in Executable Entry");
            return false;
        }
    }

    return true;
}

bool SecuritySystem::CheckSteamLicenseIntegrity(SecurityReport& report) {
    wchar_t exe[32768]{};auto length=GetModuleFileNameW(nullptr,exe,32768);
    if(!length||length>=32768||!EngineStorage::AuthenticateArchiveHeaders(std::filesystem::path(exe).parent_path()/L"Contents")){
        report.violations.push_back("Content archive missing or AES-256-GCM authentication failed");
        HandleViolation(SecurityViolationType::DllTamperedOrSubstituted,"Content archive authentication failed");
        return false;
    }
    return true;
}

bool SecuritySystem::ComputeFileSha256(const std::wstring& filePath, std::string& outHex) {
    outHex.clear();
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status)) {
        CloseHandle(hFile);
        return false;
    }

    DWORD objSize = 0, dataLen = 0;
    status = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PBYTE)&objSize, sizeof(DWORD), &dataLen, 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        CloseHandle(hFile);
        return false;
    }

    std::vector<uint8_t> hashObj(objSize);
    BCRYPT_HASH_HANDLE hHash = nullptr;
    status = BCryptCreateHash(hAlg, &hHash, hashObj.data(), (ULONG)hashObj.size(), nullptr, 0, 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        CloseHandle(hFile);
        return false;
    }

    const DWORD bufferSize = 64 * 1024;
    std::vector<uint8_t> buffer(bufferSize);
    DWORD bytesRead = 0;

    while (ReadFile(hFile, buffer.data(), bufferSize, &bytesRead, nullptr) && bytesRead > 0) {
        BCryptHashData(hHash, buffer.data(), bytesRead, 0);
    }
    CloseHandle(hFile);

    uint8_t hashBuf[32] = {};
    BCryptFinishHash(hHash, hashBuf, 32, 0);
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    static const char hexChars[] = "0123456789abcdef";
    char hexStr[65] = {};
    for (int i = 0; i < 32; ++i) {
        hexStr[i * 2 + 0] = hexChars[(hashBuf[i] >> 4) & 0x0F];
        hexStr[i * 2 + 1] = hexChars[hashBuf[i] & 0x0F];
    }
    hexStr[64] = '\0';
    outHex = hexStr;
    return true;
}

bool SecuritySystem::VerifyAuthenticodeSignature(const std::wstring& filePath, std::string* outSubject) {
    WINTRUST_FILE_INFO fileData = {};
    fileData.cbStruct = sizeof(WINTRUST_FILE_INFO);
    fileData.pcwszFilePath = filePath.c_str();
    fileData.hFile = NULL;
    fileData.pgKnownSubject = NULL;

    GUID actionGuid = WINTRUST_ACTION_GENERIC_VERIFY_V2;

    WINTRUST_DATA winTrustData = {};
    winTrustData.cbStruct = sizeof(WINTRUST_DATA);
    winTrustData.pPolicyCallbackData = NULL;
    winTrustData.pSIPClientData = NULL;
    winTrustData.dwUIChoice = WTD_UI_NONE;
    winTrustData.fdwRevocationChecks = WTD_REVOKE_NONE;
    winTrustData.dwUnionChoice = WTD_CHOICE_FILE;
    winTrustData.pFile = &fileData;
    winTrustData.dwStateAction = WTD_STATEACTION_VERIFY;
    winTrustData.hWVTStateData = NULL;
    winTrustData.pwszURLReference = NULL;
    winTrustData.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL | WTD_REVOCATION_CHECK_NONE;
    winTrustData.dwUIContext = 0;

    LONG status = WinVerifyTrust(NULL, &actionGuid, &winTrustData);
    bool isValid = (status == ERROR_SUCCESS);

    winTrustData.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(NULL, &actionGuid, &winTrustData);
    return isValid;
}

bool SecuritySystem::CheckRootDllHijacking(const std::wstring& exeDir, SecurityReport& report) {
    bool detected = false;
    std::error_code ec;
    std::filesystem::path rootPath(exeDir);

    if (!std::filesystem::exists(rootPath, ec)) return true;

    for (const auto& entry : std::filesystem::directory_iterator(rootPath, ec)) {
        if (entry.is_regular_file(ec)) {
            std::wstring ext = ToLowerW(entry.path().extension().wstring());
            if (ext == L".dll") {
                std::string filename = entry.path().filename().string();
                report.violations.push_back("Rogue Root DLL Detected (Anti-DLL Hijacking): " + filename);
                HandleViolation(SecurityViolationType::DllHijackDetected, "Rogue Root DLL Detected: " + filename);
                detected = true;
            }
        }
    }
    return !detected;
}

// Obfuscated string constants (decoded on stack via rolling MBA operations)
static const uint8_t s_encManifestPath[] = { 0x2f, 0xc6, 0x89, 0x44, 0xa, 0xd6, 0x72, 0x34, 0xbb, 0xb2, 0x4c, 0x1d, 0xd5, 0x90, 0x4c, 0xec, 0xb8, 0x6a, 0x74, 0xcc, 0x89, 0x41, 0x1f, 0xdb, 0x61, 0x38, 0xe6 };
static const uint8_t s_encHmacPrefix[]   = { 0x3b, 0xf7, 0x40, 0xb, 0xa2, 0x85, 0x55, 0x25, 0x99, 0xc7, 0xf, 0xba };

static bool ReadManifestFromPak(const std::wstring&, std::string& outContent) {
    auto data=EngineStorage::ReadContentArchive(u"security/integrity.manifest");
    outContent.assign(data.begin(),data.end());return !data.empty();
}

bool SecuritySystem::CheckDllManifest(const std::wstring& exeDir, SecurityReport& report) {
    std::string fileContent;
    if (!ReadManifestFromPak(exeDir, fileContent)) {
        report.violations.push_back("DLL Integrity verification failed: Signed integrity manifest missing or corrupted in content archives (content01.pak...content10.pak)");
        HandleViolation(SecurityViolationType::DllTamperedOrSubstituted, "Signed DLL integrity manifest missing from content archives");
        return false;
    }

    // Strip UTF-8 BOM if present
    if (fileContent.size() >= 3 && (uint8_t)fileContent[0] == 0xEF && (uint8_t)fileContent[1] == 0xBB && (uint8_t)fileContent[2] == 0xBF) {
        fileContent = fileContent.substr(3);
    }

    // 1. Verify HMAC-SHA256 digital signature header: HMAC-SHA256:<64 hex>
    std::string sigPrefix = MBAObfuscator::DeobfuscateString(s_encHmacPrefix, sizeof(s_encHmacPrefix), 0x73);
    if (fileContent.rfind(sigPrefix, 0) != 0) {
        report.violations.push_back("DLL Integrity Manifest Tampered or Unsigned: Missing HMAC-SHA256 digital signature header!");
        HandleViolation(SecurityViolationType::DllTamperedOrSubstituted, "Unsigned DLL manifest detected (Manifest Tampered)");
        return false;
    }

    size_t firstNewline = fileContent.find('\n');
    if (firstNewline == std::string::npos) {
        report.violations.push_back("Corrupted DLL Integrity Manifest (Invalid Header Format)");
        return false;
    }

    std::string expectedSig = fileContent.substr(sigPrefix.size(), firstNewline - sigPrefix.size());
    while (!expectedSig.empty() && (expectedSig.back() == '\r' || expectedSig.back() == ' ')) {
        expectedSig.pop_back();
    }

    std::string manifestBody = fileContent.substr(firstNewline + 1);

    // Dynamic stack synthesis of cryptographic key via non-linear MBA polynomials
    auto signingKey=PakCrypto::GetSigningKey();
    if(signingKey.size()!=32)return false;
    auto* manifestMasterKey=signingKey.data();

    // Calculate HMAC-SHA256 over manifest body
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    uint8_t computedHmac[32] = { 0 };
    bool hmacOk = false;

    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) >= 0) {
        if (BCryptCreateHash(hAlg, &hHash, nullptr, 0, (PUCHAR)manifestMasterKey, 32, 0) >= 0) {
            hmacOk=BCryptHashData(hHash, (PUCHAR)manifestBody.data(), (ULONG)manifestBody.size(), 0)>=0 &&
                   BCryptFinishHash(hHash, computedHmac, sizeof(computedHmac), 0)>=0;
            BCryptDestroyHash(hHash);

        }
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }
    // Clean key from stack immediately
    SecureZeroMemory(manifestMasterKey, 32);

    if (!hmacOk) {
        report.violations.push_back("Failed to initialize cryptographic HMAC provider for manifest verification");
        return false;
    }

    char computedSigHex[65] = { 0 };
    for (int i = 0; i < 32; ++i) {
        snprintf(computedSigHex + (i * 2), 3, "%02x", computedHmac[i]);
    }

    if (_stricmp(computedSigHex, expectedSig.c_str()) != 0) {
        report.violations.push_back("DLL Integrity Manifest Forgery Detected: Cryptographic HMAC-SHA256 signature mismatch! File was modified or tampered with.");
        HandleViolation(SecurityViolationType::DllTamperedOrSubstituted, "DLL integrity manifest forgery/tampering detected");
        return false;
    }

    // 2. Parse authenticated DLL paths and hashes
    std::istringstream stream(manifestBody);
    bool detected = false;
    std::string line;
    std::unordered_set<std::string> verifiedFiles;
    std::error_code ec;

    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == '#') continue;

        size_t sep = line.find(':');
        if (sep == std::string::npos) continue;

        std::string relPathStr = line.substr(0, sep);
        std::string expectedHash = line.substr(sep + 1);

        while (!expectedHash.empty() && (expectedHash.back() == '\r' || expectedHash.back() == ' '))
            expectedHash.pop_back();

        std::filesystem::path fullPath = std::filesystem::path(exeDir) / "ThirdParty" / relPathStr;
        if (!std::filesystem::exists(fullPath, ec)) {
            report.violations.push_back("Missing Critical DLL from Manifest: ThirdParty\\" + relPathStr);
            HandleViolation(SecurityViolationType::DllTamperedOrSubstituted, "Missing DLL: " + relPathStr);
            detected = true;
            continue;
        }

        std::string actualHash;
        if (!ComputeFileSha256(fullPath.wstring(), actualHash)) {
            report.violations.push_back("Failed to compute SHA-256 for ThirdParty\\" + relPathStr);
            detected = true;
            continue;
        }

        if (actualHash != expectedHash) {
            report.violations.push_back("DLL Cryptographic Hash Mismatch (Tampered/Replaced DLL): ThirdParty\\" + relPathStr);
            HandleViolation(SecurityViolationType::DllTamperedOrSubstituted, "DLL Hash Mismatch: " + relPathStr);
            detected = true;
        } else {
            std::string normRel = relPathStr;
            for (auto& c : normRel) {
                if (c == '\\') c = '/';
                c = (char)::tolower(c);
            }
            verifiedFiles.insert(normRel);
        }
    }

    // Check for extraneous / unknown DLLs placed in ThirdParty\ folder
    std::filesystem::path dllRootDir = std::filesystem::path(exeDir) / L"ThirdParty";
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dllRootDir, ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".dll") {
            auto rel = std::filesystem::relative(entry.path(), dllRootDir, ec).generic_string();
            std::string normRel = rel;
            for (auto& c : normRel) {
                if (c == '\\') c = '/';
                c = (char)::tolower(c);
            }
            if (verifiedFiles.find(normRel) == verifiedFiles.end()) {
                report.violations.push_back("Unverified/Extraneous DLL Detected in ThirdParty folder: ThirdParty\\" + rel);
                HandleViolation(SecurityViolationType::DllTamperedOrSubstituted, "Extraneous DLL: " + rel);
                detected = true;
            }
        }
    }

    return !detected;
}

bool SecuritySystem::CheckLoadedDllHooksAndPaths(SecurityReport& report) {
    bool detected = false;
    DWORD pid = GetCurrentProcessId();
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);

    if (hSnap == INVALID_HANDLE_VALUE) return true;

    MODULEENTRY32W me = {};
    me.dwSize = sizeof(me);

    WCHAR exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    PathRemoveFileSpecW(exePath);
    std::wstring exeDir = ToLowerW(exePath);
    std::wstring expectedDllPrefix = exeDir + L"\\thirdparty\\";

    static const struct SensitiveVendorDll {
        const wchar_t* name;
        bool requireSignature;
        const char* checkExport1;
        const char* checkExport2;
    } s_sensitiveDlls[] = {
        { L"steam_api64.dll", true, "SteamAPI_InitFlat", "SteamAPI_RestartAppIfNecessary" },
        { L"eossdk-win64-shipping.dll", true, "EOS_Initialize", nullptr },
        { L"dstorage.dll", true, "DStorageGetFactory", "DStorageSetConfiguration" },
        { L"dstoragecore.dll", true, nullptr, nullptr },
        { L"sl.interposer.dll", true, "slInit", "slShutdown" },
        { L"msquic.dll", true, "MsQuicOpenVersion", "MsQuicClose" },
        { L"libxess.dll", true, nullptr, nullptr },
        { L"discord_partner_sdk.dll", true, nullptr, nullptr },
        { L"PhysX_64.dll", false, nullptr, nullptr }
    };

    if (Module32FirstW(hSnap, &me)) {
        do {
            std::wstring modName = ToLowerW(me.szModule);
            std::wstring modPath = ToLowerW(me.szExePath);

            for (const auto& sensitive : s_sensitiveDlls) {
                if (modName == ToLowerW(sensitive.name)) {
                    char modNameUtf8[MAX_PATH] = {};
                    WideCharToMultiByte(CP_UTF8, 0, me.szModule, -1, modNameUtf8, sizeof(modNameUtf8), nullptr, nullptr);

                    // 1. Path check: Must be inside <exeDir>\ThirdParty directory
                    if (modPath.find(expectedDllPrefix) == std::wstring::npos) {
                        char pathBuf[MAX_PATH] = {};
                        WideCharToMultiByte(CP_UTF8, 0, me.szExePath, -1, pathBuf, sizeof(pathBuf), nullptr, nullptr);
                        report.violations.push_back("Sensitive Module Loaded from Unauthorized Path: " + std::string(pathBuf));
                        HandleViolation(SecurityViolationType::DllHijackDetected, "Unauthorized Module Path: " + std::string(pathBuf));
                        detected = true;
                    }

                    // 2. Digital Signature check on disk
                    if (sensitive.requireSignature) {
                        if (!VerifyAuthenticodeSignature(me.szExePath, nullptr)) {
                            report.violations.push_back(std::string("Digital Signature Invalid / Replaced (Cracked/Emulator DLL): ") + modNameUtf8);
                            HandleViolation(SecurityViolationType::DllSignatureInvalid, "Invalid Signature: " + std::string(modNameUtf8));
                            detected = true;
                        }
                    }

                    // 3. Export Hook Check in Memory
                    HMODULE hMod = me.hModule;
                    uintptr_t modStart = (uintptr_t)me.modBaseAddr;
                    uintptr_t modEnd = modStart + me.modBaseSize;
                    const char* exportsToCheck[] = { sensitive.checkExport1, sensitive.checkExport2 };
                    for (const char* expName : exportsToCheck) {
                        if (!expName) continue;
                        FARPROC pFunc = GetProcAddress(hMod, expName);
                        if (pFunc) {
                            const uint8_t* pBytes = (const uint8_t*)pFunc;
                            if (pBytes[0] == 0xE9) {
                                int32_t rel = *(const int32_t*)(pBytes + 1);
                                uintptr_t dest = (uintptr_t)pBytes + 5 + rel;
                                if (dest >= modStart && dest < modEnd) {
                                    // Legitimate internal compiler/linker export thunk inside the DLL itself (e.g. libxess.dll)
                                    continue;
                                }
                                report.violations.push_back(std::string("Inline JMP Hook Detected on ") + expName + " in " + modNameUtf8);
                                HandleViolation(SecurityViolationType::ApiHookDetected, "Inline Hook on " + std::string(expName));
                                detected = true;
                            } else if (pBytes[0] == 0xFF && pBytes[1] == 0x25) {
                                int32_t rel = *(const int32_t*)(pBytes + 2);
                                const uintptr_t* pTargetPtr = (const uintptr_t*)((uintptr_t)pBytes + 6 + rel);
                                if ((uintptr_t)pTargetPtr >= modStart && (uintptr_t)pTargetPtr + sizeof(uintptr_t) <= modEnd) {
                                    uintptr_t dest = *pTargetPtr;
                                    if (dest >= modStart && dest < modEnd) {
                                        // Legitimate internal thunk redirect inside the DLL itself
                                        continue;
                                    }
                                }
                                report.violations.push_back(std::string("Indirect JMP Hook Detected on ") + expName + " in " + modNameUtf8);
                                HandleViolation(SecurityViolationType::ApiHookDetected, "Indirect Hook on " + std::string(expName));
                                detected = true;
                            } else if (pBytes[0] == 0xCC) {
                                report.violations.push_back(std::string("Software Breakpoint (0xCC) Detected on ") + expName + " in " + modNameUtf8);
                                HandleViolation(SecurityViolationType::ApiHookDetected, "Breakpoint on " + std::string(expName));
                                detected = true;
                            }
                        }
                    }
                    break;
                }
            }
        } while (Module32NextW(hSnap, &me));
    }

    CloseHandle(hSnap);
    return !detected;
}

bool SecuritySystem::CheckDllIntegrity(SecurityReport& report) {
    WCHAR exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    PathRemoveFileSpecW(exePath);
    std::wstring exeDir(exePath);

    bool ok1 = CheckRootDllHijacking(exeDir, report);
    bool ok2 = CheckDllManifest(exeDir, report);
    bool ok3 = CheckLoadedDllHooksAndPaths(report);

    return ok1 && ok2 && ok3;
}

bool SecuritySystem::CheckContinuousMemoryIntegrity(SecurityReport& report) {
    if (!m_baselineInitialized || m_codeSectionBase == 0 || m_codeBlockCrcs.empty()) {
        InitializeTextSectionBaseline();
        if (!m_baselineInitialized) {
            report.violations.push_back("Executable code integrity baseline unavailable");
            return false;
        }
    }

    bool detected = false;
    for (const auto& block : m_codeBlockCrcs) {
        LPCVOID pAddress = (LPCVOID)(m_codeSectionBase + block.offset);

        // 1. Check memory page protection
        MEMORY_BASIC_INFORMATION mbi = {};
        if (!VirtualQuery(pAddress, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT ||
            (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0 ||
            reinterpret_cast<uintptr_t>(pAddress) - reinterpret_cast<uintptr_t>(mbi.BaseAddress) + block.size > mbi.RegionSize) {
            report.violations.push_back("Executable code page is unreadable or decommitted");
            return false;
        }
        {
            if ((mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY)) != 0) {
                report.violations.push_back("Executable memory page protection modified to writable (PAGE_EXECUTE_READWRITE patch detected)");
                detected = true;
                break;
            }
        }

        // 2. MBA-CRC32 integrity comparison
        uint32_t currentCrc = MBAObfuscator::CRC32((const uint8_t*)pAddress, block.size);
        if (!MBAObfuscator::Equal32(currentCrc, block.expectedCrc)) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Code section memory tampering detected at offset +0x%zX via MBA-CRC32 verification", block.offset);
            report.violations.push_back(buf);
            detected = true;
            break;
        }
    }

    return !detected;
}

bool SecuritySystem::CheckHypervisorAndTiming(SecurityReport& report) {
    bool detected = false;

    // 1. Timing Anomaly Check via RDTSC
    uint64_t t1 = __rdtsc();
    volatile int dummy = 0;
    for (int i = 0; i < 200; ++i) {
        dummy += i;
    }
    uint64_t t2 = __rdtsc();
    if (t2 > t1 && (t2 - t1) > 20000000ULL) {
        report.violations.push_back("Timing anomaly detected: excessive execution latency indicates single-stepping debugger");
        detected = true;
    }

    // 2. CPUID Hypervisor & Sandbox Verification
    int cpuInfo[4] = { 0 };
    __cpuid(cpuInfo, 1);
    bool hypervisorPresent = (cpuInfo[2] & (1 << 31)) != 0;
    if (hypervisorPresent) {
        __cpuid(cpuInfo, 0x40000000);
        char vendor[13] = { 0 };
        memcpy(vendor, &cpuInfo[1], 4);
        memcpy(vendor + 4, &cpuInfo[2], 4);
        memcpy(vendor + 8, &cpuInfo[3], 4);

        std::string v = vendor;
        // Check for specific suspicious analysis environments
        if (v == "TCGTCGTCGTCG") {
            report.violations.push_back("Emulation/Sandbox detected: QEMU TCG environment is active");
            detected = true;
        }
    }

    return !detected;
}

bool SecuritySystem::CheckHardwareLicense(SecurityReport& report) {
    LicenseStatus status = LicenseSystem::Get().ValidateLicense();
    if (status != LicenseStatus::Valid) {
        const auto& info = LicenseSystem::Get().GetLicenseInfo();
        report.violations.push_back("Hardware License Failure: " + info.statusMessage);
        return false;
    }
    return true;
}

void SecuritySystem::HandleViolation(SecurityViolationType type, const std::string& description) {
    AppendSecurityLog("[Security Violation] " + description);
#ifndef ENGINE_RELEASE
    std::ofstream logFile(EngineDiagnostics::Directory() / "log.txt", std::ios::app);
    logFile << "[Security Violation] " << description << std::endl;
#endif

    std::function<void(SecurityViolationType, const std::string&)> callback;
    {
        std::lock_guard<std::mutex> lock(m_reportMutex);
        callback = m_violationCallback;
    }
    if (callback) callback(type, description);

#ifdef ENGINE_RELEASE
    // Silent termination in release on active tampering without deadlock
    if (!m_collectingBootReport.load()) ExitProcess(0xC0000005);
#endif
}

void SecuritySystem::StartWatchdog() {
    if (m_watchdogRunning.load()) return;

    m_watchdogRunning.store(true);
    m_watchdogThread = std::thread(&SecuritySystem::WatchdogWorker, this);

    // Hide watchdog thread from debugger attachment
    HideThreadFromDebugger(m_watchdogThread.native_handle());
    // Also hide main thread
    HideThreadFromDebugger(GetCurrentThread());
}

void SecuritySystem::Shutdown() {
    if (m_watchdogRunning.load()) {
        m_watchdogRunning.store(false);
        if (m_watchdogThread.joinable()) {
            m_watchdogThread.join();
        }
    }
}

void SecuritySystem::WatchdogWorker() {
    HideThreadFromDebugger(GetCurrentThread());

    while (m_watchdogRunning.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5000));
        if (!m_watchdogRunning.load()) break;

        SecurityReport report;
        if (!PerformIntegrityCheck(report)) {
            {
                std::lock_guard<std::mutex> lock(m_reportMutex);
                m_lastReport = report;
            }
            std::string summary = "Security violation detected during runtime:\n";
            for (const auto& v : report.violations) {
                summary += " - " + v + "\n";
            }
            HandleViolation(SecurityViolationType::CodeIntegrityViolation, summary);
            break;
        }
    }
}

} // namespace EngineSecurity

#else

namespace EngineSecurity {
bool SecuritySystem::CollectStartupReport(SecurityReport& outReport) { return PerformIntegrityCheck(outReport); }

SecuritySystem::SecuritySystem() = default;
SecuritySystem::~SecuritySystem() = default;

SecuritySystem& SecuritySystem::Get() {
    static SecuritySystem s_instance;
    return s_instance;
}

bool SecuritySystem::InitializeSecurityBoot() {
    return true;
}

bool SecuritySystem::PerformNetworkIntegrityCheck(uint32_t& flags, uint64_t& codeHash) {
    flags = 0x80000000u;
    codeHash = 0;
    return false;
}

bool SecuritySystem::PerformIntegrityCheck(SecurityReport& outReport) {
    outReport.passed = true;
    outReport.violations.clear();
    outReport.summary = "macOS security checks passed.";
    return true;
}

void SecuritySystem::StartWatchdog() {
    m_watchdogRunning = true;
}

void SecuritySystem::Shutdown() {
    m_watchdogRunning = false;
}

bool SecuritySystem::CheckDebuggers(SecurityReport& report) { return true; }
bool SecuritySystem::CheckHardwareBreakpoints(SecurityReport& report) { return true; }
bool SecuritySystem::CheckRunningProcesses(SecurityReport& report) { return true; }
bool SecuritySystem::CheckSystemServicesAndDrivers(SecurityReport& report) { return true; }
bool SecuritySystem::CheckLoadedModules(SecurityReport& report) { return true; }
bool SecuritySystem::CheckApiHooks(SecurityReport& report) { return true; }
bool SecuritySystem::CheckCodeIntegrity(SecurityReport& report) { return true; }
bool SecuritySystem::CheckContinuousMemoryIntegrity(SecurityReport& report) { return true; }
bool SecuritySystem::CheckHypervisorAndTiming(SecurityReport& report) { return true; }
bool SecuritySystem::CheckHardwareLicense(SecurityReport& report) { return true; }
bool SecuritySystem::CheckSteamLicenseIntegrity(SecurityReport& report) { return true; }
bool SecuritySystem::CheckDllIntegrity(SecurityReport& report) { return true; }
void SecuritySystem::InitializeTextSectionBaseline() {}
void SecuritySystem::HideThreadFromDebugger(HANDLE) {}
void SecuritySystem::HandleViolation(SecurityViolationType, const std::string&) {}
void SecuritySystem::WatchdogWorker() {}

} // namespace EngineSecurity

#endif
