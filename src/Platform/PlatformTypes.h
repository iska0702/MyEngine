#pragma once

#include <cstdint>
#include <string>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    using PlatformWindowHandle = HWND;
#else
    // On macOS / POSIX / PS4, provide compatibility types
    #include <unistd.h>
    #include <cstdlib>
    #include <time.h>
    #include <string.h>
    using PlatformWindowHandle = void*;
    typedef void* HWND;
    typedef void* HANDLE;
    typedef void* HINSTANCE;
    typedef intptr_t INT_PTR;
    struct ID3D12Resource {};
    struct ID3D12Device {};
    struct ID3D12GraphicsCommandList {};
    #ifndef __int8_t_defined
    #define __int8_t_defined
    typedef int8_t   __int8_t;
    typedef int16_t  __int16_t;
    typedef int32_t  __int32_t;
    typedef int64_t  __int64_t;
    typedef uint8_t  __uint8_t;
    typedef uint16_t __uint16_t;
    typedef uint32_t __uint32_t;
    typedef uint64_t __uint64_t;
    #endif
    typedef unsigned int UINT;
    typedef int INT;
    typedef uint8_t UINT8;
    typedef uint8_t BYTE;
    typedef uint16_t WORD;
    typedef uint16_t USHORT;
    typedef uint32_t DWORD;
    typedef uint32_t UINT32;
    typedef uint64_t UINT64;
    #ifndef INVALID_HANDLE_VALUE
    #define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
    #endif
    struct IMMDeviceEnumerator;
    struct IMMDevice;
    struct IAudioClient;
    struct IAudioRenderClient;
    struct WAVEFORMATEXTENSIBLE {};

    #ifndef strncpy_s
    #define strncpy_s(dest, src, count) do { strncpy(dest, src, (count) - 1); (dest)[(count) - 1] = '\0'; } while(0)
    #endif

    #ifndef strcpy_s
    #define strcpy_s(dest, src) strcpy(dest, src)
    #endif

    #ifndef MAX_PATH
    #define MAX_PATH 260
    #endif

    #ifndef sprintf_s
    #define sprintf_s(buf, ...) snprintf(buf, sizeof(buf), __VA_ARGS__)
    #endif

    #ifndef _popen
    #define _popen popen
    #endif

    #ifndef _pclose
    #define _pclose pclose
    #endif

    inline uint32_t GetModuleFileNameA(void*, char* buffer, uint32_t size) {
        if (buffer && size > 0) buffer[0] = '\0';
        return 0;
    }

    struct OPENFILENAMEA {
        uint32_t lStructSize;
        void* hwndOwner;
        void* hInstance;
        const char* lpstrFilter;
        char* lpstrCustomFilter;
        uint32_t nMaxCustFilter;
        uint32_t nFilterIndex;
        char* lpstrFile;
        uint32_t nMaxFile;
        char* lpstrFileTitle;
        uint32_t nMaxFileTitle;
        const char* lpstrInitialDir;
        const char* lpstrTitle;
        uint32_t Flags;
        uint16_t nFileOffset;
        uint16_t nFileExtension;
        const char* lpstrDefExt;
        intptr_t lCustData;
        void* lpfnHook;
        const char* lpTemplateName;
    };

    #ifndef OFN_FILEMUSTEXIST
    #define OFN_FILEMUSTEXIST 0x00001000
    #define OFN_PATHMUSTEXIST 0x00000800
    #define OFN_NOCHANGEDIR   0x00000008
    #endif

    inline bool GetOpenFileNameA(OPENFILENAMEA*) { return false; }
    inline bool GetSaveFileNameA(OPENFILENAMEA*) { return false; }

    #ifndef SW_SHOWNORMAL
    #define SW_SHOWNORMAL 1
    #endif

    inline void* ShellExecuteA(void*, const char*, const char* file, const char*, const char*, int) {
        if (file) {
            std::string cmd = std::string("open \"") + file + "\"";
            (void)system(cmd.c_str());
        }
        return (void*)(intptr_t)42;
    }

    #ifndef VK_OEM_3
    #define VK_OEM_3 0xC0
    #endif

    inline int SetEnvironmentVariableA(const char* name, const char* value) {
#if defined(__APPLE__) || (defined(__linux__) && !defined(__PS4__) && !defined(__ORBIS__))
        return setenv(name, value, 1);
#else
        (void)name; (void)value;
        return 0;
#endif
    }

    inline void Sleep(unsigned long ms) {
        usleep(ms * 1000);
    }
    #if !defined(__OBJC__)
    typedef int32_t BOOL;
    #endif
    typedef long HRESULT;
    #ifndef TRUE
    #define TRUE 1
    #endif
    #ifndef FALSE
    #define FALSE 0
    #endif
    #ifndef S_OK
    #define S_OK ((HRESULT)0L)
    #endif
    #ifndef S_FALSE
    #define S_FALSE ((HRESULT)1L)
    #endif
    #ifndef FAILED
    #define FAILED(hr) (((HRESULT)(hr)) < 0)
    #endif
    #ifndef SUCCEEDED
    #define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
    #endif
#if defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
    extern "C" int32_t sceKernelDebugOutText(int32_t dbg_channel, const char* text, ...);
    inline void OutputDebugStringA(const char* str) {
        if (str) sceKernelDebugOutText(0, str);
    }
#else
    inline void OutputDebugStringA(const char*) {}
#endif

    #ifndef VK_LBUTTON
    #define VK_LBUTTON 0x01
    #define VK_RBUTTON 0x02
    #define VK_CANCEL 0x03
    #define VK_MBUTTON 0x04
    #define VK_BACK 0x08
    #define VK_TAB 0x09
    #define VK_CLEAR 0x0C
    #define VK_RETURN 0x0D
    #define VK_SHIFT 0x10
    #define VK_CONTROL 0x11
    #define VK_MENU 0x12
    #define VK_PAUSE 0x13
    #define VK_CAPITAL 0x14
    #define VK_ESCAPE 0x1B
    #define VK_SPACE 0x20
    #define VK_PRIOR 0x21
    #define VK_NEXT 0x22
    #define VK_END 0x23
    #define VK_HOME 0x24
    #define VK_LEFT 0x25
    #define VK_UP 0x26
    #define VK_RIGHT 0x27
    #define VK_DOWN 0x28
    #define VK_F1 0x70
    #define VK_F2 0x71
    #define VK_F3 0x72
    #define VK_F4 0x73
    #define VK_F5 0x74
    #define VK_F6 0x75
    #define VK_F7 0x76
    #define VK_F8 0x77
    #define VK_F9 0x78
    #define VK_F10 0x79
    #define VK_F11 0x7A
    #define VK_F12 0x7B
    #endif

    #define MB_OK 0x00000000L
    #define MB_OKCANCEL 0x00000001L
    #define MB_ICONERROR 0x00000010L
    #define MB_ICONWARNING 0x00000030L
    #define MB_ICONINFORMATION 0x00000040L
    inline int MessageBoxA(HWND, const char*, const char*, unsigned int) { return 1; }

    inline short GetAsyncKeyState(int) { return 0; }
    inline short GetKeyState(int) { return 0; }
#endif

namespace EnginePlatform {

enum class PlatformType {
    Windows,
    macOS,
    Linux,
    PS4
};

inline constexpr PlatformType CurrentPlatform =
#if defined(_WIN32)
    PlatformType::Windows;
#elif defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
    PlatformType::PS4;
#elif defined(__APPLE__)
    PlatformType::macOS;
#else
    PlatformType::Linux;
#endif

} // namespace EnginePlatform
