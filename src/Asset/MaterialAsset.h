#pragma once
#include "Renderer/Renderer.h"
#include <sstream>
#include <iomanip>
#include <fstream>
namespace EngineAsset {
inline std::string EncodeMaterial(const Renderer::PBRMaterial& m){
    std::ostringstream out;out.precision(9);
    out<<"IDKMAT2 "<<m.albedo.x<<' '<<m.albedo.y<<' '<<m.albedo.z<<' '<<m.roughness<<' '<<m.metallic<<' '<<m.reflectance<<' '<<m.emission<<' '<<m.ior<<' '<<m.uvTiling.x<<' '<<m.uvTiling.y<<' '
       <<std::quoted(m.albedoPath)<<' '<<std::quoted(m.normalPath)<<' '<<std::quoted(m.rmaPath)<<' '<<std::quoted(m.aoPath)<<' '<<std::quoted(m.emissivePath);
    out<<' ';EngineRender::WriteTessellation(out,m.tessellation);
    return out.str();
}
inline bool DecodeMaterial(const std::string& text,Renderer::PBRMaterial& material,Renderer* renderer){
    std::istringstream in(text);std::string magic;auto m=material;
    if(!(in>>magic)||(magic!="IDKMAT1"&&magic!="IDKMAT2"))return false;
    if(!(in>>m.albedo.x>>m.albedo.y>>m.albedo.z>>m.roughness>>m.metallic>>m.reflectance>>m.emission>>m.ior>>m.uvTiling.x>>m.uvTiling.y
        >>std::quoted(m.albedoPath)>>std::quoted(m.normalPath)>>std::quoted(m.rmaPath)>>std::quoted(m.aoPath)>>std::quoted(m.emissivePath)))return false;
    m.tessellation={};if(magic=="IDKMAT2"&&!EngineRender::ReadTessellation(in,m.tessellation))return false;
    for(float value:{m.albedo.x,m.albedo.y,m.albedo.z,m.roughness,m.metallic,m.reflectance,m.emission,m.ior,m.uvTiling.x,m.uvTiling.y})if(!std::isfinite(value))return false;
    m.roughness=(std::clamp)(m.roughness,.02f,1.f);m.metallic=(std::clamp)(m.metallic,0.f,1.f);m.preset=Renderer::MaterialPreset::Custom;
    if(renderer){
        auto load=[&](const std::string& p){return p.empty()?std::shared_ptr<Texture>{}:renderer->LoadTexture(p);};
        m.albedoMap=load(m.albedoPath);m.normalMap=load(m.normalPath);m.rmaMap=load(m.rmaPath);m.aoMap=load(m.aoPath);m.emissiveMap=load(m.emissivePath);
    }
    material=std::move(m);return true;
}
template<class Model> std::string EncodeModelAsset(const Model& m){
    std::ostringstream out;out.precision(9);
    out<<std::quoted(EncodeMaterial(m.material))<<' '<<m.hasCollision<<' '<<m.physicsBodyType<<' '<<m.physicsShapeType<<' '<<m.physicsMass<<' '<<m.physicsFriction<<' '<<m.physicsRestitution<<' '
       <<m.physicsLinearDamping<<' '<<m.physicsAngularDamping<<' '<<m.physicsEnableGravity<<' '<<m.physicsEnableCCD<<' '
       <<m.collider.offset.x<<' '<<m.collider.offset.y<<' '<<m.collider.offset.z<<' '<<m.collider.size.x<<' '<<m.collider.size.y<<' '<<m.collider.size.z<<' ';
    std::ostringstream gameplay;EngineScene::WriteGameplay(gameplay,m.gameplay);auto text=gameplay.str();text.erase(std::remove(text.begin(),text.end(),'\n'),text.end());out<<std::quoted(text);
    std::ostringstream vehicle;EnginePhysics::WriteVehicle(vehicle,m.vehicle);auto vehicleText=vehicle.str();std::replace(vehicleText.begin(),vehicleText.end(),'\n',' ');out<<' '<<std::quoted(vehicleText);
    return out.str();
}
template<class Model> bool DecodeModelAsset(const std::string& text,Model& m,Renderer* renderer){
    std::istringstream in(text);std::string material,gameplay;
    if(!(in>>std::quoted(material)))return false;
    if(!(in>>m.hasCollision>>m.physicsBodyType>>m.physicsShapeType>>m.physicsMass>>m.physicsFriction>>m.physicsRestitution>>m.physicsLinearDamping>>m.physicsAngularDamping
        >>m.physicsEnableGravity>>m.physicsEnableCCD>>m.collider.offset.x>>m.collider.offset.y>>m.collider.offset.z>>m.collider.size.x>>m.collider.size.y>>m.collider.size.z>>std::quoted(gameplay)))return false;
    if(!DecodeMaterial(material,m.material,renderer))return false;
    std::istringstream settings(gameplay);std::string tag;settings>>tag;if(!EngineScene::ReadGameplay(settings,m.gameplay))return false;
    in>>std::ws;if(in.eof()){m.vehicle={};return true;}
    std::string vehicle;if(!(in>>std::quoted(vehicle)))return false;
    std::istringstream data(vehicle);while(data>>tag)if(!EnginePhysics::ReadVehicle(tag,data,m.vehicle))return false;return true;
}
}
