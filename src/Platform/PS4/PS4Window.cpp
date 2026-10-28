#include "PS4Window.h"
#include "Input/DualSense.h"
#include <iostream>
#include <cstring>
#include <algorithm>

#if defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
#include <orbis/libkernel.h>
#include <orbis/VideoOut.h>
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include <orbis/Sysmodule.h>

extern "C" {
    // Sony Orbis OS / shadPS4 Process Parameters
    struct OrbisProcParam {
        uint64_t size;
        uint32_t magic;
        uint32_t entry_count;
        uint64_t sdk_version;
        char* process_name;
        char* main_thread_name;
        uint32_t* main_thread_prio;
        uint32_t* main_thread_stack_size;
        void* libc_param;
        void* mem_param;
        void* fs_param;
        uint32_t* process_preload_enable;
        uint64_t unknown1;
    };

    alignas(16) __attribute__((used, section(".data.sce_process_param")))
    const OrbisProcParam _sceProcessParam = {
        sizeof(OrbisProcParam),
        0x13C13F4B, // Magic: 'ORBI'
        1,          // entry_count
        0x04508101, // sdk_version (Firmware 4.50+)
        nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, 0
    };
}
#endif

namespace EnginePlatform {

PS4Window::PS4Window(int width, int height, const std::wstring& title)
    : m_width(width > 0 ? width : 1920)
    , m_height(height > 0 ? height : 1080)
    , m_depth(4)
    , m_title(title)
    , m_frameBufferSize(static_cast<size_t>(m_width) * m_height * m_depth)
{
}

PS4Window::~PS4Window() {
    DeallocateDisplayBuffers();

#if defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
    m_padHandle = -1;

    if (m_videoHandle >= 0) {
        sceVideoOutClose(m_videoHandle);
        m_videoHandle = -1;
    }
#endif
}

bool PS4Window::Initialize() {
#if defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
    // 1. Open PS4 VideoOut handle
    m_videoHandle = sceVideoOutOpen(ORBIS_VIDEO_USER_MAIN, ORBIS_VIDEO_OUT_BUS_MAIN, 0, 0);
    if (m_videoHandle < 0) {
        std::cerr << "[PS4Window] sceVideoOutOpen failed: " << m_videoHandle << std::endl;
        return false;
    }

    // 2. Initialize VSync flip event queue
    if (!InitFlipQueue()) {
        std::cerr << "[PS4Window] InitFlipQueue failed" << std::endl;
        return false;
    }

    // 3. Allocate display framebuffers in Garlic Direct Memory
    if (!AllocateDisplayBuffers()) {
        std::cerr << "[PS4Window] AllocateDisplayBuffers failed" << std::endl;
        return false;
    }

    // 4. Set flip rate (0 = vsync every frame, 60 FPS)
    sceVideoOutSetFlipRate(m_videoHandle, 0);

    // 5. Connect to shared controller handle
    m_padHandle = EngineInput::GetPS4PadHandle();
#else
    // Fallback simulation for non-PS4 hosts
    m_directMemSize = m_frameBufferSize * m_bufferCount;
    m_directMemMapped = malloc(m_directMemSize);
    if (m_directMemMapped) {
        memset(m_directMemMapped, 0, m_directMemSize);
        uint8_t* ptr = static_cast<uint8_t*>(m_directMemMapped);
        for (int i = 0; i < m_bufferCount; ++i) {
            m_frameBuffers[i] = reinterpret_cast<uint32_t*>(ptr + i * m_frameBufferSize);
        }
    }
#endif

    m_isOpen = true;
    std::cout << "[PS4Window] Initialized successfully (" << m_width << "x" << m_height << " 60Hz)" << std::endl;
    return true;
}

void PS4Window::Show() {
    m_isOpen = true;
}

PlatformWindowHandle PS4Window::GetNativeHandle() const {
    return const_cast<void*>(reinterpret_cast<const void*>(this));
}

void PS4Window::SetDisplayMode(int width, int height, const std::string&) {
    if (width > 0 && height > 0) {
        m_width = width;
        m_height = height;
    }
}

bool PS4Window::InitFlipQueue() {
#if defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
    int rc = sceKernelCreateEqueue(&m_flipQueue, "MyEngine PS4 FlipQueue");
    if (rc < 0) return false;

    rc = sceVideoOutAddFlipEvent(m_flipQueue, m_videoHandle, nullptr);
    if (rc < 0) return false;
#endif
    return true;
}

bool PS4Window::AllocateDisplayBuffers() {
#if defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
    const int alignment = 0x200000; // 2MB page alignment
    size_t totalNeeded = m_frameBufferSize * m_bufferCount;
    m_directMemSize = (totalNeeded + alignment - 1) / alignment * alignment;

    // Allocate direct physical memory (Garlic memory pool = 3)
    int rc = sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(), m_directMemSize, alignment, 3, &m_directMemOffset);
    if (rc < 0) {
        std::cerr << "[PS4Window] sceKernelAllocateDirectMemory failed: " << rc << std::endl;
        return false;
    }

