#include "NetworkReplication.h"

namespace EngineNetwork {

NetworkReplicationManager::NetworkReplicationManager() {}

uint32_t NetworkReplicationManager::AllocateNetworkId() {
    return m_nextNetworkId++;
}

void NetworkReplicationManager::RegisterObject(std::shared_ptr<NetworkObject> object) {
    if (!object) return;
    m_objects[object->GetNetworkId()] = object;
}

void NetworkReplicationManager::UnregisterObject(uint32_t networkId) {
    m_objects.erase(networkId);
}

std::shared_ptr<NetworkObject> NetworkReplicationManager::FindObject(uint32_t networkId) const {
    auto it = m_objects.find(networkId);
    if (it != m_objects.end()) {
        return it->second;
    }
    return nullptr;
}

void NetworkReplicationManager::Clear() {
    m_objects.clear();
}

} // namespace EngineNetwork
