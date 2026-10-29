#include "AI/NavAgent.h"
#include "AI/NavMesh.h"
#include <cassert>
#include <cmath>
#include <limits>
int main() {
    using namespace EngineAI;
    NavMesh mesh; NavMeshParams params;
    assert(mesh.Bake(params, {}, 0));
    NavAgent agent(1); agent.SetState(AgentState::Idle); agent.SetPosition({0,0,0});
    assert(agent.SetDestination({0,0,10},mesh));
    agent.Update(.1f,mesh,{100,0,100});
    assert(agent.GetPosition().z>0);
    assert(!agent.SetDestination({100000,0,100000},mesh));
    assert(agent.GetCurrentPath().empty());
    const auto before=agent.GetPosition();
    agent.Update(std::numeric_limits<float>::quiet_NaN(),mesh,{0,0,0});
    assert(agent.GetPosition().x==before.x && agent.GetPosition().z==before.z);
    assert(agent.GetModelId()==0);agent.SetModelId(123);assert(agent.GetModelId()==123);
}