    // Map direct memory into virtual address space (0x33 = Read/Write)
    rc = sceKernelMapDirectMemory(&m_directMemMapped, m_directMemSize, 0x33, 0, m_directMemOffset, alignment);
    if (rc < 0) {
        std::cerr << "[PS4Window] sceKernelMapDirectMemory failed: " << rc << std::endl;
        sceKernelReleaseDirectMemory(m_directMemOffset, m_directMemSize);
        m_directMemOffset = 0;
        m_directMemSize = 0;
        return false;
    }

    // Assign buffer pointers
    uint8_t* base = static_cast<uint8_t*>(m_directMemMapped);
    for (int i = 0; i < m_bufferCount; ++i) {
        m_frameBuffers[i] = reinterpret_cast<uint32_t*>(base + (i * m_frameBufferSize));
        memset(m_frameBuffers[i], 0, m_frameBufferSize);
    }

    // Configure video buffer attribute (32-bit ARGB/RGBA format, LINEAR tiling mode = 1)
    sceVideoOutSetBufferAttribute(&m_bufferAttr, 0x80000000, 1, 0, m_width, m_height, m_width);

    // Register display buffers with VideoOut
    rc = sceVideoOutRegisterBuffers(m_videoHandle, 0, reinterpret_cast<void**>(m_frameBuffers), m_bufferCount, &m_bufferAttr);
    if (rc != 0) {
        std::cerr << "[PS4Window] sceVideoOutRegisterBuffers failed: " << rc << std::endl;
        return false;
    }
#endif
    return true;
}

void PS4Window::DeallocateDisplayBuffers() {
#if defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
    if (m_videoHandle >= 0) {
        sceVideoOutUnregisterBuffers(m_videoHandle, 0);
    }
    if (m_flipQueue > 0) {
        sceKernelDeleteEqueue(m_flipQueue);
        m_flipQueue = 0;
    }
    if (m_directMemMapped && m_directMemSize > 0) {
        sceKernelReleaseDirectMemory(m_directMemOffset, m_directMemSize);
        m_directMemMapped = nullptr;
        m_directMemOffset = 0;
        m_directMemSize = 0;
    }
#else
    if (m_directMemMapped) {
        free(m_directMemMapped);
        m_directMemMapped = nullptr;
    }
#endif
}

void PS4Window::SubmitFlip() {
#if defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
    if (m_videoHandle >= 0) {
        int rc = sceVideoOutSubmitFlip(m_videoHandle, m_activeBufferIdx, ORBIS_VIDEO_OUT_FLIP_VSYNC, m_frameId);
        if (rc < 0) {
            // Flip queue full/busy: wait for previous flip to complete and retry
            for (int retry = 0; retry < 60 && rc < 0; ++retry) {
                WaitFlip();
                sceKernelUsleep(500); // 0.5ms
                rc = sceVideoOutSubmitFlip(m_videoHandle, m_activeBufferIdx, ORBIS_VIDEO_OUT_FLIP_VSYNC, m_frameId);
            }
        }
    }
#endif
}

void PS4Window::WaitFlip() {
#if defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
    if (m_videoHandle < 0) return;

    OrbisVideoOutFlipStatus flipStatus = {};
    if (sceVideoOutGetFlipStatus(m_videoHandle, &flipStatus) == 0 && flipStatus.flipArg >= m_frameId) {
        return;
    }

    if (m_flipQueue > 0) {
        OrbisKernelEvent evt = {};
        int count = 0;
        OrbisKernelUseconds timeout = 16000; // 16ms
        int rc = sceKernelWaitEqueue(m_flipQueue, &evt, 1, &count, &timeout);
        if (rc == 0 && count > 0) {
            return;
        }
    }

    // Fallback polling on flipStatus (ensures 60Hz pacing in emulators and hardware)
    for (int i = 0; i < 33; ++i) {
        if (sceVideoOutGetFlipStatus(m_videoHandle, &flipStatus) == 0 && flipStatus.flipArg >= m_frameId) {
            return;
        }
        sceKernelUsleep(500); // 0.5ms poll
    }
#endif
}

void PS4Window::SwapBuffers() {
    m_frameId++;
    m_activeBufferIdx = (m_activeBufferIdx + 1) % m_bufferCount;
}

uint32_t* PS4Window::GetCurrentFrameBuffer() const {
    return m_frameBuffers[m_activeBufferIdx];
}

bool PS4Window::IsKeyDown(int key) const {
    if (key >= 0 && key < 256) return m_keys[key];
    return false;
}

void PS4Window::GetMouseDelta(int& dx, int& dy) {
    dx = m_mouseDeltaX;
    dy = m_mouseDeltaY;
}

void PS4Window::ResetMouseDelta() {
    m_mouseDeltaX = 0;
    m_mouseDeltaY = 0;
}

