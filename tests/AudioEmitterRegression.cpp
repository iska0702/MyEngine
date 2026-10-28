#include "Scene/SceneNode.h"
#include "Audio/AudioSystem.h"
#include <iostream>
#include <stdexcept>
// Deterministic audio backend: exercise real component lifecycle without an audio device.
static unsigned starts=0,stops=0;static float lastVolume=0;static bool lastLoop=false;
namespace EngineAudio {
struct AudioSystem::Impl {};
AudioSystem::AudioSystem()=default;
AudioSystem::~AudioSystem()=default;
AudioSystem& AudioSystem::Get(){static AudioSystem instance;return instance;}
uint32_t AudioSystem::Play3D(const std::string&,float,float,float,AudioBus,float volume,float,float,float,bool loop){lastVolume=volume;lastLoop=loop;return ++starts;}
void AudioSystem::StopSound(uint32_t){++stops;}
void AudioSystem::SetSoundPosition(uint32_t,float,float,float){}
void AudioSystem::SetSoundVolume(uint32_t,float volume){lastVolume=volume;}
}
static void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
    auto node=std::make_shared<EngineScene::SceneNode>("Audio test");
    auto component=node->AddComponent<EngineScene::AudioEmitterComponent>();component->soundPath="fixture.wav";component->isPlaying=true;component->loop=true;component->volume=.25f;
    node->UpdateHierarchy(DirectX::XMMatrixIdentity(),0);
    Check(starts==1&&component->soundId==1&&lastLoop&&lastVolume==.25f,"Playing emitter did not start with configured parameters");
    node->UpdateHierarchy(DirectX::XMMatrixIdentity(),0);Check(starts==1,"Emitter restarted on every update");
    component->isPlaying=false;node->UpdateHierarchy(DirectX::XMMatrixIdentity(),0);Check(stops==1&&component->soundId==0,"Stop did not release runtime sound");
    component->isPlaying=true;node->UpdateHierarchy(DirectX::XMMatrixIdentity(),0);Check(starts==2,"Emitter could not restart");
    node->RemoveComponent<EngineScene::AudioEmitterComponent>();Check(stops==2&&component->soundId==0&&component->GetOwner()==nullptr,"Removing emitter leaked its sound or owner");
    auto second=node->AddComponent<EngineScene::AudioEmitterComponent>();second->soundPath="fixture.wav";second->isPlaying=true;node->UpdateHierarchy(DirectX::XMMatrixIdentity(),0);
    node.reset();Check(stops==3&&second->soundId==0&&second->GetOwner()==nullptr,"Deleting node leaked emitter sound");
    std::cout<<"PASS: audio emitter start, parameter forwarding, stable playback, stop/restart and deletion cleanup\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
