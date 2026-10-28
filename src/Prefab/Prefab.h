#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <DirectXMath.h>

namespace EnginePrefab {

struct PrefabNodeData {
    std::string name = "Node";
    std::string tag = "Prop";
    DirectX::XMFLOAT3 localPosition = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 localRotation = { 0.0f, 0.0f, 0.0f }; // Euler angles in degrees
    DirectX::XMFLOAT3 localScale = { 1.0f, 1.0f, 1.0f };

    // Mesh / Model
    bool hasMesh = false;
    std::string meshType = ""; // "Cube", "Plane", "Sphere", or custom obj filepath
    DirectX::XMFLOAT4 diffuseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    float roughness = 0.5f;
    float metallic = 0.0f;
    DirectX::XMFLOAT3 emissiveColor = { 0.0f, 0.0f, 0.0f };
    float emissiveIntensity = 0.0f;

    // Light
    bool hasLight = false;
    int lightType = 0; // 0 = PointLight, 1 = SpotLight
    DirectX::XMFLOAT3 lightColor = { 1.0f, 1.0f, 1.0f };
    float lightIntensity = 3.0f;
    float lightRadius = 10.0f;
    float spotAngle = 45.0f;
    DirectX::XMFLOAT3 lightOffset = { 0.0f, 0.0f, 0.0f };

    // Audio
    bool hasAudio = false;
    std::string audioPath = "";
    float audioVolume = 1.0f;
    float audioMaxDist = 20.0f;
    bool audioLoop = false;

    // Script
    bool hasScript = false;
    std::string scriptPath = "";

    // Hierarchy
    std::vector<PrefabNodeData> children;

    // Custom Key-Values
    std::unordered_map<std::string, std::string> properties;
};

class Prefab {
public:
    Prefab(const std::string& name = "NewPrefab");
    ~Prefab() = default;

    const std::string& GetName() const { return m_name; }
    void SetName(const std::string& name) { m_name = name; }

    const std::string& GetGUID() const { return m_guid; }
    void SetGUID(const std::string& guid) { m_guid = guid; }

    const std::string& GetFilePath() const { return m_filePath; }
    void SetFilePath(const std::string& path) { m_filePath = path; }

    PrefabNodeData& GetRootData() { return m_rootData; }
    const PrefabNodeData& GetRootData() const { return m_rootData; }
    void SetRootData(const PrefabNodeData& data) { m_rootData = data; }

    // Serialization
    bool SaveToFile(const std::string& filepath);
    bool LoadFromFile(const std::string& filepath);

    std::string SerializeToString() const;
    bool DeserializeFromString(const std::string& data);

private:
    std::string m_name;
    std::string m_guid;
    std::string m_filePath;
    PrefabNodeData m_rootData;

    void WriteNode(std::ostream& os, const PrefabNodeData& node, int indent) const;
};

} // namespace EnginePrefab
