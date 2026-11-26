#pragma once
#include "Scene/GameplaySettings.h"
#include <imgui.h>
#include <cstring>
namespace EngineUI {
inline bool DrawGameplayInspector(EngineScene::GameplaySettings& g){
    bool changed=false;
    if(ImGui::CollapsingHeader("Gameplay components")){
        changed|=ImGui::Checkbox("Proximity trigger",&g.trigger);
        changed|=ImGui::Checkbox("Door (rotate around local Y)",&g.door);
        changed|=ImGui::Checkbox("Button (press E nearby)",&g.button);
        changed|=ImGui::Checkbox("Pickup (press E nearby)",&g.pickup);
        changed|=ImGui::Checkbox("Spatial sound on activation",&g.audio);
        changed|=ImGui::DragFloat("Activation radius (m)",&g.radius,.05f,.1f,1000);
        changed|=ImGui::Checkbox("Activate once",&g.once);
        changed|=ImGui::DragFloat("Activation cooldown (s)",&g.cooldown,.05f,.05f,3600);
        char target[256]{};std::strncpy(target,g.target.c_str(),sizeof(target)-1);
        if(ImGui::InputText("Target door name",target,sizeof(target))){g.target=target;changed=true;}
        if(g.door){changed|=ImGui::SliderFloat("Open angle (degrees)",&g.doorAngle,-180,180);changed|=ImGui::DragFloat("Opening speed (deg/s)",&g.doorSpeed,1,1,720);}
        if(g.audio){char sound[1024]{};std::strncpy(sound,g.sound.c_str(),sizeof(sound)-1);if(ImGui::InputText("Sound asset",sound,sizeof(sound))){g.sound=sound;changed=true;}
            if(ImGui::BeginDragDropTarget()){if(auto payload=ImGui::AcceptDragDropPayload("IDK_ASSET")){const char* path=static_cast<const char*>(payload->Data);g.sound=path;changed=true;}ImGui::EndDragDropTarget();}
            changed|=ImGui::SliderFloat("Sound volume",&g.volume,0,1);
        }
        if (ImGui::TreeNode("Activation actions")) {
            const char* types[] = {"Toggle door", "Toggle light", "Play sound", "Show message"};
            const char* fields[] = {"Door name", "Light name", "Sound asset", "Message"};
            size_t remove = g.actions.size();
            for (size_t i = 0; i < g.actions.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                auto& action = g.actions[i];
                int type = static_cast<int>(action.type);
                if (ImGui::Combo("Action", &type, types, IM_ARRAYSIZE(types))) {
                    action.type = static_cast<EngineScene::GameplayActionType>(type);
                    action.value.clear();
                    changed = true;
                }
                char value[1024]{};
                std::strncpy(value, action.value.c_str(), sizeof(value) - 1);
                if (ImGui::InputText(fields[type], value, sizeof(value))) {
                    action.value = value;
                    changed = true;
                }
                if (ImGui::Button("Remove action")) remove = i;
                ImGui::Separator();
                ImGui::PopID();
            }
            if (remove < g.actions.size()) {
                g.actions.erase(g.actions.begin() + remove);
                changed = true;
            }
            if (g.actions.size() < 32 && ImGui::Button("Add action")) {
                if (!(g.trigger || g.button || g.pickup || g.audio)) g.trigger = true;
                g.actions.emplace_back();
                changed = true;
            }
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Dialogue and quest progress")) {
            auto input = [&](const char* label, std::string& value) {
                char buffer[1024]{};
                std::strncpy(buffer, value.c_str(), sizeof(buffer) - 1);
                if (ImGui::InputText(label, buffer, sizeof(buffer))) { value = buffer; changed = true; }
            };
            input("Dialogue text", g.dialogue);
            input("Required quest flag", g.requiredQuestFlag);
            size_t remove = g.choices.size();
            for (size_t i = 0; i < g.choices.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                input("Choice", g.choices[i].label);
                input("Next dialogue object", g.choices[i].next);
                input("Set quest flag", g.choices[i].questFlag);
                if (ImGui::Button("Remove choice")) remove = i;
                ImGui::Separator();
                ImGui::PopID();
            }
            if (remove < g.choices.size()) {
                g.choices.erase(g.choices.begin() + remove);
                changed = true;
            }
            if (g.choices.size() < 4 && ImGui::Button("Add choice")) {
                g.choices.push_back({"Continue", {}, {}});
                changed = true;
            }
            ImGui::TextWrapped("Press E near the object, then select a response with 1-4. Leave the next object empty to end the dialogue.");
            ImGui::TreePop();
        }
        ImGui::TextWrapped("Triggers use a sphere around the object origin. In Play, E activates nearby buttons and pickups. Use unique target names. Doors rotate around the mesh pivot.");
    }
    return changed;
}
}
