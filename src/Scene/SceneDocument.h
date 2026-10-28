#pragma once
#include "SceneGraph.h"
#include "Renderer/Renderer.h"
#include <sstream>
#include <iomanip>
#include <functional>
namespace EngineScene {
inline std::string WriteHierarchy(){
    std::ostringstream out;out.precision(9);size_t next=0;
    std::function<void(std::shared_ptr<SceneNode>,size_t)> visit=[&](auto node,size_t parent){
        if(!node)return;size_t id=++next;auto& t=node->GetTransform();
        out<<"SCENE_NODE "<<id<<' '<<parent<<' '<<std::quoted(node->GetName())<<' '<<std::quoted(node->GetTag())<<' '<<node->IsActive()<<' '
           <<t.localPosition.x<<' '<<t.localPosition.y<<' '<<t.localPosition.z<<' '<<t.localRotation.x<<' '<<t.localRotation.y<<' '<<t.localRotation.z<<' '
           <<t.localScale.x<<' '<<t.localScale.y<<' '<<t.localScale.z<<'\n';
        if(auto c=node->GetComponent<MeshComponent>())out<<"NODE_MESH "<<id<<' '<<c->isDynamic<<' '<<std::quoted(c->modelName)<<'\n';
        if(auto c=node->GetComponent<PointLightComponent>())out<<"NODE_POINT "<<id<<' '<<c->lightIndex<<' '<<c->relativeOffset.x<<' '<<c->relativeOffset.y<<' '<<c->relativeOffset.z<<'\n';
        if(auto c=node->GetComponent<SpotLightComponent>())out<<"NODE_SPOT "<<id<<' '<<c->lightIndex<<' '<<c->relativeOffset.x<<' '<<c->relativeOffset.y<<' '<<c->relativeOffset.z<<'\n';
        if(auto c=node->GetComponent<AudioEmitterComponent>())out<<"NODE_AUDIO "<<id<<' '<<std::quoted(c->soundPath)<<' '<<c->volume<<' '<<c->maxDistance<<' '<<c->loop<<'\n';
        for(const auto& c:node->GetChildren())visit(c,id);
    };
    auto root=SceneGraph::Get().GetRoot();if(root)for(auto& node:root->GetChildren())visit(node,0);
    return out.str();
}
inline void ReadHierarchy(const std::string& text,Renderer& renderer){
    auto& graph=SceneGraph::Get();graph.Clear();
    std::unordered_map<size_t,std::shared_ptr<SceneNode>> nodes;nodes[0]=graph.GetRoot();
    std::istringstream all(text);std::string line;
    while(std::getline(all,line)){
        std::istringstream in(line);std::string tag;size_t id;in>>tag>>id;
        if(tag=="SCENE_NODE"){
            size_t parent;std::string name,label;bool active;Transform transform;
            if(!(in>>parent>>std::quoted(name)>>std::quoted(label)>>active>>transform.localPosition.x>>transform.localPosition.y>>transform.localPosition.z
                >>transform.localRotation.x>>transform.localRotation.y>>transform.localRotation.z>>transform.localScale.x>>transform.localScale.y>>transform.localScale.z)||!nodes.count(parent)||nodes.size()>100000)continue;
            auto node=graph.CreateNode(name,nodes[parent]);node->SetTag(label);node->SetActive(active);node->GetTransform()=transform;nodes[id]=node;
        }else if(nodes.count(id)){
            auto node=nodes[id];
            if(tag=="NODE_MESH"){
                bool dynamic;std::string name;if(!(in>>dynamic>>std::quoted(name)))continue;
                auto attach=[&](auto& models){for(auto& m:models)if(m.name==name){node->AddComponent<MeshComponent>(m.modelId,dynamic,name);break;}};
                if(dynamic)attach(renderer.GetDynamicModels());else attach(renderer.GetModels());
            }else if(tag=="NODE_POINT"){auto c=node->AddComponent<PointLightComponent>();in>>c->lightIndex>>c->relativeOffset.x>>c->relativeOffset.y>>c->relativeOffset.z;}
            else if(tag=="NODE_SPOT"){auto c=node->AddComponent<SpotLightComponent>();in>>c->lightIndex>>c->relativeOffset.x>>c->relativeOffset.y>>c->relativeOffset.z;}
            else if(tag=="NODE_AUDIO"){auto c=node->AddComponent<AudioEmitterComponent>();in>>std::quoted(c->soundPath)>>c->volume>>c->maxDistance>>c->loop;}
        }
    }
    graph.Update(0);
}
}
