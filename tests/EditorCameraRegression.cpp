#include "Renderer/CameraSystem.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

static void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(){
    try{
        using namespace DirectX;
        auto& cameras=CameraSystem::Get();cameras.Initialize(nullptr);
        Camera camera;
        camera.SetPosition(2,3,4);camera.SetYaw(XMConvertToRadians(65));camera.SetPitch(XMConvertToRadians(-25));
        cameras.SyncFromCamera(&camera,0);
        auto& profile=cameras.GetCameraProfile(0);
        Check(std::abs(profile.yaw-65)<.001f&&std::abs(profile.pitch+25)<.001f,"Camera profile angles are not degrees");
        camera.SetYaw(0);camera.SetPitch(0);cameras.SyncToCamera(&camera,0);
        Check(std::abs(camera.GetYaw()-XMConvertToRadians(65))<.0001f,"Camera yaw conversion failed");
        Check(std::abs(camera.GetPitch()-XMConvertToRadians(-25))<.0001f,"Camera pitch conversion failed");
        camera.SetFreecamMode(true);camera.SetPosition(20,30,40);camera.SetYaw(0);
        cameras.Update(.016f,&camera);
        Check(profile.position.x==2&&profile.position.y==3&&profile.position.z==4,"Editor viewport moved the game camera");
        Check(camera.GetPosition().x==20,"Camera system overrode detached editor viewport");
        auto& created=cameras.CreateCameraFromView(&camera,nullptr,"Editor test camera");
        Check(created.position.x==20&&std::abs(created.pitch+25)<.001f,"Camera from view lost its pose");
        camera.SetFreecamMode(false);camera.SetPosition(5,6,7);cameras.Update(.016f,&camera);
        Check(cameras.GetCameraProfile(0).position.x==5,"Game camera stopped synchronizing in play mode");
        std::cout<<"PASS: degree/radian camera roundtrip, detached editor viewport, camera creation and play synchronization\n";
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
