#include "Core/Project.h"
#include <windows.h>
#include <iostream>

int main(int argc,char** argv){
    if(argc!=2)return 1;
    namespace fs=std::filesystem;
    const auto root=fs::path(argv[1])/("config-"+std::to_string(GetCurrentProcessId()));
    fs::create_directories(root/"Content");
    { std::ofstream legacy(root/"Legacy.idkp"); legacy<<"IDKP 1\nname \"Legacy\"\ncontent \"Content\"\nmainMenu \"\"\n"; }
    auto old=EngineProject::Project::Load(root/"Legacy.idkp");
    if(old.discordEnabled || old.discordAppId!=0)return 7;
    EngineProject::Project project;project.file=root/"Config.idkp";project.name="Build configuration";project.mainMenu.clear();
    project.platform="steam";project.steamAppId=123456;project.steamRestart=true;project.Save();
    auto loaded=EngineProject::Project::Load(project.file);
    if(loaded.platform!="steam"||loaded.steamAppId!=123456||!loaded.steamRestart)return 2;
    loaded.platform="epic";loaded.epicProductId="product";loaded.epicSandboxId="sandbox";
    loaded.epicDeploymentId="deployment";loaded.epicClientId="client";loaded.epicClientSecret="test-secret";loaded.Save();
    loaded=EngineProject::Project::Load(project.file);
    if(loaded.platform!="epic"||loaded.epicProductId!="product"||loaded.epicSandboxId!="sandbox"||
       loaded.epicDeploymentId!="deployment"||loaded.epicClientId!="client"||loaded.epicClientSecret!="test-secret"||loaded.steamAppId!=123456)return 3;
    if(loaded.discordEnabled||loaded.discordAppId!=0)return 4;
    loaded.discordEnabled=true;loaded.discordAppId=123456789012345678ULL;loaded.discordState="Playing";loaded.discordDetails="Level one";loaded.Save();
    auto discord=EngineProject::Project::Load(project.file);
    if(!discord.discordEnabled||discord.discordAppId!=123456789012345678ULL||discord.discordState!="Playing"||discord.discordDetails!="Level one"||discord.platform!="epic")return 5;
    discord.discordAppId=0;try{discord.Validate();return 6;}catch(const std::exception&){}
    std::cout<<"PASS: Steam, Epic and Discord configuration survives project save and reload\n";return 0;
}
