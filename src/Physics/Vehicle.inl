// Arcade four-wheel raycast vehicle on a real PhysX rigid body.
// Units: metres, kilograms, seconds and newtons; +Z is forward.
uint32_t PhysicsSystem::SpawnVehicle(Renderer* renderer,const DirectX::XMFLOAT3& position){
    if(!renderer)return 0;
    const auto assembly=EngineProject::NewShaderAppId();
    static unsigned counter=1;
    std::string name;
    do{name="Car "+std::to_string(counter++);}while(std::any_of(renderer->GetModels().begin(),renderer->GetModels().end(),[&](const auto& m){return m.name==name;}));
    renderer->SpawnCube(position);auto& chassis=renderer->GetModels().back();
    chassis.name=name;chassis.geometryName="Cube";chassis.vehicle.enabled=true;chassis.vehicle.assembly=assembly;
    chassis.initialPosition=position;chassis.scale={1.7f,.42f,3.9f};chassis.physicsBodyType=2;chassis.physicsMass=1200;
    chassis.physicsRestitution=.02f;chassis.physicsAngularDamping=.7f;chassis.physicsLinearDamping=.01f;
    chassis.material.albedo={.65f,.025f,.02f};chassis.material.metallic=.3f;chassis.material.roughness=.3f;
    const auto id=chassis.modelId;
    for(int part=1;part<=13;++part){
        if(part>=2&&part<=5)renderer->SpawnSphere(position);else renderer->SpawnCube(position);
        auto& visual=renderer->GetModels().back();visual.geometryName=(part>=2&&part<=5)?"Sphere":"Cube";
        visual.name=name+" / "+(part==1?"Cabin":part<=5?"Wheel ":part<=9?"Lamp ":"Spoke ")+std::to_string(part);
        visual.hasCollision=false;visual.vehicle.assembly=assembly;visual.vehicle.part=part;
        visual.material.albedo=part==1?DirectX::XMFLOAT3(.08f,.18f,.25f):part<=5?DirectX::XMFLOAT3(.018f,.018f,.018f):part<=7?DirectX::XMFLOAT3(1,.9f,.65f):part<=9?DirectX::XMFLOAT3(.8f,.015f,.01f):DirectX::XMFLOAT3(.7f,.7f,.7f);
        visual.material.roughness=part<=5?.7f:.3f;visual.material.emission=(part>=6&&part<=9)?2.f:0.f;
    }
    SyncAllActorsFromScene(renderer);m_vehicles[id]=VehicleState{};UpdateVehicleVisuals(renderer);
    auto& graph=EngineScene::SceneGraph::Get();auto rootNode=graph.CreateNode(name);rootNode->AddComponent<EngineScene::MeshComponent>(id,false,name);
    graph.SyncNodeFromModel(renderer,id);graph.Update(0);
    for(const auto& part:renderer->GetModels())if(part.vehicle.assembly==assembly&&part.vehicle.part){
        auto node=graph.CreateNode(part.name,rootNode);node->AddComponent<EngineScene::MeshComponent>(part.modelId,false,part.name);graph.SyncNodeFromModel(renderer,part.modelId);
    }
    graph.Update(0);renderer->InvalidateRaytracingScene();return id;
}
void PhysicsSystem::SetVehicleInput(uint32_t id,float throttle,float steer,float brake,bool handbrake){
    auto& state=m_vehicles[id];state.throttle=std::isfinite(throttle)?(std::clamp)(throttle,-1.f,1.f):0;
    state.steer=std::isfinite(steer)?(std::clamp)(steer,-1.f,1.f):0;state.brake=std::isfinite(brake)?(std::clamp)(brake,0.f,1.f):0;state.handbrake=handbrake;
}
float PhysicsSystem::VehicleRPM(uint32_t id)const{auto i=m_vehicles.find(id);return i==m_vehicles.end()||!i->second.dynamics?0:i->second.dynamics->engine.rotationSpeed*30/physx::PxPi;}
int PhysicsSystem::VehicleGear(uint32_t id)const{auto i=m_vehicles.find(id);return i==m_vehicles.end()||!i->second.dynamics?0:int(i->second.dynamics->gearbox.currentGear)-1;}
float PhysicsSystem::VehicleSlip(uint32_t id)const{auto i=m_vehicles.find(id);if(i==m_vehicles.end()||!i->second.dynamics)return 0;float slip=0;for(float s:i->second.dynamics->slips)slip=(std::max)(slip,std::abs(s));return slip;}
void PhysicsSystem::SimulateStep(float dt,Renderer* renderer){
    if(!m_scene||!std::isfinite(dt)||dt<=0)return;
    if(m_vehicles.empty()){m_scene->simulate(dt);m_scene->fetchResults(true);return;}
    AdvanceVehicleScene(*m_scene,dt,[&](float h){StepVehicles(h,renderer);});
}
void PhysicsSystem::StepVehicles(float dt,Renderer* renderer){
    using namespace physx;
    auto step=[&](auto& models){for(auto& model:models){
        if(!model.vehicle.enabled||model.vehicle.part!=0||!model.visible||!model.hasCollision||model.physicsBodyType!=2)continue;
        auto actor=m_actors.find(model.modelId);if(actor==m_actors.end())continue;
        auto body=actor->second.actor->template is<PxRigidDynamic>();if(!body||(body->getRigidBodyFlags()&PxRigidBodyFlag::eKINEMATIC))continue;
        auto& state=m_vehicles[model.modelId];
        auto settings=model.vehicle;
        if(settings.weatherGrip)settings.wetness=(std::max)(settings.wetness,EngineRain::RainSystem::Get().GetDebugStats().currentWetness);
        SimulateVehicle(*m_physics,*m_scene,*m_defaultMaterial,*body,settings,XM3ToPx(model.scale),state,dt);
    }};
    step(renderer->GetModels());step(renderer->GetDynamicModels());
}
void PhysicsSystem::UpdateVehicleVisuals(Renderer* renderer){
    using namespace physx;
    bool changed=false;
    auto visuals=[&](const auto& root){
        const auto& v=root.vehicle;if(v.part||v.assembly.empty())return;
        auto pose=PxTransform(XM3ToPx(root.position),EulerToPxQuat(root.rotation));
        auto [entry,created]=m_vehicles.try_emplace(root.modelId);auto& state=entry->second;
        if(created)for(const auto& part:renderer->GetModels())if(part.vehicle.assembly==v.assembly&&part.vehicle.part>=2&&part.vehicle.part<=5)
            state.lengths[part.vehicle.part-2]=(std::clamp)(-pose.transformInv(XM3ToPx(part.position)).y,v.restLength-v.travel,v.restLength+v.travel);
        auto& graph=EngineScene::SceneGraph::Get();graph.SyncNodeFromModel(renderer,root.modelId);graph.Update(0);
        for(auto& part:renderer->GetModels()){
            if(part.vehicle.assembly!=v.assembly||part.vehicle.part==0)continue;
            int role=part.vehicle.part;PxVec3 offset;PxQuat rotation(PxIdentity);
            part.visible=root.visible;
            if(role==1){offset=PxVec3(0,.52f,-.25f);part.scale={1.45f,.65f,1.8f};}
            else if((role>=2&&role<=5)||role>=10){
                int wheel=role>=10?role-10:role-2;float length=state.lengths[wheel];

                offset=PxVec3((wheel&1?1.f:-1.f)*v.track*.5f,-length,(wheel<2?1.f:-1.f)*v.wheelbase*.5f);
                float steer=wheel<2?state.steer*v.steerDegrees*PxPi/180.f/(1.f+PxAbs(state.speed)*.045f):0;
                rotation=PxQuat(steer,PxVec3(0,1,0))*PxQuat(state.spin,PxVec3(1,0,0));
                part.scale=role>=10?DirectX::XMFLOAT3(.32f,.055f,v.radius*1.5f):DirectX::XMFLOAT3(.28f,v.radius*2,v.radius*2);
            }else{bool front=role<=7;offset=PxVec3((role&1?1.f:-1.f)*.55f,.04f,front?1.96f:-1.96f);part.scale={.32f,.16f,.06f};}
            auto world=pose*PxTransform(offset,rotation);part.position=PxToXM3(world.p);part.rotation=QuatToEuler(world.q);
            auto oldWorld=part.worldMatrix;
            DirectX::XMStoreFloat4x4(&part.worldMatrix,PxTransformToMatrix(world,part.scale));
            changed|=std::memcmp(&oldWorld,&part.worldMatrix,sizeof(oldWorld))!=0;
            graph.SyncNodeFromModel(renderer,part.modelId);
        }
    };
    for(const auto& root:renderer->GetModels())if(root.vehicle.part==0)visuals(root);
    for(const auto& root:renderer->GetDynamicModels())visuals(root);
    EngineScene::SceneGraph::Get().Update(0);
    if(changed)renderer->InvalidateRaytracingScene(false);
}
void PhysicsSystem::UpdateVehicleInput(Renderer* renderer,Window* window,Camera* camera){
    if(!window||!camera)return;
    bool active=!m_isPaused&&!camera->IsFreecamMode()&&!window->IsCursorEnabled();
    bool pressed=window->IsKeyDown('V');
    if(!active){m_drivenVehicle=0;}else if(pressed&&!m_vehicleKey){
        if(m_drivenVehicle){
            for(const auto& m:renderer->GetModels())if(m.modelId==m_drivenVehicle){auto p=physx::PxTransform(XM3ToPx(m.position),EulerToPxQuat(m.rotation)).transform(physx::PxVec3(2.5f,1,0));CharacterController::Get().SetPosition(p.x,p.y,p.z);camera->SetControllerPose(CharacterController::Get().GetEyePosition(),false);}
            m_drivenVehicle=0;
        }else{
            float best=36;auto p=XM3ToPx(camera->GetPosition());
            for(const auto& m:renderer->GetModels())if(m.vehicle.enabled&&m.visible&&m.hasCollision&&m.physicsBodyType==2){float d=(XM3ToPx(m.position)-p).magnitudeSquared();if(d<best){best=d;m_drivenVehicle=m.modelId;}}
        }
    }
    m_vehicleKey=pressed;
    for(auto& pair:m_vehicles){if(pair.first!=m_drivenVehicle||!active)SetVehicleInput(pair.first,0,0,0);}
    if(m_drivenVehicle)SetVehicleInput(m_drivenVehicle,float(window->IsKeyDown('W'))-float(window->IsKeyDown('S')),float(window->IsKeyDown('D'))-float(window->IsKeyDown('A')),0,window->IsKeyDown(VK_SPACE));
}
void PhysicsSystem::FollowVehicle(Camera* camera){
    if(!m_drivenVehicle||!camera)return;
    auto actor=m_actors.find(m_drivenVehicle);if(actor==m_actors.end()){m_drivenVehicle=0;return;}
    auto pose=actor->second.actor->getGlobalPose();auto forward=pose.q.rotate(physx::PxVec3(0,0,1));forward.y=0;
    if(forward.normalize()<.001f)forward=physx::PxVec3(0,0,1);
    auto position=pose.p-forward*6.f+physx::PxVec3(0,2.8f,0);
    camera->SetPosition(position.x,position.y,position.z);camera->SetYaw(std::atan2(forward.x,forward.z));camera->SetPitch(-.28f);
}
