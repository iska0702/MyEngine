#include "Core/Project.h"
#include <windows.h>
#include <iostream>

int main(int argc,char** argv){
    if(argc!=2)return 1;
    namespace fs=std::filesystem;
    const auto root=fs::path(argv[1])/("config-"+std::to_string(GetCurrentProcessId()));
    fs::create_directories(root/"Content");
    EngineProject::Project project;project.file=root/"Config.idkp";project.name="Build configuration";project.mainMenu.clear();
    project.platform="steam";project.steamAppId=123456;project.steamRestart=true;project.Save();
    auto loaded=EngineProject::Project::Load(project.file);
    if(loaded.platform!="steam"||loaded.steamAppId!=123456||!loaded.steamRestart)return 2;
    loaded.platform="epic";loaded.epicProductId="product";loaded.epicSandboxId="sandbox";
    loaded.epicDeploymentId="deployment";loaded.epicClientId="client";loaded.epicClientSecret="test-secret";loaded.Save();
    loaded=EngineProject::Project::Load(project.file);
    if(loaded.platform!="epic"||loaded.epicProductId!="product"||loaded.epicSandboxId!="sandbox"||
       loaded.epicDeploymentId!="deployment"||loaded.epicClientId!="client"||loaded.epicClientSecret!="test-secret"||loaded.steamAppId!=123456)return 3;
    std::cout<<"PASS: Steam and Epic build configuration survives project save and reload\n";return 0;
}
