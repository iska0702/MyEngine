#pragma once

#include "NetworkTypes.h"
#include "NetworkObject.h"
#include <unordered_map>
#include <memory>
#include <vector>

namespace EngineNetwork {

class NetworkReplicationManager {
public:
    NetworkReplicationManager();

    uint32_t AllocateNetworkId();

    void RegisterObject(std::shared_ptr<NetworkObject> object);
    void UnregisterObject(uint32_t networkId);
    std::shared_ptr<NetworkObject> FindObject(uint32_t networkId) const;

    const std::unordered_map<uint32_t, std::shared_ptr<NetworkObject>>& GetAllObjects() const { return m_objects; }

    void Clear();

private:
    uint32_t m_nextNetworkId = 1;
    std::unordered_map<uint32_t, std::shared_ptr<NetworkObject>> m_objects;
};

} // namespace EngineNetwork
