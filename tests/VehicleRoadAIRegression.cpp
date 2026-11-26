#include "AI/VehicleRoadAI.h"
#include <cassert>
#include <sstream>
int main() {
    using namespace EnginePhysics;
    using namespace EngineAI;
    VehicleSettings settings; settings.aiEnabled=true;
    RoadWaypoint first; first.z=20; first.speedLimit=8; settings.route.push_back(first);
    VehicleRoadState state;
    auto go=DriveRoad(settings,state,0,0,0,0,100,0.1f);
    assert(go.throttle>0 && go.brake==0);
    auto obstacle=DriveRoad(settings,state,0,0,0,6,2,0.1f);
    assert(obstacle.brake>0 && obstacle.throttle==0);
    settings.route[0].redSignal=true;
    auto red=DriveRoad(settings,state,0,19,0,0,100,0.1f);
    assert(red.brake==1 && state.waypoint==0);
    settings.route[0].redSignal=false; settings.route[0].stop=true;
    for(int i=0;i<19;++i) {auto stop=DriveRoad(settings,state,0,19,0,0,100,0.1f); assert(stop.brake==1 && !state.complete);}
    DriveRoad(settings,state,0,19,0,0,100,0.2f); assert(state.complete);
    settings.route[0].stop=false;
    settings.route[0].crossingRadius=4.f;
    state={};
    auto yield=DriveRoad(settings,state,0,15,0,4,100,0.1f,[](const RoadWaypoint&) {return true;});
    assert(yield.brake==1 && !state.complete);
    auto clear=DriveRoad(settings,state,0,15,0,0,100,0.1f,[](const RoadWaypoint&) {return false;});
    assert(clear.throttle>0 && clear.brake==0);
    settings.route[0].stop=true;
    std::stringstream stream; WriteVehicle(stream,settings);
    VehicleSettings restored; std::string line;
    while(std::getline(stream,line)) {std::istringstream in(line); std::string tag;in>>tag;ReadVehicle(tag,in,restored);}
    assert(restored.aiEnabled && restored.route.size()==1 && restored.route[0].stop && restored.route[0].speedLimit==8);
    assert(restored.route[0].crossingRadius==4.f);
    std::istringstream legacy("0 0 15 8 1 0");
    assert(ReadVehicle("VEHICLE_WAYPOINT",legacy,restored) && restored.route.back().crossingRadius==0);
    std::istringstream legacyRoute("0 0 15 8 1 0\nVEHICLE_WAYPOINT 0 0 25 8 0 0\n");
    assert(ReadVehicle("VEHICLE_WAYPOINT",legacyRoute,restored));
    std::string nextTag;
    assert(legacyRoute >> nextTag && nextTag=="VEHICLE_WAYPOINT");
    assert(ReadVehicle(nextTag,legacyRoute,restored) && restored.route.back().z==25);
}
