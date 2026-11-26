#pragma once
#include "Renderer/Renderer.h"
#include "Physics/PhysicsSystem.h"
#include "Audio/AudioSystem.h"
#include <unordered_map>
#include <set>
#include "Storage/SaveSystem.h"
#include <cmath>
namespace EngineScene {
class GameplayRuntime {
    struct State {bool entered=false,fired=false,open=false,initialized=false;float cooldown=0,baseYaw=0,angle=0;};
    std::unordered_map<uint32_t,State> states;
    std::string message;
    float messageTime = 0;
    std::string dialogueText;
    std::vector<DialogueChoice> choices;
    std::set<std::string> questFlags;
    bool progressLoaded = false;

    bool Available(const GameplaySettings& settings) const {
        return settings.requiredQuestFlag.empty() || questFlags.contains(settings.requiredQuestFlag);
    }
    void Open(const GameplaySettings& settings) {
        choices = settings.choices;
        if (choices.empty()) choices.push_back({"Close", {}, {}});
        dialogueText = settings.dialogue;
        for (size_t i = 0; i < choices.size(); ++i)
            dialogueText += "\n" + std::to_string(i + 1) + ". " + choices[i].label;
    }
    void LoadProgress() {
#if defined(ENGINE_RELEASE)
        EngineSave::SavePackage save;
        if (EngineSave::SaveManager::Get().LoadGame(save, "QuestProgress.save")) {
            std::istringstream input(save.sceneData);
            std::string flag;
            while (questFlags.size() < 4096 && input >> std::quoted(flag)) questFlags.insert(flag);
        }
#endif
    }
public:
    void Clear(){states.clear();message.clear();messageTime=0;dialogueText.clear();choices.clear();}
    const std::string& GetMessage() const { return dialogueText.empty() ? message : dialogueText; }
    void Choose(Renderer& renderer, size_t index) {
        if (index >= choices.size()) return;
        auto choice = choices[index];
        dialogueText.clear();
        choices.clear();
        if (!choice.questFlag.empty() && questFlags.size() < 4096 && questFlags.insert(choice.questFlag).second) {
#if defined(ENGINE_RELEASE)
            std::ostringstream progress;
            for (const auto& flag : questFlags) progress << std::quoted(flag) << '\n';
            if (!EngineSave::SaveManager::Get().SaveGame(progress.str(), {}, "QuestProgress.save")) {
                message = "Could not save quest progress.";
                messageTime = 4;
            }
#endif
        }
        if (choice.next.empty()) return;
        auto find = [&](const auto& models) {
            for (const auto& model : models)
                if (model.name == choice.next && !model.gameplay.dialogue.empty() && Available(model.gameplay)) {
                    Open(model.gameplay);
                    return true;
                }
            return false;
        };
        if (!find(renderer.GetModels()) && !find(renderer.GetDynamicModels())) {
            message = "Dialogue branch unavailable.";
            messageTime = 4;
        }
    }
    void Update(Renderer& renderer,const DirectX::XMFLOAT3& player,float dt,bool interact){
        if (!progressLoaded) { LoadProgress(); progressLoaded = true; }
        dt=(std::clamp)(dt,0.f,.1f);
        messageTime -= dt;
        if (messageTime <= 0) { messageTime = 0; message.clear(); }
        std::vector<std::string> lightToggles;
        auto target=[&](const std::string& name){
            auto find=[&](auto& models){for(auto& m:models)if(m.name==name&&m.gameplay.door)states[m.modelId].open=!states[m.modelId].open;};
            find(renderer.GetModels());find(renderer.GetDynamicModels());
        };
        auto visit=[&](auto& models,bool dynamic){for(auto& m:models){
            auto& g=m.gameplay;if(!(g.trigger||g.door||g.button||g.pickup||g.audio||!g.dialogue.empty()))continue;
            auto& state=states[m.modelId];if(!state.initialized){state.baseYaw=m.rotation.y;state.initialized=true;}
            state.cooldown=(std::max)(0.f,state.cooldown-dt);
            float x=player.x-m.position.x,y=player.y-m.position.y,z=player.z-m.position.z;
            bool isNear=x*x+y*y+z*z<=g.radius*g.radius;
            if (interact && dialogueText.empty() && m.visible && isNear && !g.dialogue.empty() && Available(g)) Open(g);
            bool fire=m.visible&&isNear&&Available(g)&&state.cooldown<=0&&(!g.once||!state.fired)&&((g.button||g.pickup)?interact:((g.trigger||g.audio)&&!state.entered));
            state.entered=isNear;
            if(fire){
                state.fired=true;state.cooldown=g.cooldown;
                if(!g.target.empty())target(g.target);
                if(g.door)state.open=!state.open;
                if(g.audio&&!g.sound.empty())EngineAudio::AudioSystem::Get().Play3D(g.sound,m.position.x,m.position.y,m.position.z,EngineAudio::AudioBus::SFX,g.volume);
                for (const auto& action : g.actions) {
                    if (action.value.empty()) continue;
                    switch (action.type) {
                    case GameplayActionType::ToggleDoor: target(action.value); break;
                    case GameplayActionType::ToggleLight: lightToggles.push_back(action.value); break;
                    case GameplayActionType::PlaySound:
                        EngineAudio::AudioSystem::Get().Play3D(action.value,m.position.x,m.position.y,m.position.z,EngineAudio::AudioBus::SFX,g.volume);
                        break;
                    case GameplayActionType::ShowMessage: message = action.value; messageTime = 4; break;
                    }
                }
                if(g.pickup){m.visible=false;m.hasCollision=false;EnginePhysics::PhysicsSystem::Get().RemoveActorForModel(m.modelId);renderer.InvalidateRaytracingScene(false);}
            }
            if(g.door){
                const float wanted=state.open?g.doorAngle:0,step=g.doorSpeed*dt;
                const float next=state.angle+(std::clamp)(wanted-state.angle,-step,step);
                if(std::abs(next-state.angle)>.0001f){state.angle=next;m.rotation.y=state.baseYaw+next;EnginePhysics::PhysicsSystem::Get().ApplyModelPhysics(&renderer,m.modelId,dynamic);renderer.InvalidateRaytracingScene(false);}
            }
        }};
        visit(renderer.GetModels(),false);visit(renderer.GetDynamicModels(),true);
        for (const auto& name : lightToggles) {
            auto toggle = [&](auto& lights) {
                for (auto& light : lights) if (light.name == name) light.enabled = !light.enabled;
            };
            toggle(renderer.GetPointLights());
            toggle(renderer.GetSpotLights());
        }
    }
};
}
