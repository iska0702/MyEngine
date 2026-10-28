#pragma once
#include "Renderer.h"
#include "CameraSystem.h"
#include "Scene/SceneGraph.h"
#include "imgui.h"
#include <cmath>
#include <functional>

namespace EngineUI {
// Editor-only vector overlays. Nothing is added to the rendered/exported scene.
class GizmoCanvas {
    DirectX::XMMATRIX m_viewProjection;
    ImVec2 m_size, m_origin;
public:
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    GizmoCanvas(Camera& camera, float fov, float width, float height)
        : m_viewProjection(camera.GetViewMatrix()*camera.GetCleanProjectionMatrix(DirectX::XMConvertToRadians(fov),width/height,.1f,2000.f)),
          m_size(width,height),m_origin(ImGui::GetMainViewport()->Pos) {}
    ImVec2 Screen(DirectX::FXMVECTOR clip) const {
        const float w=DirectX::XMVectorGetW(clip);
        return {m_origin.x+(DirectX::XMVectorGetX(clip)/w*.5f+.5f)*m_size.x,
                m_origin.y+(-DirectX::XMVectorGetY(clip)/w*.5f+.5f)*m_size.y};
    }
    bool Point(const DirectX::XMFLOAT3& point, ImVec2& screen) const {
        using namespace DirectX;
        auto p=XMVector4Transform(XMVectorSet(point.x,point.y,point.z,1),m_viewProjection);
        const float w=XMVectorGetW(p),z=XMVectorGetZ(p);
        if(!std::isfinite(w)||w<=.001f||z<0||z>w)return false;
        screen=Screen(p);
        return std::isfinite(screen.x)&&std::isfinite(screen.y)&&screen.x>=m_origin.x&&screen.y>=m_origin.y&&screen.x<=m_origin.x+m_size.x&&screen.y<=m_origin.y+m_size.y;
    }
    void Line(const DirectX::XMFLOAT3& from,const DirectX::XMFLOAT3& to,ImU32 color,float thickness=1.4f) {
        using namespace DirectX;
        auto a=XMVector4Transform(XMVectorSet(from.x,from.y,from.z,1),m_viewProjection);
        auto b=XMVector4Transform(XMVectorSet(to.x,to.y,to.z,1),m_viewProjection);
        // Clip to near/far planes before dividing, including segments crossing the camera.
        for(int plane=0;plane<2;++plane){
            float da=plane?XMVectorGetW(a)-XMVectorGetZ(a):XMVectorGetZ(a);
            float db=plane?XMVectorGetW(b)-XMVectorGetZ(b):XMVectorGetZ(b);
            if(!std::isfinite(da)||!std::isfinite(db)||(da<0&&db<0))return;
            if(da<0)a=XMVectorLerp(a,b,da/(da-db));
            else if(db<0)b=XMVectorLerp(a,b,da/(da-db));
        }
        if(XMVectorGetW(a)<=.001f||XMVectorGetW(b)<=.001f)return;
        auto x=Screen(a),y=Screen(b);
        if(std::isfinite(x.x)&&std::isfinite(x.y)&&std::isfinite(y.x)&&std::isfinite(y.y))draw->AddLine(x,y,color,thickness);
    }
    bool Badge(const DirectX::XMFLOAT3& point,const std::string& name,ImU32 color,int kind,bool selected=false) {
        ImVec2 p;if(!Point(point,p))return false;
        draw->AddCircleFilled(p,10,IM_COL32(20,24,32,235),16);
        if(kind==0){ // camera body and lens
            draw->AddRect({p.x-6,p.y-4},{p.x+3,p.y+4},color,1,0,1.8f);
            draw->AddTriangle({p.x+3,p.y-3},{p.x+7,p.y-5},{p.x+7,p.y+5},color,1.6f);
        }else if(kind==3){ // audio emitter
            draw->AddTriangle({p.x-6,p.y},{p.x,p.y-5},{p.x,p.y+5},color,1.6f);
            draw->AddCircle({p.x+2,p.y},5,color,12,1.4f);
        }else{
            draw->AddCircle(p,4,color,12,1.7f);
            for(int i=0;i<8;++i){float a=i*DirectX::XM_2PI/8;draw->AddLine({p.x+6*cosf(a),p.y+6*sinf(a)},{p.x+9*cosf(a),p.y+9*sinf(a)},color,1.4f);}
        }
        auto size=ImGui::CalcTextSize(name.c_str());
        ImVec2 lo{p.x+13,p.y-9},hi{p.x+19+size.x,p.y+11};
        draw->AddRectFilled(lo,hi,IM_COL32(18,22,30,210),3);
        draw->AddText({lo.x+3,lo.y+2},color,name.c_str());
        if(selected)draw->AddCircle(p,12,IM_COL32(255,220,70,255),20,2);
        auto mouse=ImGui::GetIO().MousePos;
        bool hover=mouse.x>=p.x-11&&mouse.x<=hi.x&&mouse.y>=p.y-11&&mouse.y<=hi.y;
        if(hover&&!ImGui::GetIO().WantCaptureMouse){draw->AddRect({p.x-12,p.y-12},hi,color,3);return ImGui::IsMouseClicked(ImGuiMouseButton_Left);}
        return false;
    }
    void Ring(const DirectX::XMFLOAT3& center,DirectX::FXMVECTOR u,DirectX::FXMVECTOR v,float radius,ImU32 color) {
        using namespace DirectX;
        if(!std::isfinite(radius)||radius<=0)return;
        XMFLOAT3 previous{};
        for(int i=0;i<=32;++i){float a=i*XM_2PI/32;XMFLOAT3 point;XMStoreFloat3(&point,XMLoadFloat3(&center)+u*(cosf(a)*radius)+v*(sinf(a)*radius));if(i)Line(previous,point,color);previous=point;}
    }
};

inline void DrawSceneGizmos(Renderer& renderer,Camera& view,float width,float height,int& selectedType,int& selectedIndex,bool& showLights) {
    using namespace DirectX;
    if(!view.IsFreecamMode()||width<=0||height<=0)return;
    GizmoCanvas c(view,renderer.GetFOV(),width,height);
    auto* viewport=ImGui::GetMainViewport();
    c.draw->PushClipRect(viewport->Pos,{viewport->Pos.x+width,viewport->Pos.y+height},true);
    auto& cameras=CameraSystem::Get();
    for(size_t i=0;i<cameras.GetCameraCount();++i){
        const auto& camera=cameras.GetCameraProfile(i);if(!camera.showGizmoInWorld)continue;
        const ImU32 color=IM_COL32(85,190,255,245);
        if(c.Badge(camera.position,"Camera: "+camera.name,color,0,cameras.GetSelectedIndex()==i))cameras.SetSelectedIndex(i,true);
        float pitch=XMConvertToRadians(camera.pitch),yaw=XMConvertToRadians(camera.yaw);
        auto forward=XMVectorSet(cosf(pitch)*sinf(yaw),sinf(pitch),cosf(pitch)*cosf(yaw),0);
        auto right=XMVector3Normalize(XMVector3Cross(XMVectorSet(0,1,0,0),forward));auto up=XMVector3Cross(forward,right);
        float halfY=tanf(XMConvertToRadians((std::clamp)(camera.fov,20.f,130.f))*.5f)*1.2f;
        XMFLOAT3 corners[4];auto center=XMLoadFloat3(&camera.position)+forward*1.2f;
        for(int j=0;j<4;++j){XMStoreFloat3(&corners[j],center+right*((j==0||j==3?-1.f:1.f)*halfY*width/height)+up*((j<2?1.f:-1.f)*halfY));c.Line(camera.position,corners[j],color);}
        for(int j=0;j<4;++j)c.Line(corners[j],corners[(j+1)%4],color);
    }
    for(size_t i=0;i<renderer.GetPointLights().size();++i){
        const auto& light=renderer.GetPointLights()[i];bool selected=selectedType==3&&selectedIndex==int(i);
        ImU32 color=light.enabled?IM_COL32(255,215,80,245):IM_COL32(130,135,145,230);
        if(c.Badge(light.position,"Point: "+light.name+(light.enabled?"":" (off)"),color,1,selected)){selectedType=3;selectedIndex=int(i);}
        if(selected){c.Ring(light.position,XMVectorSet(1,0,0,0),XMVectorSet(0,0,1,0),light.range,color);c.Ring(light.position,XMVectorSet(1,0,0,0),XMVectorSet(0,1,0,0),light.range,color);c.Ring(light.position,XMVectorSet(0,1,0,0),XMVectorSet(0,0,1,0),light.range,color);}
    }
    for(size_t i=0;i<renderer.GetSpotLights().size();++i){
        const auto& light=renderer.GetSpotLights()[i];bool selected=selectedType==4&&selectedIndex==int(i);
        ImU32 color=light.enabled?IM_COL32(255,170,70,245):IM_COL32(130,135,145,230);
        if(c.Badge(light.position,"Spot: "+light.name+(light.enabled?"":" (off)"),color,2,selected)){selectedType=4;selectedIndex=int(i);}
        auto direction=XMLoadFloat3(&light.direction);
        float length=XMVectorGetX(XMVector3LengthSq(direction));if(!std::isfinite(length)||length<.000001f)direction=XMVectorSet(0,-1,0,0);else direction=XMVector3Normalize(direction);
        float distance=(std::max)(.1f,selected?light.range:(std::min)(light.range,2.5f));
        auto axis=std::abs(XMVectorGetY(direction))<.95f?XMVectorSet(0,1,0,0):XMVectorSet(1,0,0,0);
        auto u=XMVector3Normalize(XMVector3Cross(direction,axis)),v=XMVector3Cross(direction,u);
        XMFLOAT3 end;XMStoreFloat3(&end,XMLoadFloat3(&light.position)+direction*distance);
        float radius=distance*tanf(XMConvertToRadians((std::clamp)(light.outerAngle,1.f,85.f)));
        c.Line(light.position,end,color,2);c.Ring(end,u,v,radius,color);
        for(int j=0;j<8;++j){float a=j*XM_2PI/8;XMFLOAT3 tip;XMStoreFloat3(&tip,XMLoadFloat3(&end)+u*(cosf(a)*radius)+v*(sinf(a)*radius));c.Line(light.position,tip,color);}
    }
    XMFLOAT3 sun{0,4,0},sunTip;auto direction=XMLoadFloat3(&renderer.GetLightDir());XMStoreFloat3(&sunTip,XMLoadFloat3(&sun)-direction*1.5f);
    const auto sunColor=IM_COL32(255,235,130,240);
    if(c.Badge(sun,"Sun (directional)",sunColor,1))showLights=true;c.Line(sun,sunTip,sunColor,2);
    // Non-rendering scene components still need a visible location in the editor.
    std::function<void(const std::shared_ptr<EngineScene::SceneNode>&)> audio=[&](const auto& node){
        if(!node||!node->IsActive())return;
        if(node->template HasComponent<EngineScene::AudioEmitterComponent>())c.Badge(node->GetTransform().GetWorldPosition(),"Audio: "+node->GetName(),IM_COL32(130,235,180,245),3);
        for(const auto& child:node->GetChildren())audio(child);
    };
    audio(EngineScene::SceneGraph::Get().GetRoot());
    c.draw->PopClipRect();
}
}
