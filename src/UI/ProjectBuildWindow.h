#pragma once
#if defined(_WIN32) && defined(ENGINE_EDITOR)
#include "Core/ProjectLauncher.h"
#include "Core/ProjectBuilder.h"
#include "imgui.h"
#include <functional>
namespace EngineProject {
inline bool showBuildWindow=false;
inline std::string buildProjectPath,buildPlatform="standalone",buildWindowError;
inline Project buildDraft;
inline void ShowProjectBuild(const Project& project){buildDraft=project;buildProjectPath=Utf8(project.file);buildPlatform=project.platform;buildWindowError.clear();showBuildWindow=true;}
inline void DrawProjectBuild(HWND owner,const std::function<bool()>& saveScene){
    if(!showBuildWindow)return;
    ImGui::SetNextWindowSize(ImVec2(680,680),ImGuiCond_FirstUseEver);
    if(ImGui::Begin("Build Project",&showBuildWindow)){
        auto snapshot=ProjectBuilder::Get().Snapshot();
        ImGui::BeginDisabled(snapshot.running);
        char path[32768]{};strncpy_s(path,buildProjectPath.c_str(),_TRUNCATE);
        if(ImGui::InputText("Project (.idkp)",path,sizeof(path)))buildProjectPath=path;
        ImGui::SameLine();if(ImGui::Button("Browse...")){auto file=PickProject(false,owner);if(!file.empty())try{auto project=Project::Load(file);ShowProjectBuild(project);}catch(const std::exception& error){buildWindowError=error.what();}}
        std::error_code configError;
        bool configMatches=!buildDraft.file.empty()&&fs::equivalent(Path(buildProjectPath),buildDraft.file,configError);
        if(!configMatches){
            ImGui::TextWrapped("Load this project's configuration before building.");
            if(ImGui::Button("Load configuration"))try{ShowProjectBuild(Project::Load(Path(buildProjectPath)));}catch(const std::exception& error){buildWindowError=error.what();}
        }
        const char* names[]={"Standalone","Steam","Epic Online Services"};int platform=buildPlatform=="steam"?1:buildPlatform=="epic"?2:0;
        if(ImGui::Combo("Platform",&platform,names,3))buildPlatform=platform==1?"steam":platform==2?"epic":"standalone";
        ImGui::BeginDisabled(!configMatches);
        if(buildPlatform=="steam"){
            ImGui::InputScalar("Steam App ID",ImGuiDataType_U32,&buildDraft.steamAppId);
            ImGui::Checkbox("Restart through Steam",&buildDraft.steamRestart);
        }else if(buildPlatform=="epic"){
            auto text=[](const char* label,std::string& value,ImGuiInputTextFlags flags=0){char buffer[4096]{};strncpy_s(buffer,value.c_str(),_TRUNCATE);if(ImGui::InputText(label,buffer,sizeof(buffer),flags))value=buffer;};
            text("Product ID",buildDraft.epicProductId);text("Sandbox ID",buildDraft.epicSandboxId);text("Deployment ID",buildDraft.epicDeploymentId);
            text("Client ID",buildDraft.epicClientId);text("Client secret",buildDraft.epicClientSecret,ImGuiInputTextFlags_Password);
            ImGui::TextWrapped("Use credentials intended for public clients; these settings are included in the game build.");
        }
        if(ImGui::Button("Save build configuration"))try{
            buildDraft.platform=buildPlatform;buildDraft.Save();
            std::error_code same;if(active&&fs::equivalent(buildDraft.file,current.file,same))current=buildDraft;
            buildWindowError.clear();
        }catch(const std::exception& error){buildWindowError=error.what();}
        ImGui::EndDisabled();
        const bool available=ProjectBuilder::HasRuntime(buildPlatform);
        if(!available)ImGui::TextColored(ImVec4(1,.55f,.2f,1),"The selected runtime is not installed in this engine.");
        ImGui::BeginDisabled(!available||!configMatches);
        if(ImGui::Button("Build game"))try{
            auto file=fs::absolute(Path(buildProjectPath)).lexically_normal();
            std::error_code sameProject;
            buildDraft.platform=buildPlatform;buildDraft.Save();
            if(active&&fs::equivalent(file,current.file,sameProject)){current=buildDraft;if(!saveScene())throw std::runtime_error("Cannot save the current scene; build was not started");}
            ProjectBuilder::Get().Start(file,buildPlatform);buildWindowError.clear();
        }catch(const std::exception& error){buildWindowError=error.what();}
        ImGui::EndDisabled();ImGui::EndDisabled();
        if(snapshot.running){ImGui::SameLine();if(ImGui::Button("Cancel build"))ProjectBuilder::Get().Cancel();}
        if(!buildProjectPath.empty())ImGui::TextWrapped("Output: %s",Utf8(Path(buildProjectPath).parent_path()/"Build").c_str());
        if(!snapshot.phase.empty()){ImGui::ProgressBar(snapshot.progress,ImVec2(-1,0));ImGui::TextUnformatted(snapshot.phase.c_str());}
        if(!buildWindowError.empty())ImGui::TextColored(ImVec4(1,.35f,.25f,1),"%s",buildWindowError.c_str());
        if(!snapshot.error.empty())ImGui::TextWrapped("%s",snapshot.error.c_str());
        std::error_code sameOutput;const bool selectedMatches=!snapshot.project.empty()&&fs::equivalent(snapshot.project,Path(buildProjectPath),sameOutput);
        ImGui::BeginDisabled(snapshot.running||!snapshot.succeeded||!selectedMatches);
        if(ImGui::Button("Open Build folder"))ShellExecuteW(owner,L"open",snapshot.output.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
        ImGui::SameLine();if(ImGui::Button("Run game")){auto exe=snapshot.output/"Idontknow.exe";ShellExecuteW(owner,L"open",exe.c_str(),nullptr,snapshot.output.c_str(),SW_SHOWNORMAL);}
        ImGui::EndDisabled();
        if(!snapshot.logFile.empty()){ImGui::SameLine();if(ImGui::Button("Open log"))ShellExecuteW(owner,L"open",snapshot.logFile.c_str(),nullptr,nullptr,SW_SHOWNORMAL);}
        ImGui::Separator();ImGui::BeginChild("Build log",ImVec2(0,0),ImGuiChildFlags_Borders);
        const bool follow=ImGui::GetScrollY()>=ImGui::GetScrollMaxY()-5;
        for(auto& line:snapshot.lines)ImGui::TextUnformatted(line.c_str());if(follow)ImGui::SetScrollHereY(1);
        ImGui::EndChild();
    }ImGui::End();
}
}
#endif
