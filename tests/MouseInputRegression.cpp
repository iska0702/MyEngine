#include "Input/InputMapping.h"
#include "Input/DualSense.h"
#include "Input/DualShock.h"
#include "Core/Window.h"
#include <iostream>
#include <stdexcept>

// A consumable platform source, without a desktop window or HID devices.
static int pendingX = 0, pendingY = 0, reads = 0;
Window::Window(int width, int height, const std::wstring& title)
    : m_width(width), m_height(height), m_title(title) {}
Window::~Window() = default;
void Window::GetMouseDelta(int& x, int& y) {
    ++reads; x = pendingX; y = pendingY; pendingX = pendingY = 0;
}
bool Window::IsKeyDown(int) const { return false; }
namespace EngineInput {
bool DualSenseController::IsButtonDown(DualSenseButton) const { return false; }
bool DualSenseController::IsButtonPressed(DualSenseButton) const { return false; }
bool DualSenseController::IsButtonReleased(DualSenseButton) const { return false; }
bool DualShockController::IsButtonDown(DualShockButton) const { return false; }
bool DualShockController::IsButtonPressed(DualShockButton) const { return false; }
bool DualShockController::IsButtonReleased(DualShockButton) const { return false; }
}
int main() {
    try {
        Window window(1280, 720, L"test source");
        auto& mapping = EngineInput::InputMapping::Get();
        for (float dt : { 1.0f / 240.0f, 1.0f / 60.0f, 1.0f / 15.0f }) {
            pendingX = 1200; pendingY = -600; reads = 0;
            mapping.Update(dt, &window);
            const auto firstConsumer = mapping.GetFrameMouseDelta();
            const auto camera = mapping.GetFrameMouseDelta();
            if (reads != 1 || firstConsumer.x != 1200 || camera.x != 1200 || camera.y != -600)
                throw std::runtime_error("Camera lost mouse counts to an earlier consumer");
            mapping.Update(dt, &window);
            const auto idle = mapping.GetFrameMouseDelta();
            if (idle.x != 0 || idle.y != 0) throw std::runtime_error("Previous frame movement replayed");
        }
        mapping.Update(0.016f, nullptr);
        if (mapping.GetFrameMouseDelta().x != 0) throw std::runtime_error("Missing source retained stale input");
        std::cout << "PASS: one consumable read per frame, stable reads by multiple consumers, no stale counts, 15/60/240 FPS\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
}
