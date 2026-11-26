#pragma once

#include "Core/Project.h"
#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace EngineUI {
struct HUDValues {
    float x, y, z;
    float speedKmh;
    const char* fpsLabel;
    bool driving;
    const std::string& message;
};

inline void DrawGameHUD(const EngineProject::HUDLayout& layout, const HUDValues& values, bool preview) {
    using Binding = EngineProject::HUDBinding;
    const auto* viewport = ImGui::GetMainViewport();
    auto* draw = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    auto drawText = [&](const EngineProject::HUDWidget& widget, const char* text) {
        if (!*text) return;
        const float wrap = viewport->Size.x * .8f;
        const auto size = font->CalcTextSizeA(widget.fontSize, 1.e6f, wrap, text);
        const ImVec2 position(
            viewport->Pos.x + widget.x * (std::max)(0.f, viewport->Size.x - size.x),
            viewport->Pos.y + widget.y * (std::max)(0.f, viewport->Size.y - size.y));
        draw->AddText(font, widget.fontSize, {position.x + 1.f, position.y + 1.f},
                      IM_COL32(0, 0, 0, 230), text, nullptr, wrap);
        draw->AddText(font, widget.fontSize, position, IM_COL32_WHITE, text, nullptr, wrap);
    };
    bool hasMessageWidget = false;
    for (const auto& widget : layout) {
        char value[96]{};
        switch (widget.binding) {
        case Binding::Text: break;
        case Binding::PlayerPosition:
            std::snprintf(value, sizeof(value), "%.1f, %.1f, %.1f", values.x, values.y, values.z);
            break;
        case Binding::VehicleSpeed:
            if (!values.driving && !preview) continue;
            std::snprintf(value, sizeof(value), "%.0f km/h", values.speedKmh);
            break;
        case Binding::FrameRate: std::snprintf(value, sizeof(value), "%s FPS", values.fpsLabel); break;
        case Binding::TriggerMessage: {
            hasMessageWidget = true;
            if (values.message.empty() && !preview) continue;
            const auto text = widget.label + (values.message.empty() ? "Trigger message" : values.message);
            drawText(widget, text.c_str());
            continue;
        }
        }
        const auto text = widget.label + value;
        drawText(widget, text.c_str());
    }
    if (!hasMessageWidget && !values.message.empty()) {
        EngineProject::HUDWidget message;
        message.x = .5f;
        message.y = .82f;
        drawText(message, values.message.c_str());
    }
}

#if defined(ENGINE_EDITOR)
inline void DrawHUDEditor(bool& open, EngineProject::HUDLayout& draft, bool& preview) {
    if (!open) return;
    static std::string status;
    ImGui::SetNextWindowSize({390, 470}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Game HUD", &open)) {
        ImGui::Checkbox("Preview in viewport", &preview);
        const char* bindings[] = {"Text", "Player position", "Vehicle speed", "Frame rate", "Trigger message"};
        size_t remove = EngineProject::MaxHUDWidgets;
        if (ImGui::BeginChild("HUD widgets", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 3), ImGuiChildFlags_Borders)) {
            for (size_t i = 0; i < draft.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                auto& widget = draft[i];
                int binding = static_cast<int>(widget.binding);
                if (ImGui::Combo("Data", &binding, bindings, IM_ARRAYSIZE(bindings)))
                    widget.binding = static_cast<EngineProject::HUDBinding>(binding);
                char text[193]{};
                std::memcpy(text, widget.label.data(), widget.label.size());
                if (ImGui::InputText("Text or prefix", text, sizeof(text))) widget.label = text;
                ImGui::SliderFloat("Horizontal", &widget.x, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                ImGui::SliderFloat("Vertical", &widget.y, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                ImGui::SliderFloat("Font size", &widget.fontSize, 12.f, 64.f, "%.0f px", ImGuiSliderFlags_AlwaysClamp);
                if (ImGui::Button("Remove widget")) remove = i;
                ImGui::Separator();
                ImGui::PopID();
            }
            if (draft.size() < EngineProject::MaxHUDWidgets && ImGui::Button("Add widget"))
                draft.emplace_back();
        }
        ImGui::EndChild();
        if (remove < draft.size())
            draft.erase(draft.begin() + remove);
        if (ImGui::Button("Save HUD")) {
            try {
                auto project = EngineProject::current;
                project.hud = draft;
                project.Save();
                EngineProject::current.hud = draft;
                status = "HUD saved.";
            } catch (const std::exception& error) {
                status = error.what();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard edits")) {
            draft = EngineProject::current.hud;
            status.clear();
        }
        ImGui::TextWrapped("%s", status.c_str());
    }
    ImGui::End();
}
#endif
}
