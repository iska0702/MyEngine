#pragma once

#include "Platform/PlatformTypes.h"
#include <string>
#include <vector>
#include <cstdint>

#if defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
#include <orbis/libkernel.h>
#include <orbis/VideoOut.h>
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include <orbis/Sysmodule.h>
#else
// Fallback definitions when compiled outside PS4 target
typedef int32_t OrbisKernelEqueue;
typedef struct OrbisVideoOutBufferAttribute {} OrbisVideoOutBufferAttribute;
typedef struct OrbisPadData {
    uint32_t buttons;
    struct { uint8_t x, y; } leftStick;
    struct { uint8_t x, y; } rightStick;
    struct { uint8_t l2, r2; } analogButtons;
} OrbisPadData;
#endif

namespace EnginePlatform {

class PS4Window {
public:
    PS4Window(int width, int height, const std::wstring& title);
    ~PS4Window();

    bool Initialize();
    void Show();
    bool IsOpen() const { return m_isOpen; }
    void Close() { m_isOpen = false; }

    PlatformWindowHandle GetNativeHandle() const;

    int GetClientWidth() const { return m_width; }
    int GetClientHeight() const { return m_height; }

    bool IsFullscreen() const { return true; }
    void ToggleFullscreen() {}
    void SetDisplayMode(int width, int height, const std::string& mode);

    bool IsCursorEnabled() const { return m_cursorEnabled; }
    void SetCursorEnabled(bool enabled) { m_cursorEnabled = enabled; }

    float GetVirtualCursorX() const { return m_virtualCursorX; }
    float GetVirtualCursorY() const { return m_virtualCursorY; }
    void SetVirtualCursor(float x, float y) { m_virtualCursorX = x; m_virtualCursorY = y; }

    bool IsKeyDown(int key) const;
    void GetMouseDelta(int& dx, int& dy);
    void ResetMouseDelta();

    // Process platform events once per frame
    bool ProcessEvents();

    // Swapchain & Display management
    void SubmitFlip();
    void WaitFlip();
    void SwapBuffers();

    uint32_t* GetCurrentFrameBuffer() const;
    int GetActiveBufferIndex() const { return m_activeBufferIdx; }
    int GetBufferCount() const { return m_bufferCount; }
    int GetVideoHandle() const { return m_videoHandle; }

private:
    bool AllocateDisplayBuffers();
    void DeallocateDisplayBuffers();
    bool InitFlipQueue();

    int m_width = 1920;
    int m_height = 1080;
    int m_depth = 4; // 32 bpp (RGBA8888)
    std::wstring m_title;

    bool m_isOpen = false;
    bool m_cursorEnabled = false;
    float m_virtualCursorX = 960.0f;
    float m_virtualCursorY = 540.0f;

    // PS4 VideoOut state
    int m_videoHandle = -1;
    OrbisKernelEqueue m_flipQueue = 0;
    OrbisVideoOutBufferAttribute m_bufferAttr = {};

    static constexpr int MAX_BUFFERS = 3;
    int m_bufferCount = 3;
    int m_activeBufferIdx = 0;
    uint32_t* m_frameBuffers[MAX_BUFFERS] = {};
    size_t m_frameBufferSize = 0;

    // Direct memory tracking
    off_t m_directMemOffset = 0;
    size_t m_directMemSize = 0;
    void* m_directMemMapped = nullptr;

    int64_t m_frameId = 1;

    // PS4 Controller state for window interaction
    int m_padHandle = -1;
    int m_userId = -1;
    OrbisPadData m_padData = {};
    bool m_keys[256] = {};
    int m_mouseDeltaX = 0;
    int m_mouseDeltaY = 0;
};

} // namespace EnginePlatform
