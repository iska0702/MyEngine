#include "Prefab.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>

namespace EnginePrefab {

Prefab::Prefab(const std::string& name)
    : m_name(name) {
    m_rootData.name = name;
}

void Prefab::WriteNode(std::ostream& os, const PrefabNodeData& node, int indent) const {
    std::string ind(indent * 2, ' ');
    os << ind << "[NODE]\n";
    os << ind << "name = " << node.name << "\n";
    os << ind << "tag = " << node.tag << "\n";
    os << ind << "pos = " << node.localPosition.x << " " << node.localPosition.y << " " << node.localPosition.z << "\n";
    os << ind << "rot = " << node.localRotation.x << " " << node.localRotation.y << " " << node.localRotation.z << "\n";
    os << ind << "scale = " << node.localScale.x << " " << node.localScale.y << " " << node.localScale.z << "\n";

    if (node.hasMesh) {
        os << ind << "has_mesh = 1\n";
        os << ind << "mesh_type = " << node.meshType << "\n";
        os << ind << "diffuse = " << node.diffuseColor.x << " " << node.diffuseColor.y << " " << node.diffuseColor.z << " " << node.diffuseColor.w << "\n";
        os << ind << "roughness = " << node.roughness << "\n";
        os << ind << "metallic = " << node.metallic << "\n";
        os << ind << "emissive = " << node.emissiveColor.x << " " << node.emissiveColor.y << " " << node.emissiveColor.z << "\n";
        os << ind << "emissive_intensity = " << node.emissiveIntensity << "\n";
    }

    if (node.hasLight) {
        os << ind << "has_light = 1\n";
        os << ind << "light_type = " << (node.lightType == 0 ? "Point" : "Spot") << "\n";
        os << ind << "light_color = " << node.lightColor.x << " " << node.lightColor.y << " " << node.lightColor.z << "\n";
        os << ind << "light_intensity = " << node.lightIntensity << "\n";
        os << ind << "light_radius = " << node.lightRadius << "\n";
        os << ind << "spot_angle = " << node.spotAngle << "\n";
        os << ind << "light_offset = " << node.lightOffset.x << " " << node.lightOffset.y << " " << node.lightOffset.z << "\n";
    }

    if (node.hasAudio) {
        os << ind << "has_audio = 1\n";
        os << ind << "audio_path = " << node.audioPath << "\n";
        os << ind << "audio_volume = " << node.audioVolume << "\n";
        os << ind << "audio_max_dist = " << node.audioMaxDist << "\n";
        os << ind << "audio_loop = " << (node.audioLoop ? 1 : 0) << "\n";
    }

    if (node.hasScript) {
        os << ind << "has_script = 1\n";
        os << ind << "script_path = " << node.scriptPath << "\n";
    }

    for (const auto& kv : node.properties) {
        os << ind << "prop " << kv.first << " = " << kv.second << "\n";
    }

    for (const auto& child : node.children) {
        WriteNode(os, child, indent + 1);
    }
    os << ind << "[END_NODE]\n";
}

std::string Prefab::SerializeToString() const {
    std::ostringstream os;
    os << "# MyEngine Prefab Archetype Definition\n";
    os << "[PREFAB]\n";
    os << "name = " << m_name << "\n";
    os << "guid = " << m_guid << "\n";
    os << "version = 1\n\n";

    WriteNode(os, m_rootData, 0);
    return os.str();
}

bool Prefab::SaveToFile(const std::string& filepath) {
    std::ofstream file(filepath);
    if (!file.is_open()) return false;
    file << SerializeToString();
    m_filePath = filepath;
    return true;
}

static void Trim(std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) { s = ""; return; }
    size_t end = s.find_last_not_of(" \t\r\n");
    s = s.substr(start, end - start + 1);
}