bool PS4Window::ProcessEvents() {
    if (!m_isOpen) return false;

#if defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
    if (m_padHandle < 0) {
        m_padHandle = EngineInput::GetPS4PadHandle();
    }

    if (m_padHandle >= 0) {
        int rc = scePadReadState(m_padHandle, &m_padData);
        if (rc == 0) {
            // Map DPad to Arrow Keys
            m_keys[0x26] = (m_padData.buttons & ORBIS_PAD_BUTTON_UP) != 0;    // VK_UP
            m_keys[0x28] = (m_padData.buttons & ORBIS_PAD_BUTTON_DOWN) != 0;  // VK_DOWN
            m_keys[0x25] = (m_padData.buttons & ORBIS_PAD_BUTTON_LEFT) != 0;  // VK_LEFT
            m_keys[0x27] = (m_padData.buttons & ORBIS_PAD_BUTTON_RIGHT) != 0; // VK_RIGHT

            // Map Cross -> Space (Jump), Square -> Reload/Interact (E), Circle -> Crouch (C)
            m_keys[0x20] = (m_padData.buttons & ORBIS_PAD_BUTTON_CROSS) != 0; // VK_SPACE
            m_keys['E']  = (m_padData.buttons & ORBIS_PAD_BUTTON_SQUARE) != 0;
            m_keys['C']  = (m_padData.buttons & ORBIS_PAD_BUTTON_CIRCLE) != 0;

            // Options, Touchpad, or L3+R3 combo all map to VK_TAB to toggle ImGui Editor
            bool imguiCombo = (m_padData.buttons & (ORBIS_PAD_BUTTON_OPTIONS | ORBIS_PAD_BUTTON_TOUCH_PAD)) != 0 ||
                              ((m_padData.buttons & ORBIS_PAD_BUTTON_L3) && (m_padData.buttons & ORBIS_PAD_BUTTON_R3));
            m_keys[0x09] = imguiCombo; // VK_TAB

            const int deadzone = 20;
            int lx = static_cast<int>(m_padData.leftStick.x) - 128;
            int ly = static_cast<int>(m_padData.leftStick.y) - 128;
            int rx = static_cast<int>(m_padData.rightStick.x) - 128;
            int ry = static_cast<int>(m_padData.rightStick.y) - 128;

            if (m_cursorEnabled) {
                // In Editor / Cursor mode: stick moves virtual cursor
                float moveX = 0.0f;
                float moveY = 0.0f;
                if (std::abs(rx) > deadzone) {
                    moveX = rx / 128.0f;
                } else if (std::abs(lx) > deadzone) {
                    moveX = lx / 128.0f;
                }
                if (std::abs(ry) > deadzone) {
                    moveY = ry / 128.0f;
                } else if (std::abs(ly) > deadzone) {
                    moveY = ly / 128.0f;
                }

                m_virtualCursorX += moveX * 16.0f;
                m_virtualCursorY += moveY * 16.0f;
                m_virtualCursorX = std::clamp(m_virtualCursorX, 0.0f, static_cast<float>(m_width - 1));
                m_virtualCursorY = std::clamp(m_virtualCursorY, 0.0f, static_cast<float>(m_height - 1));

                // Cross (X) and R2 click virtual mouse on ImGui
                m_keys[0x01] = (m_padData.buttons & (ORBIS_PAD_BUTTON_CROSS | ORBIS_PAD_BUTTON_R2)) != 0; // VK_LBUTTON

                // Circle (O) closes editor
                if (m_padData.buttons & ORBIS_PAD_BUTTON_CIRCLE) {
                    m_cursorEnabled = false;
                }

                // Also allow WASD in editor freecam
                m_keys['W'] = (ly < -deadzone);
                m_keys['S'] = (ly > deadzone);
                m_keys['A'] = (lx < -deadzone);
                m_keys['D'] = (lx > deadzone);
                m_mouseDeltaX = 0;
                m_mouseDeltaY = 0;
            } else {
                // Left Analog Stick -> WASD movement
                m_keys['W'] = (ly < -deadzone);
                m_keys['S'] = (ly > deadzone);
                m_keys['A'] = (lx < -deadzone);
                m_keys['D'] = (lx > deadzone);
                m_keys[0x10] = (m_padData.buttons & ORBIS_PAD_BUTTON_L3) != 0; // VK_SHIFT (Sprint)

                // Right Analog Stick -> camera look fallback (if not consumed by DualSense subsystem)
                auto& ds = EngineInput::DualSenseController::Get();
                if ((!ds.IsConnected() || (std::abs(ds.GetRightStickX()) < 0.001f && std::abs(ds.GetRightStickY()) < 0.001f)) &&
                    (std::abs(rx) > deadzone || std::abs(ry) > deadzone)) {
                    m_mouseDeltaX = rx;
                    m_mouseDeltaY = ry;
                } else {
                    m_mouseDeltaX = 0;
                    m_mouseDeltaY = 0;
                }
            }
        }
    }
#endif

    return m_isOpen;
}

} // namespace EnginePlatform
