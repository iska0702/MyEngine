#pragma once
#if defined(_WIN32) && defined(ENGINE_EDITOR)
#include "Core/ProjectLauncher.h"
#include "ProjectBuildWindow.h"
#include "imgui.h"
#include "Renderer/AdvancedShaderDelivery.h"
namespace EngineProject {
inline void DrawSettings(bool& visible,HWND owner,const std::function<bool()>& saveScene){
    static Project draft;
    static bool wasVisible=false;
    if(!visible||!active){wasVisible=false;return;}
    if(!wasVisible || draft.file!=current.file)draft=current;
    wasVisible=true;
    ImGui::SetNextWindowSize(ImVec2(680,640),ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(560,300),ImVec2(10000,10000));
    if(ImGui::Begin("Project Settings",&visible)){
        ImGui::TextWrapped("Project: %s",Utf8(draft.file).c_str());
        auto text=[](const char* label,std::string& value){char buffer[4096]{};strncpy_s(buffer,value.c_str(),_TRUNCATE);if(ImGui::InputText(label,buffer,sizeof(buffer)))value=buffer;};
        text("Project name",draft.name);text("Content directory",draft.content);text("Editor scene",draft.editorScene);text("First level (.level/.pak)",draft.startupLevel);
        if(ImGui::CollapsingHeader("Project information")){text("Developer",draft.developer);text("Version",draft.version);text("Description",draft.description);}
        for(const char* section:{"Physics","Player","Ragdoll","Camera"}){
            if(ImGui::CollapsingHeader(section))draft.tuning.Visit([&](const char* key,float& value,float lo,float hi,const char* group,const char* label){if(strcmp(group,section)==0){if(strcmp(key,"maxSubSteps")==0){int steps=(int)value;if(ImGui::DragInt(label,&steps,1,(int)lo,(int)hi,"%d",ImGuiSliderFlags_AlwaysClamp))value=(float)steps;}else ImGui::DragFloat(label,&value,.05f,lo,hi,"%.2f",ImGuiSliderFlags_AlwaysClamp);}});
        }
        ImGui::TextWrapped("Physics, player, camera and ragdoll defaults apply on next project launch.");
        ImGui::TextWrapped("Leave First level empty to start with the saved editor scene. Paths are relative to the project folder.");
        text("Main menu (HTML)",draft.mainMenu);text("Splash (HTML)",draft.splash);
        ImGui::SliderFloat("Splash seconds",&draft.splashSeconds,0,30);ImGui::Checkbox("Show menu on game startup",&draft.startWithMenu);
        ImGui::InputInt("Window width",&draft.width);ImGui::InputInt("Window height",&draft.height);
        if(ImGui::CollapsingHeader("Steam")){
            ImGui::InputScalar("Steam App ID",ImGuiDataType_U32,&draft.steamAppId);
            ImGui::Checkbox("Restart through Steam",&draft.steamRestart);
        }
        if(ImGui::CollapsingHeader("Epic Online Services")){
            text("Product ID",draft.epicProductId);text("Sandbox ID",draft.epicSandboxId);
            text("Deployment ID",draft.epicDeploymentId);text("Client ID",draft.epicClientId);
            char secret[4096]{};strncpy_s(secret,draft.epicClientSecret.c_str(),_TRUNCATE);
            if(ImGui::InputText("Client secret",secret,sizeof(secret),ImGuiInputTextFlags_Password))draft.epicClientSecret=secret;
            ImGui::TextWrapped("Client credentials are stored in the project and shipped with the game. Use credentials intended for public clients.");
        }
        if(ImGui::CollapsingHeader("Discord")){
            ImGui::Checkbox("Enable Discord Social SDK",&draft.discordEnabled);
            ImGui::InputScalar("Discord Application ID",ImGuiDataType_U64,&draft.discordAppId);
            text("Discord activity state",draft.discordState);
            text("Discord activity details",draft.discordDetails);
            ImGui::TextWrapped("Changes apply on the next project launch. Create an application in the Discord Developer Portal to obtain its Application ID.");
        }
        static std::string status;
        if(ImGui::Button("Save project settings")){try{draft.platform=current.platform;draft.Save();current=draft;std::error_code same;if(showBuildWindow&&fs::equivalent(buildDraft.file,draft.file,same))buildDraft=draft;status="Saved. Startup settings apply on next launch.";}catch(const std::exception& e){status=e.what();}}
        ImGui::SameLine();if(ImGui::Button("Build game")){try{draft.platform=current.platform;draft.Save();current=draft;ShowProjectBuild(draft);}catch(const std::exception& e){status=e.what();}}
        ImGui::TextWrapped("%s",status.c_str());
    }ImGui::End();
}
}

#endif
