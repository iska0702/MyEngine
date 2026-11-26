#pragma once

#include <cmath>
#include <iomanip>
#include <istream>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace EngineProject {
enum class HUDBinding { Text, PlayerPosition, VehicleSpeed, FrameRate, TriggerMessage };

struct HUDWidget {
    HUDBinding binding = HUDBinding::Text;
    float x = .05f;
    float y = .05f;
    float fontSize = 22.f;
    std::string label = "Label";
};

using HUDLayout = std::vector<HUDWidget>;
inline constexpr size_t MaxHUDWidgets = 32;

inline void ValidateHUD(const HUDLayout& layout) {
    if (layout.size() > MaxHUDWidgets) throw std::runtime_error("Too many HUD widgets");
    for (const auto& widget : layout) {
        const int binding = static_cast<int>(widget.binding);
        if (binding < 0 || binding > static_cast<int>(HUDBinding::TriggerMessage) ||
            !std::isfinite(widget.x) || widget.x < 0 || widget.x > 1 ||
            !std::isfinite(widget.y) || widget.y < 0 || widget.y > 1 ||
            !std::isfinite(widget.fontSize) || widget.fontSize < 12 || widget.fontSize > 64 ||
            widget.label.size() > 192 || widget.label.find_first_of("\r\n") != std::string::npos)
            throw std::runtime_error("Invalid HUD widget");
    }
}

inline void WriteHUD(std::ostream& out, const HUDLayout& layout) {
    for (const auto& widget : layout)
        out << "#hudWidget " << static_cast<int>(widget.binding) << ' ' << widget.x << ' ' << widget.y
            << ' ' << widget.fontSize << ' ' << std::quoted(widget.label) << '\n';
}

inline void ReadHUDWidget(std::istream& row, HUDLayout& layout) {
    int binding = 0;
    HUDWidget widget;
    if (!(row >> binding >> widget.x >> widget.y >> widget.fontSize >> std::quoted(widget.label)) ||
        layout.size() >= MaxHUDWidgets) throw std::runtime_error("Invalid HUD widget");
    widget.binding = static_cast<HUDBinding>(binding);
    layout.push_back(std::move(widget));
}
}
