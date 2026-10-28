#pragma once
#include "Asset/MaterialAsset.h"
#include "Platform/PlatformUtils.h"
#include <imgui.h>
#include <filesystem>
namespace EngineUI {
inline bool DrawMaterialAsset(Renderer::PBRMaterial& m,Renderer& renderer){
    bool changed=false;
    if(ImGui::TreeNode("Material asset and preview")){
        ImGui::TextDisabled("Lit swatch (approximation); final shading is visible in the scene.");
        auto start=ImGui::GetCursorScreenPos();auto* draw=ImGui::GetWindowDrawList();
        draw->AddRectFilled(start,ImVec2(start.x+128,start.y+128),IM_COL32(30,32,38,255));
        for(int y=0;y<32;++y)for(int x=0;x<32;++x){
            float nx=(x+.5f)/16-1,ny=1-(y+.5f)/16,r2=nx*nx+ny*ny;if(r2>1)continue;
            float nz=std::sqrt(1-r2),diff=(std::max)(0.f,-nx*.4f+ny*.6f+nz*.69f);
            float spec=std::pow((std::max)(0.f,-nx*.21f+ny*.32f+nz*.92f),2+126*(1-m.roughness)*(1-m.roughness));
            auto channel=[&](float c){float v=c*(.12f+diff*(1-m.metallic)+m.emission)+spec*((1-m.metallic)*m.reflectance*.16f+m.metallic*c)*2;return (std::clamp)(std::pow((std::max)(0.f,v/(1+v)),1/2.2f),0.f,1.f);};
            auto color=ImGui::ColorConvertFloat4ToU32(ImVec4(channel(m.albedo.x),channel(m.albedo.y),channel(m.albedo.z),1));
            draw->AddRectFilled(ImVec2(start.x+x*4,start.y+y*4),ImVec2(start.x+x*4+4,start.y+y*4+4),color);
        }
        ImGui::Dummy(ImVec2(128,128));
        if(auto id=renderer.GetEditorTextureId(m.albedoMap))ImGui::Image(static_cast<ImTextureID>(id),ImVec2(128,128));
        static char assetName[96]="Material";static std::string status;
        ImGui::InputText("Material name",assetName,sizeof(assetName));
        std::string name=assetName;for(char& c:name)if(!(std::isalnum(static_cast<unsigned char>(c))||c=='_'||c=='-'))c='_';
        auto path=std::filesystem::path(EnginePlatform::PlatformUtils::ResolveContentPath("Materials"))/(name+".idkmat");
        if(ImGui::Button("Save material asset")){
            std::error_code ec;std::filesystem::create_directories(path.parent_path(),ec);std::ofstream file(path);file<<EngineAsset::EncodeMaterial(m);status=file.good()?"Material saved":"Save failed";
        }
        ImGui::SameLine();
        if(ImGui::Button("Load material asset")){std::ifstream file(path);std::string text((std::istreambuf_iterator<char>(file)),{});changed=EngineAsset::DecodeMaterial(text,m,&renderer);status=changed?"Material loaded":"Invalid or missing material";}
        ImGui::Button("Drop .idkmat here");
        if(ImGui::BeginDragDropTarget()){
            if(auto payload=ImGui::AcceptDragDropPayload("IDK_ASSET")){auto filePath=std::filesystem::u8path(static_cast<const char*>(payload->Data));if(filePath.extension()==".idkmat"){std::ifstream file(filePath);std::string text((std::istreambuf_iterator<char>(file)),{});changed=EngineAsset::DecodeMaterial(text,m,&renderer);}}
            ImGui::EndDragDropTarget();
        }
        if(!status.empty())ImGui::TextUnformatted(status.c_str());ImGui::TreePop();
    }
    if(ImGui::TreeNode("Tessellation")){
        auto& t=m.tessellation;
        changed|=ImGui::Checkbox("Enable tessellation",&t.enabled);
        changed|=ImGui::SliderFloat("Tessellation factor",&t.factor,1,16,"%.1f");
        changed|=ImGui::SliderFloat("Surface smoothing",&t.smoothing,0,1,"%.2f");
        changed|=ImGui::DragFloat("Full detail distance",&t.nearDistance,.1f,0,9999);
        changed|=ImGui::DragFloat("Minimum detail distance",&t.farDistance,.1f,.1f,10000);
        if(changed)t.Normalize();
        ImGui::TextWrapped("Adaptive hardware tessellation and Phong smoothing: raster rendering and sun shadows. DXR and collision use the original geometry. Height-map displacement is not implemented.");
        if(t.enabled&&renderer.m_dxrEnabled)ImGui::TextColored(ImVec4(1,.65f,.2f,1),"Inactive while DXR is enabled. Switch to raster rendering.");
        ImGui::TreePop();
    }
    return changed;
}
}
