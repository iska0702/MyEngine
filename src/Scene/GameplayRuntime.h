#pragma once
#include "Renderer/Renderer.h"
#include "Physics/PhysicsSystem.h"
#include "Audio/AudioSystem.h"
#include <unordered_map>
#include <cmath>
namespace EngineScene {
class GameplayRuntime {
    struct State {bool entered=false,fired=false,open=false,initialized=false;float cooldown=0,baseYaw=0,angle=0;};
    std::unordered_map<uint32_t,State> states;
public:
    void Clear(){states.clear();}
    void Update(Renderer& renderer,const DirectX::XMFLOAT3& player,float dt,bool interact){
        dt=(std::clamp)(dt,0.f,.1f);
        auto target=[&](const std::string& name){
            auto find=[&](auto& models){for(auto& m:models)if(m.name==name&&m.gameplay.door)states[m.modelId].open=!states[m.modelId].open;};
            find(renderer.GetModels());find(renderer.GetDynamicModels());
        };
        auto visit=[&](auto& models,bool dynamic){for(auto& m:models){
            auto& g=m.gameplay;if(!(g.trigger||g.door||g.button||g.pickup||g.audio))continue;
            auto& state=states[m.modelId];if(!state.initialized){state.baseYaw=m.rotation.y;state.initialized=true;}
            state.cooldown=(std::max)(0.f,state.cooldown-dt);
            float x=player.x-m.position.x,y=player.y-m.position.y,z=player.z-m.position.z;
            bool isNear=x*x+y*y+z*z<=g.radius*g.radius;
            bool fire=m.visible&&isNear&&state.cooldown<=0&&(!g.once||!state.fired)&&((g.button||g.pickup)?interact:((g.trigger||g.audio)&&!state.entered));
            state.entered=isNear;
            if(fire){
                state.fired=true;state.cooldown=g.cooldown;
                if(!g.target.empty())target(g.target);
                if(g.door)state.open=!state.open;
                if(g.audio&&!g.sound.empty())EngineAudio::AudioSystem::Get().Play3D(g.sound,m.position.x,m.position.y,m.position.z,EngineAudio::AudioBus::SFX,g.volume);
                if(g.pickup){m.visible=false;m.hasCollision=false;EnginePhysics::PhysicsSystem::Get().RemoveActorForModel(m.modelId);renderer.InvalidateRaytracingScene(false);}
            }
            if(g.door){
                const float wanted=state.open?g.doorAngle:0,step=g.doorSpeed*dt;
                const float next=state.angle+(std::clamp)(wanted-state.angle,-step,step);
                if(std::abs(next-state.angle)>.0001f){state.angle=next;m.rotation.y=state.baseYaw+next;EnginePhysics::PhysicsSystem::Get().ApplyModelPhysics(&renderer,m.modelId,dynamic);renderer.InvalidateRaytracingScene(false);}
            }
        }};
        visit(renderer.GetModels(),false);visit(renderer.GetDynamicModels(),true);
    }
};
}
