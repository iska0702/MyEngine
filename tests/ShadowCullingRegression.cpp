#include "../src/Renderer/ShadowCulling.h"
#include <cassert>
#include <limits>
int main() {
    using namespace DirectX;
    const EngineRender::ShadowFrustum f(XMMatrixIdentity());
    assert(f.Intersects({0,0,.5f}, .1f));
    assert(!f.Intersects({2,0,.5f}, .1f));
    assert(!f.Intersects({0,0,-1}, .1f));
    assert(!f.Intersects({0,0,2}, .1f));
    assert(f.Intersects({1.1f,0,.5f}, .1f));
    assert(f.Intersects({100,0,.5f}, -1));
    assert(f.Intersects({std::numeric_limits<float>::quiet_NaN(),0,.5f}, .1f));
    const auto projection=XMMatrixPerspectiveFovLH(XM_PIDIV2,1,.1f,10.f);
    const EngineRender::ShadowFrustum perspective(projection);
    assert(perspective.Intersects({0,0,2},.1f));
    assert(!perspective.Intersects({10,0,2},.1f));
    assert(!perspective.Intersects({0,0,-2},.1f));
}
