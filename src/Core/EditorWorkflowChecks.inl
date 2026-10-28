// Explicit, bounded editor integration checks; never write the user's scene.
#if defined(ENGINE_EDITOR)
bool Application::RunEditorWorkflowChecks(){
    const std::string original=CaptureScene();
    auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    try{
        const size_t originalCount=m_renderer->GetModels().size();
        EngineEditor::History history;history.Reset(original);
        m_renderer->SpawnCube({80,4,80});auto& cube=m_renderer->GetModels().back();cube.name="Workflow authored cube";cube.geometryName="Cube";
        cube.physicsMass=6;cube.collider.offset={.2f,.3f,.4f};cube.gameplay.trigger=true;cube.gameplay.target="Workflow door";cube.material.roughness=.27f;
        const std::string edited=CaptureScene();history.Commit(edited);
        ApplyScene(*history.Undo());check(m_renderer->GetModels().size()==originalCount,"Undo create failed");
        ApplyScene(*history.Redo());check(m_renderer->GetModels().back().name=="Workflow authored cube"&&m_renderer->GetModels().back().physicsMass==6&&m_renderer->GetModels().back().gameplay.target=="Workflow door","Redo lost model properties");
        m_renderer->GetModels().pop_back();history.Commit(CaptureScene());ApplyScene(*history.Undo());check(m_renderer->GetModels().size()==originalCount+1,"Undo delete failed");
        auto material=EngineAsset::EncodeMaterial(m_renderer->GetModels().back().material);Renderer::PBRMaterial decoded;
        auto tessMaterial=m_renderer->GetModels().back().material;
        tessMaterial.tessellation={true,7,.8f,3,70};
        check(EngineAsset::DecodeMaterial(EngineAsset::EncodeMaterial(tessMaterial),decoded,nullptr)&&decoded.tessellation.enabled&&decoded.tessellation.factor==7&&decoded.tessellation.farDistance==70,"Tessellation material roundtrip failed");
        auto legacy=material;legacy.replace(0,7,"IDKMAT1");legacy=legacy.substr(0,legacy.find_last_of('"')+1);
        check(EngineAsset::DecodeMaterial(legacy,decoded,nullptr)&&!decoded.tessellation.enabled,"Legacy material compatibility failed");
        m_renderer->GetModels().back().material.tessellation=tessMaterial.tessellation;
        auto tessSnapshot=CaptureScene();ApplyScene(tessSnapshot);
        check(m_renderer->GetModels().back().material.tessellation.factor==7&&m_renderer->GetModels().back().material.tessellation.enabled,"Tessellation scene roundtrip failed");
        check(EngineAsset::DecodeMaterial(material,decoded,m_renderer.get())&&std::abs(decoded.roughness-.27f)<.0001f,"Material roundtrip failed");
        auto prefab=std::make_shared<EnginePrefab::Prefab>("WorkflowRegression");auto& data=prefab->GetRootData();data.name="Workflow root";data.hasMesh=true;data.meshType="Cube";data.localScale={2,1,1};
        data.properties["model_settings"]=EngineAsset::EncodeModelAsset(m_renderer->GetModels().back());
        auto path=EngineDiagnostics::Directory()/"WorkflowRegression.prefab";
        auto& system=EnginePrefab::PrefabSystem::Get();check(system.SavePrefab(prefab,path.generic_string()),"Prefab save failed");prefab=system.LoadPrefab(path.generic_string());check(bool(prefab),"Prefab reload failed");
        auto node=system.InstantiateWithRenderer(prefab->GetName(),m_renderer.get(),nullptr,{85,2,85});check(bool(node),"Prefab instantiation failed");
        auto& instance=m_renderer->GetModels().back();check(instance.prefabSource==prefab->GetName()&&instance.physicsMass==6&&std::abs(instance.collider.offset.x-.2f)<.0001f&&std::abs(instance.position.x-85)<.01f&&std::abs(instance.scale.x-2)<.01f,"Prefab settings or transforms lost");
        auto snapshot=CaptureScene();ApplyScene(snapshot);check(EngineScene::SceneGraph::Get().FindNode("Workflow root")!=nullptr,"Hierarchy lost after restore");
        auto& saves=EngineSave::SaveManager::Get();auto slot=(EngineDiagnostics::Directory()/"workflow-roundtrip.save").generic_string();
        check(saves.SaveGame(snapshot,"",slot),"Recovery save failed");EngineSave::SavePackage package;
        check(saves.LoadGame(package,slot)&&package.sceneData==snapshot,"Recovery roundtrip failed");
        ApplyScene(original);
        m_renderer->SpawnCube({70,5,70});auto& body=m_renderer->GetModels().back();body.name="Workflow falling cube";body.geometryName="Cube";body.physicsBodyType=2;body.physicsMass=2;
        auto& physics=EnginePhysics::PhysicsSystem::Get();physics.ApplyModelPhysics(m_renderer.get(),body.modelId,false);physics.SetPaused(false);
        for(int i=0;i<20;++i)physics.Update(1.f/60,m_renderer.get(),m_window.get(),m_camera.get());
        float fallen=m_renderer->GetModels().back().position.y;check(fallen<4.95f,"Rigid body did not fall");
        physics.SetPaused(true);physics.Update(1.f/60,m_renderer.get(),m_window.get(),m_camera.get());check(std::abs(m_renderer->GetModels().back().position.y-fallen)<.001f,"Pause reset physics pose");
        physics.SingleStep();physics.Update(1.f/60,m_renderer.get(),m_window.get(),m_camera.get());check(m_renderer->GetModels().back().position.y<fallen,"Single step did not advance physics");
        ApplyScene(original);physics.SetPaused(true);
        m_renderer->SpawnCube({90,2,90});m_renderer->GetModels().back().name="Workflow door";m_renderer->GetModels().back().gameplay.door=true;
        m_renderer->SpawnCube({90,2,90});auto& trigger=m_renderer->GetModels().back();trigger.gameplay.trigger=true;trigger.gameplay.target="Workflow door";
        for(int i=0;i<10;++i)m_gameplay->Update(*m_renderer,{90,2,90},.05f,false);
        check(m_renderer->GetModels()[originalCount].rotation.y>1,"Trigger did not open target door");
        m_renderer->SpawnCube({90,2,90});m_renderer->GetModels().back().gameplay.pickup=true;
        m_gameplay->Update(*m_renderer,{90,2,90},.05f,true);check(!m_renderer->GetModels().back().visible&&!m_renderer->GetModels().back().hasCollision,"Pickup did not disable rendering and collision");
        ApplyScene(original);
        auto vehicleId=physics.SpawnVehicle(m_renderer.get(),{75,2,75});
        auto vehicleModel=[&]()->Renderer::RenderModel&{for(auto& m:m_renderer->GetModels())if(m.modelId==vehicleId)return m;throw std::runtime_error("Vehicle missing");};
        check(vehicleId!=0&&m_renderer->GetModels().size()==originalCount+14,"Vehicle assembly creation failed");
        auto assembly=vehicleModel().vehicle.assembly;auto document=CaptureScene();ApplyScene(document);
        for(auto& m:m_renderer->GetModels())if(m.vehicle.assembly==assembly&&m.vehicle.part==0)vehicleId=m.modelId;
        check(vehicleModel().vehicle.enabled&&vehicleModel().physicsMass==1200,"Vehicle scene settings lost");
        auto encodedVehicle=EngineAsset::EncodeModelAsset(vehicleModel());auto copy=vehicleModel();copy.vehicle={};
        check(EngineAsset::DecodeModelAsset(encodedVehicle,copy,m_renderer.get())&&copy.vehicle.assembly==assembly,"Vehicle prefab settings lost");
        auto tick=[&](int frames){for(int i=0;i<frames;++i){physics.SimulateStep(1.f/60,m_renderer.get());physics.SyncAllActorsToScene(m_renderer.get());}physics.UpdateVehicleVisuals(m_renderer.get());};
        physics.SetVehicleInput(vehicleId,0,0,0);tick(240);
        std::cout<<"[Vehicle settle] contacts="<<physics.VehicleContacts(vehicleId)<<" position="<<vehicleModel().position.x<<","<<vehicleModel().position.y<<","<<vehicleModel().position.z<<" speed="<<physics.VehicleSpeed(vehicleId)<<" RPM="<<physics.VehicleRPM(vehicleId)<<"\n";
        check(physics.VehicleContacts(vehicleId)==4&&std::isfinite(vehicleModel().position.y)&&vehicleModel().position.y>.3f,"Vehicle suspension did not settle on four contacts");
        float startZ=vehicleModel().position.z;physics.SetVehicleInput(vehicleId,1,0,0);tick(120);
        float speed=std::abs(physics.VehicleSpeed(vehicleId));check(vehicleModel().position.z>startZ+1&&speed>1,"Vehicle did not accelerate");
        physics.SetVehicleInput(vehicleId,0,0,1);tick(180);check(std::abs(physics.VehicleSpeed(vehicleId))<speed*.5f,"Vehicle brakes did not slow the car");
        float heading=vehicleModel().rotation.y;physics.SetVehicleInput(vehicleId,1,.7f,0);tick(120);
        check(std::abs(vehicleModel().rotation.y-heading)>2,"Vehicle steering did not turn the body");
        physics.SetVehicleInput(vehicleId,0,0,1);tick(180);physics.SetVehicleInput(vehicleId,-1,0,0);tick(180);
        check(physics.VehicleSpeed(vehicleId)<-1,"Vehicle reverse did not engage");
        std::cout<<"[Vehicle] PASS: assembly, scene/prefab settings, four suspension contacts, acceleration, braking, steering and reverse\n";
        ApplyScene(original);physics.SetPaused(true);m_editHistory.Reset(CaptureScene());
        {
            auto& rain=EngineRain::RainSystem::Get();auto saved=rain.GetSettings();
            struct RestoreRain {EngineRain::RainSystem& rain;EngineRain::RainSettings saved;~RestoreRain(){rain.GetSettings()=saved;}} restore{rain,saved};
            auto& settings=rain.GetSettings();settings.enabled=false;rain.Update(1.f/60,{0,1.6f,0},{});
            settings.enabled=true;settings.intensity=1;settings.useCustomParticleCount=true;settings.particleCount=1000;
            settings.areaRadius=4;settings.areaHeight=4;settings.surface.enabled=true;settings.surface.splashesEnabled=true;
            for(int cycle=0;cycle<2;++cycle){
                int peak=0;for(int i=0;i<180;++i){rain.Update(1.f/60,{0,1.6f,0},{});peak=(std::max)(peak,rain.GetDebugStats().activeSplashes);}
                check(peak>0,"Rain impacts vanished after enable toggle");
                settings.enabled=false;rain.Update(1.f/60,{0,1.6f,0},{});
                check(rain.GetDebugStats().activeSplashes==0,"Disabled rain retained visible splashes");settings.enabled=true;
            }
            std::cout<<"[Rain] PASS: ground impacts and splash pool survive repeated disable/enable\n";
        }
        std::cout<<"[Editor workflow] PASS: create/delete undo/redo, material and prefab roundtrips, hierarchy, recovery, physics pause/step, trigger-door and pickup\n";
        return true;
    }catch(const std::exception& error){
        ApplyScene(original);EnginePhysics::PhysicsSystem::Get().SetPaused(true);
        std::cerr<<"[Editor workflow] FAIL: "<<error.what()<<'\n';return false;
    }
}
#endif
