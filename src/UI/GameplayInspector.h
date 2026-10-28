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
        changed|=ImGui::Checkbox("Button (press F nearby)",&g.button);
        changed|=ImGui::Checkbox("Pickup (press F nearby)",&g.pickup);
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
        ImGui::TextWrapped("Triggers use a sphere around the object origin. In Play, F activates nearby buttons and pickups. Target names must be unique. Doors rotate around the mesh pivot.");
    }
    return changed;
}
}
