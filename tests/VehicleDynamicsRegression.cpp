#include "Physics/VehicleSimulation.h"
#include <fstream>
#include <stdexcept>
#include <sstream>
#include <limits>
using namespace physx;
using namespace EnginePhysics;
static void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Motion {
    float throttle=0,brake=0,steer=0,speed=0,spin=0,lengths[4]{};
    int contacts=0,manualGear=1;bool handbrake=false;
    std::shared_ptr<VehicleDynamics> dynamics;
};
struct World {
    PxDefaultAllocator allocator;PxDefaultErrorCallback errors;
    PxFoundation* foundation=nullptr;PxPhysics* physics=nullptr;PxScene* scene=nullptr;
    PxDefaultCpuDispatcher* dispatcher=nullptr;PxMaterial* material=nullptr;PxRigidStatic* floor=nullptr;
    World(){
        foundation=PxCreateFoundation(PX_PHYSICS_VERSION,allocator,errors);Check(foundation,"Foundation failed");
        physics=PxCreatePhysics(PX_PHYSICS_VERSION,*foundation,PxTolerancesScale(),false,nullptr);Check(physics,"Physics failed");
        Check(PxInitExtensions(*physics,nullptr)&&PxInitVehicleExtension(*foundation),"Extensions failed");
        dispatcher=PxDefaultCpuDispatcherCreate(1);PxSceneDesc desc(physics->getTolerancesScale());
        desc.gravity=PxVec3(0,-9.81f,0);desc.cpuDispatcher=dispatcher;desc.filterShader=PxDefaultSimulationFilterShader;desc.flags|=PxSceneFlag::eENABLE_CCD;
        scene=physics->createScene(desc);Check(scene,"Scene failed");material=physics->createMaterial(.5f,.5f,0);
        floor=PxCreateStatic(*physics,PxTransform(PxVec3(0,-1,0)),PxBoxGeometry(300,1,300),*material);scene->addActor(*floor);
    }
    ~World(){floor->release();scene->release();dispatcher->release();material->release();PxCloseVehicleExtension();PxCloseExtensions();physics->release();foundation->release();}
};
struct Car {
    World& world;VehicleSettings settings;Motion motion;PxRigidDynamic* body;
    Car(World& w,float y=2):world(w){
        settings.enabled=true;settings.assembly="regression";
        body=PxCreateDynamic(*w.physics,PxTransform(PxVec3(0,y,0)),PxBoxGeometry(.85f,.21f,1.95f),*w.material,1000);
        Check(body,"Car failed");PxRigidBodyExt::setMassAndUpdateInertia(*body,1200);body->setAngularDamping(.05f);
        body->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD,true);world.scene->addActor(*body);
    }
    ~Car(){motion.dynamics.reset();body->release();}
    void Tick(float dt=1.f/60){
        AdvanceVehicleScene(*world.scene,dt,[&](float h){SimulateVehicle(*world.physics,*world.scene,*world.material,*body,settings,PxVec3(1.7f,.42f,3.9f),motion,h);});
        Check(body->getGlobalPose().isFinite()&&body->getLinearVelocity().isFinite(),"Vehicle became nonfinite");
    }
    void Settle(float dt=1.f/60){for(int i=0;i<int(std::lround(4/dt));++i)Tick(dt);Check(motion.contacts==4,"Suspension lost contacts");Check(body->getGlobalPose().p.y>.5f&&body->getGlobalPose().p.y<.9f,"Suspension rest height invalid");}
};
struct Measurement{float time100=-1,stopDistance=0,distance5=0;int maxGear=0;};
static Measurement Measure(World& w,float dt,float wetness){
    Car car(w);car.settings.wetness=wetness;car.Settle(dt);Measurement m;car.motion.throttle=1;
    for(int i=0;i<int(20/dt);++i){
        car.Tick(dt);m.maxGear=(std::max)(m.maxGear,int(car.motion.dynamics->gearbox.currentGear)-1);
        if(i==int(std::lround(5/dt))-1)m.distance5=car.body->getGlobalPose().p.z;
        if(car.motion.speed>=100/3.6f){m.time100=(i+1)*dt;break;}
    }
    Check(m.time100>3&&m.time100<20,"0-100 acceleration outside plausible baseline range");
    Check(m.maxGear>=2,"Automatic gearbox never shifted");
    float start=car.body->getGlobalPose().p.z;car.motion.throttle=0;car.motion.brake=1;
    bool stopped=false;for(int i=0;i<int(15/dt);++i){car.Tick(dt);if(std::abs(car.motion.speed)<.1f){stopped=true;break;}}
    Check(stopped,"Brakes failed to stop car");m.stopDistance=car.body->getGlobalPose().p.z-start;
    Check(m.stopDistance>20&&m.stopDistance<160,"Stopping distance outside baseline bounds");return m;
}
int main(int argc,char** argv){try{
    World world;
    {
        VehicleSettings s;VehicleDynamics d;d.Configure(s);
        Check(d.engineParams.isValid()&&d.gearboxParams.isValid()&&d.differentialParams.isValid(d.axles),"Native configuration invalid");
        d.gearbox.currentGear=d.gearbox.targetGear=1;d.throttle=1;
        for(int i=0;i<1200;++i)d.Drive(s,0,1.f/600);
        Check(d.engine.rotationSpeed>d.engineParams.idleOmega*2&&d.engine.rotationSpeed<=d.engineParams.maxOmega+.01f,"Neutral free-rev/redline failed");
        for(auto& wheel:d.wheelStates)Check(std::abs(wheel.rotationSpeed)<.001f,"Neutral drives wheels");
        Check(VehicleAckermann(.4f,1,s)>VehicleAckermann(.4f,0,s)&&VehicleAckermann(-.4f,0,s)<VehicleAckermann(-.4f,1,s),"Ackermann inside wheel invalid");
        s.peakTorque=430;s.frontDrive=0;s.abs=false;s.weatherGrip=false;std::stringstream data;WriteVehicle(data,s);VehicleSettings restored;std::string line;
        while(std::getline(data,line)){std::istringstream in(line);std::string tag;in>>tag;Check(ReadVehicle(tag,in,restored),"Settings tag rejected");}
        Check(restored.peakTorque==430&&restored.frontDrive==0&&!restored.abs&&!restored.weatherGrip,"Dynamics roundtrip lost parameters");
        s.radius=std::numeric_limits<float>::quiet_NaN();s.Sanitize();Check(std::isfinite(s.radius),"Sanitization failed");
    }
    {
        auto body=PxCreateDynamic(*world.physics,PxTransform(PxVec3(20,20,0)),PxBoxGeometry(.2f,.2f,.2f),*world.material,1000);
        PxRigidBodyExt::setMassAndUpdateInertia(*body,10);body->setLinearDamping(0);world.scene->addActor(*body);
        body->addForce(PxVec3(120,0,0));
        AdvanceVehicleScene(*world.scene,1.f/60,[](float){});
        Check(std::abs(body->getLinearVelocity().x-.2f)<.0001f,"Scene substeps changed external force duration");
        Check(!(body->getRigidBodyFlags()&PxRigidBodyFlag::eRETAIN_ACCELERATIONS),"Scene substeps leaked retain flag");
        AdvanceVehicleScene(*world.scene,1.f/60,[](float){});
        Check(std::abs(body->getLinearVelocity().x-.2f)<.0001f,"Scene substeps leaked external force");
        body->setRigidBodyFlag(PxRigidBodyFlag::eRETAIN_ACCELERATIONS,true);body->addForce(PxVec3(120,0,0));
        AdvanceVehicleScene(*world.scene,1.f/60,[](float){});
        AdvanceVehicleScene(*world.scene,1.f/60,[](float){});
        Check((body->getRigidBodyFlags()&PxRigidBodyFlag::eRETAIN_ACCELERATIONS)&&std::abs(body->getLinearVelocity().x-.6f)<.0001f,"Existing retained forces were cleared");
        body->release();
    }
    {
        Car car(world,.6f);car.body->setGlobalPose(PxTransform(PxVec3(75,.6f,75)));car.Settle();
        Check(std::abs(car.body->getGlobalPose().p.x-75)<.1f&&std::abs(car.body->getGlobalPose().p.z-75)<.1f,"Overlapping wheel sweep destabilized chassis");
    }
    for(float yaw:{0.f,PxHalfPi}){
        Car car(world);car.body->setGlobalPose(PxTransform(PxVec3(0,2,0),PxQuat(yaw,PxVec3(0,1,0))));car.Settle();
        auto start=car.body->getGlobalPose().p;auto forward=car.body->getGlobalPose().q.rotate(PxVec3(0,0,1));
        car.motion.throttle=1;for(int i=0;i<360;++i)car.Tick();
        auto pose=car.body->getGlobalPose();auto heading=pose.q.rotate(PxVec3(0,0,1));
        auto side=PxVec3(0,1,0).cross(forward);float drift=(pose.p-start).dot(side);
        std::cout<<"straight yaw="<<yaw<<" lateral="<<drift<<" distance="<<(pose.p-start).dot(forward)<<"\n";
        Check(std::abs(drift)<.05f&&std::abs(heading.dot(side))<.001f,"Zero steering generates lateral drift");
    }
    auto dry=Measure(world,1.f/60,0);auto wet=Measure(world,1.f/60,1);auto faster=Measure(world,1.f/120,0);
    std::cout<<"metrics dry "<<dry.time100<<","<<dry.stopDistance<<","<<dry.distance5<<" wet "<<wet.time100<<","<<wet.stopDistance<<","<<wet.distance5<<" 120Hz "<<faster.time100<<","<<faster.stopDistance<<","<<faster.distance5<<"\n";
    Check(wet.stopDistance>dry.stopDistance*1.1f,"Wet surface did not reduce braking grip");
    Check(std::abs(faster.distance5-dry.distance5)/(std::max)(1.f,dry.distance5)<.2f,"60/120Hz trajectory mismatch");
    {
        Car car(world,8);car.motion.throttle=1;for(int i=0;i<30;++i)car.Tick();
        Check(car.motion.contacts==0&&std::abs(car.body->getLinearVelocity().z)<.01f,"Engine propels airborne chassis");
        Check(std::abs(car.motion.dynamics->wheelStates[0].rotationSpeed)>1,"Airborne driven wheels do not spin");
    }
    {
        Car car(world);car.Settle();auto y=car.body->getGlobalPose().p.y;
        auto curb=PxCreateStatic(*world.physics,PxTransform(PxVec3(0,.06f,10)),PxBoxGeometry(4,.06f,.25f),*world.material);world.scene->addActor(*curb);
        car.motion.throttle=.5f;float highest=y;
        for(int i=0;i<600;++i){car.Tick();if(car.body->getGlobalPose().p.z>7)highest=(std::max)(highest,car.body->getGlobalPose().p.y);}
        curb->release();Check(car.body->getGlobalPose().p.z>12,"Car failed to cross low curb");Check(highest>y+.025f&&highest<y+.7f,"Curb response invalid");
    }
    std::ostream* output=&std::cout;std::ofstream csv;if(argc>1){csv.open(argv[1]);Check(bool(csv),"Cannot write measurements");output=&csv;}
    *output<<"surface,hz,time_0_100_s,stop_100_0_m,distance_5s_m,max_gear\n";
    *output<<"dry,60,"<<dry.time100<<','<<dry.stopDistance<<','<<dry.distance5<<','<<dry.maxGear<<"\nwet,60,"<<wet.time100<<','<<wet.stopDistance<<','<<wet.distance5<<','<<wet.maxGear<<"\ndry,120,"<<faster.time100<<','<<faster.stopDistance<<','<<faster.distance5<<','<<faster.maxGear<<'\n';
    std::cout<<"PASS: native drivetrain/tires, neutral, gearing, versioned settings, contacts, dry/wet braking, timestep comparison, airborne wheels and convex curb sweep\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}

