#pragma once
#include "Asset/MaterialAsset.h"
#include "Prefab/PrefabSystem.h"
#include "Physics/PhysicsSystem.h"
#include "Platform/PlatformUtils.h"
#include <imgui.h>
#include <type_traits>
#include <functional>
namespace EngineUI {
inline EnginePrefab::PrefabNodeData* FindPrefabNode(EnginePrefab::PrefabNodeData& root,const std::string& name){
    if(root.name==name)return &root;for(auto& child:root.children)if(auto node=FindPrefabNode(child,name))return node;return nullptr;
}
template<class Model> bool DrawPrefabInspector(Model& model,Renderer& renderer){
    if(!ImGui::CollapsingHeader("Prefab"))return false;
    static char name[96]="ObjectPrefab";bool changed=false;
    ImGui::InputText("Asset name",name,sizeof(name));
    auto& system=EnginePrefab::PrefabSystem::Get();
    if(ImGui::Button("Save selection as prefab")){
        std::string safe=name;for(char& c:safe)if(!(std::isalnum(static_cast<unsigned char>(c))||c=='_'||c=='-'))c='_';if(safe.empty())safe="ObjectPrefab";
        auto prefab=std::make_shared<EnginePrefab::Prefab>(safe);auto& data=prefab->GetRootData();data.name=safe;data.hasMesh=true;data.localScale=model.scale;
        if constexpr(std::is_same_v<Model,Renderer::RenderDynamicModel>)data.meshType=model.filepath;
        else {data.meshType=model.name.find("Sphere")!=std::string::npos?"Sphere":model.name.find("Plane")!=std::string::npos?"Plane":"Cube";
            data.properties["static_geometry"]=std::to_string(model.indexCount)+" "+std::to_string(model.startIndexLocation)+" "+std::to_string(model.baseVertexLocation)+" "+std::to_string(model.vertexCount);}
        data.properties["model_settings"]=EngineAsset::EncodeModelAsset(model);
        data.diffuseColor={model.material.albedo.x,model.material.albedo.y,model.material.albedo.z,1};data.roughness=model.material.roughness;data.metallic=model.material.metallic;data.emissiveIntensity=model.material.emission;
        if(system.SavePrefab(prefab)){model.prefabSource=safe;model.prefabNode=data.name;changed=true;}
    }
    if(!model.prefabSource.empty()){
        ImGui::Text("Source: %s",model.prefabSource.c_str());
        auto prefab=system.GetPrefab(model.prefabSource);
        if(prefab)if(auto data=FindPrefabNode(prefab->GetRootData(),model.prefabNode)){
            if(ImGui::Button("Revert settings from prefab")){
                auto settings=data->properties.find("model_settings");if(settings!=data->properties.end())changed=EngineAsset::DecodeModelAsset(settings->second,model,&renderer);
            }
            if(ImGui::Button("Publish settings to linked instances")){
                data->properties["model_settings"]=EngineAsset::EncodeModelAsset(model);
                if(system.SavePrefab(prefab)){
                    auto apply=[&](auto& models,bool dynamic){for(auto& other:models)if(other.prefabSource==model.prefabSource&&other.prefabNode==model.prefabNode){EngineAsset::DecodeModelAsset(data->properties["model_settings"],other,&renderer);EnginePhysics::PhysicsSystem::Get().ApplyModelPhysics(&renderer,other.modelId,dynamic);}};
                    apply(renderer.GetModels(),false);apply(renderer.GetDynamicModels(),true);changed=true;
                }
            }
        }
        if(ImGui::Button("Unlink prefab")){model.prefabSource.clear();model.prefabNode.clear();changed=true;}
        ImGui::TextWrapped("Settings overrides stay local until published. Publishing updates material, collider, body and gameplay settings; instance transforms stay independent.");
    }
    if(changed)renderer.InvalidateRaytracingScene();return changed;
}
}