bool Prefab::DeserializeFromString(const std::string& data) {
    std::istringstream iss(data);
    std::string line;
    std::vector<PrefabNodeData> nodeStack;

    while (std::getline(iss, line)) {
        Trim(line);
        if (line.empty() || line[0] == '#') continue;

        if (line == "[PREFAB]") {
            continue;
        }

        if (line.rfind("name = ", 0) == 0 && nodeStack.empty()) {
            m_name = line.substr(7);
            Trim(m_name);
            continue;
        }

        if (line.rfind("guid = ", 0) == 0 && nodeStack.empty()) {
            m_guid = line.substr(7);
            Trim(m_guid);
            continue;
        }

        if (line == "[NODE]") {
            PrefabNodeData newNode;
            nodeStack.push_back(newNode);
            continue;
        }

        if (line == "[END_NODE]") {
            if (!nodeStack.empty()) {
                PrefabNodeData completed = nodeStack.back();
                nodeStack.pop_back();

                if (nodeStack.empty()) {
                    m_rootData = completed;
                } else {
                    nodeStack.back().children.push_back(completed);
                }
            }
            continue;
        }

        if (nodeStack.empty()) continue;
        PrefabNodeData& cur = nodeStack.back();

        size_t eqPos = line.find('=');
        if (eqPos != std::string::npos) {
            std::string key = line.substr(0, eqPos);
            std::string val = line.substr(eqPos + 1);
            Trim(key);
            Trim(val);

            std::istringstream valStream(val);

            if (key.rfind("prop ", 0) == 0 && key.size() > 5) cur.properties[key.substr(5)] = val;
            else if (key == "name") cur.name = val;
            else if (key == "tag") cur.tag = val;
            else if (key == "pos") valStream >> cur.localPosition.x >> cur.localPosition.y >> cur.localPosition.z;
            else if (key == "rot") valStream >> cur.localRotation.x >> cur.localRotation.y >> cur.localRotation.z;
            else if (key == "scale") valStream >> cur.localScale.x >> cur.localScale.y >> cur.localScale.z;
            else if (key == "has_mesh") cur.hasMesh = (val == "1" || val == "true");
            else if (key == "mesh_type") cur.meshType = val;
            else if (key == "diffuse") valStream >> cur.diffuseColor.x >> cur.diffuseColor.y >> cur.diffuseColor.z >> cur.diffuseColor.w;
            else if (key == "roughness") valStream >> cur.roughness;
            else if (key == "metallic") valStream >> cur.metallic;
            else if (key == "emissive") valStream >> cur.emissiveColor.x >> cur.emissiveColor.y >> cur.emissiveColor.z;
            else if (key == "emissive_intensity") valStream >> cur.emissiveIntensity;
            else if (key == "has_light") cur.hasLight = (val == "1" || val == "true");
            else if (key == "light_type") cur.lightType = (val == "Spot" ? 1 : 0);
            else if (key == "light_color") valStream >> cur.lightColor.x >> cur.lightColor.y >> cur.lightColor.z;
            else if (key == "light_intensity") valStream >> cur.lightIntensity;
            else if (key == "light_radius") valStream >> cur.lightRadius;
            else if (key == "spot_angle") valStream >> cur.spotAngle;
            else if (key == "light_offset") valStream >> cur.lightOffset.x >> cur.lightOffset.y >> cur.lightOffset.z;
            else if (key == "has_audio") cur.hasAudio = (val == "1" || val == "true");
            else if (key == "audio_path") cur.audioPath = val;
            else if (key == "audio_volume") valStream >> cur.audioVolume;
            else if (key == "audio_max_dist") valStream >> cur.audioMaxDist;
            else if (key == "audio_loop") cur.audioLoop = (val == "1" || val == "true");
            else if (key == "has_script") cur.hasScript = (val == "1" || val == "true");
            else if (key == "script_path") cur.scriptPath = val;
        }
    }

    return true;
}

bool Prefab::LoadFromFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) return false;

    std::stringstream ss;
    ss << file.rdbuf();
    m_filePath = filepath;
    return DeserializeFromString(ss.str());
}

} // namespace EnginePrefab
