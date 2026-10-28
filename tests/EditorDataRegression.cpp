#include "Level/LevelSystem.h"
#include "Renderer/Common/SceneLighting.h"
#include <sstream>
#include <stdexcept>
#include <iostream>
#include "Asset/AssetImport.h"
#include "PostProcess/PostProcessSerialization.h"
#include <fstream>
#include <chrono>

static void Check(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
int RunEditorDataRegression() {
    EngineLevel::LevelData source;
    EngineLevel::LevelEntity model;
    model.name="Imported scenery"; model.filepath="Content/Models/custom model.obj";
    model.physicsBodyType=1; model.physicsShapeType=4;
    model.physicsMass=85;model.physicsLinearDamping=.8f;model.physicsAngularDamping=1.2f;
    model.ragdollSettings.useProjectDefaults=false;model.ragdollSettings.mass=95;model.ragdollSettings.radius=.12f;model.ragdollSettings.jointLimit=65;model.ragdollSettings.gravity=false;
    model.collider.offset={.25f,1.5f,-2}; model.collider.size={2,.5f,3};
    model.albedoPath="Content/Textures/wall color.png";model.roughness=.73f;model.metallic=.15f;
    source.entities.push_back(model);
    EngineRender::PointLight point;point.name="Warm key light";point.position={2,3,4};point.intensity=7;point.enabled=false;
    EngineRender::SpotLight spot;spot.name="Spot \"accent\"";spot.innerAngle=12;spot.outerAngle=30;spot.direction={1,-1,0};
    source.pointLights.push_back(point);source.spotLights.push_back(spot);
    EngineRender::SceneConstantBuffer gpu{};
    EngineRender::UploadSceneLights(gpu,source.pointLights,source.spotLights);
    Check(gpu.numPointLights==0 && gpu.numSpotLights==1 && gpu.spotLights[0].intensity==spot.intensity,"local lights not uploaded or disabled light leaked");
    auto enabled=point;enabled.enabled=true;
    EngineRender::UploadSceneLights(gpu,{enabled},{});
    Check(gpu.numPointLights==1 && gpu.pointLights[0].intensity==point.intensity && gpu.numSpotLights==0 && gpu.spotLights[0].intensity==0,"stale or missing GPU lights");
    auto zeroDirection=spot;zeroDirection.direction={0,0,0};
    EngineRender::UploadSceneLights(gpu,std::vector<EngineRender::PointLight>(20,enabled),{zeroDirection});
    Check(gpu.numPointLights==EngineRender::MAX_POINT_LIGHTS && std::isfinite(gpu.spotLights[0].direction.y) && gpu.spotLights[0].direction.y==-1,"light limit or direction fallback broken");
    EngineLevel::LevelData loaded;
    Check(loaded.Deserialize(source.Serialize()),"level deserialize");
    Check(loaded.entities.size()==1,"entity count");
    const auto& m=loaded.entities[0];
    Check(m.collider.offset.y==1.5f && m.collider.size.z==3,"collider settings lost in level save");
    Check(m.physicsShapeType==4 && m.physicsBodyType==1,"physics type lost");
    Check(m.physicsMass==85 && m.physicsLinearDamping==.8f && m.physicsAngularDamping==1.2f,"physics mass or damping lost");
    Check(!m.ragdollSettings.useProjectDefaults && m.ragdollSettings.mass==95 && m.ragdollSettings.radius==.12f && m.ragdollSettings.jointLimit==65 && !m.ragdollSettings.gravity,"ragdoll overrides lost");
    Check(m.albedoPath==model.albedoPath && m.roughness==model.roughness,"material lost");
    Check(loaded.pointLights.size()==1 && loaded.spotLights.size()==1,"level lights lost");
    std::ostringstream encoded;
    EngineRender::WriteSceneLights(encoded,source.pointLights,source.spotLights);
    std::vector<EngineRender::PointLight> points;std::vector<EngineRender::SpotLight> spots;
    std::istringstream stream(encoded.str());std::string line,tag;
    while(std::getline(stream,line)) { std::istringstream in(line);in>>tag;Check(EngineRender::ReadSceneLight(tag,in,points,spots),"light tag"); }
    Check(points.size()==1 && spots.size()==1,"scene lights missing");
    Check(points[0].name==point.name && !points[0].enabled && points[0].intensity==7,"point properties lost");
    Check(spots[0].name==spot.name && spots[0].innerAngle==12 && spots[0].direction.x==1,"spot properties lost");
    EnginePostProcess::PostProcessSettings post;post.exposure=2.2f;post.tonemapper=EnginePostProcess::TonemapOperator::Neutral;post.bloomEnabled=false;
    EnginePostProcess::PostProcessVolume volume;volume.name="Warm room";volume.center={1,2,3};volume.settings.colorFilter={1,.2f,.5f};
    std::ostringstream postText;EnginePostProcess::WritePostProcessState(postText,post,{volume});
    EnginePostProcess::PostProcessSettings restoredPost;std::vector<EnginePostProcess::PostProcessVolume> restoredVolumes;
    std::istringstream postStream(postText.str());
    while(std::getline(postStream,line)) { std::istringstream in(line);in>>tag;Check(EnginePostProcess::ReadPostProcessState(tag,in,restoredPost,restoredVolumes),"post-process tag"); }
    Check(restoredPost.exposure==2.2f && !restoredPost.bloomEnabled && restoredPost.tonemapper==post.tonemapper,"post-process global settings lost");
    Check(restoredVolumes.size()==1 && restoredVolumes[0].name==volume.name && restoredVolumes[0].settings.colorFilter.y==.2f,"post-process volume lost");
    EngineLevel::LevelData legacy;
    Check(legacy.Deserialize("FORMAT Idontknow.Level\nENTITY_START\nNAME Cube\nENTITY_END\n"),"legacy level rejected");
    Check(legacy.entities[0].collider.size.x==1 && legacy.entities[0].collider.offset.x==0,"legacy collider defaults");
    const auto temporary=std::filesystem::temp_directory_path();
    const auto directory=temporary/("Idontknow-editor-import-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    { std::ofstream texture(directory/"wall color.png",std::ios::binary);texture<<"test texture bytes"; }
    auto imported=EngineAsset::ImportTextureAsset(directory/"wall color.png",directory/"Textures");
    auto second=EngineAsset::ImportTextureAsset(directory/"wall color.png",directory/"Textures");
    Check(imported=="Content/Textures/wall color.png" && second=="Content/Textures/wall color_1.png","import overwrote an existing asset");
    Check(std::filesystem::file_size(directory/"Textures"/"wall color.png")==18,"imported bytes changed");
    Check(std::filesystem::equivalent(directory.parent_path(),temporary),"invalid cleanup target");
    std::filesystem::remove_all(directory);
    std::cout<<"Editor data passed: collider, material, level and scene lighting roundtrip\n";
    return 0;
}
int main() {
    try { return RunEditorDataRegression(); }
    catch(const std::exception& error) { std::cerr<<"FAIL: "<<error.what()<<'\n'; return 1; }
}
