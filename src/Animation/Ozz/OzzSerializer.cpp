#include "OzzSerializer.h"
#include <fstream>
#include <filesystem>

namespace EngineAnim {

namespace {
    const uint32_t SKEL_MAGIC = 0x4C454B53; // "SKEL"
    const uint32_t ANIM_MAGIC = 0x4D494E41; // "ANIM"
    const uint32_t VERSION = 1;

    void WriteString(std::ofstream& out, const std::string& str) {
        uint32_t len = static_cast<uint32_t>(str.size());
        out.write(reinterpret_cast<const char*>(&len), sizeof(len));
        if (len > 0) {
            out.write(str.data(), len);
        }
    }

    bool ReadString(std::ifstream& in, std::string& str) {
        uint32_t len = 0;
        if (!in.read(reinterpret_cast<char*>(&len), sizeof(len))) return false;
        str.resize(len);
        if (len > 0) {
            if (!in.read(&str[0], len)) return false;
        }
        return true;
    }
}

bool OzzSerializer::SaveSkeleton(const OzzSkeleton& skeleton, const std::string& filepath) {
    std::filesystem::path p(filepath);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }

    std::ofstream out(filepath, std::ios::binary);
    if (!out.is_open()) return false;

    out.write(reinterpret_cast<const char*>(&SKEL_MAGIC), sizeof(SKEL_MAGIC));
    out.write(reinterpret_cast<const char*>(&VERSION), sizeof(VERSION));

    uint32_t numJoints = static_cast<uint32_t>(skeleton.GetNumJoints());
    out.write(reinterpret_cast<const char*>(&numJoints), sizeof(numJoints));

    for (uint32_t i = 0; i < numJoints; ++i) {
        const auto& j = skeleton.GetJoint(i);
        WriteString(out, j.name);
        out.write(reinterpret_cast<const char*>(&j.parentIndex), sizeof(j.parentIndex));
        out.write(reinterpret_cast<const char*>(&j.restPose), sizeof(j.restPose));
    }

    return true;
}

bool OzzSerializer::LoadSkeleton(OzzSkeleton& skeleton, const std::string& filepath) {
    std::ifstream in(filepath, std::ios::binary);
    if (!in.is_open()) return false;

    uint32_t magic = 0, ver = 0;
    if (!in.read(reinterpret_cast<char*>(&magic), sizeof(magic))) return false;
    if (!in.read(reinterpret_cast<char*>(&ver), sizeof(ver))) return false;
    if (magic != SKEL_MAGIC) return false;

    uint32_t numJoints = 0;
    if (!in.read(reinterpret_cast<char*>(&numJoints), sizeof(numJoints))) return false;

    std::vector<OzzJoint> joints(numJoints);
    for (uint32_t i = 0; i < numJoints; ++i) {
        if (!ReadString(in, joints[i].name)) return false;
        if (!in.read(reinterpret_cast<char*>(&joints[i].parentIndex), sizeof(joints[i].parentIndex))) return false;
        if (!in.read(reinterpret_cast<char*>(&joints[i].restPose), sizeof(joints[i].restPose))) return false;
    }

    skeleton.SetJoints(std::move(joints));
    return true;
}

bool OzzSerializer::SaveAnimation(const OzzAnimation& animation, const std::string& filepath) {
    std::filesystem::path p(filepath);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }

    std::ofstream out(filepath, std::ios::binary);
    if (!out.is_open()) return false;

    out.write(reinterpret_cast<const char*>(&ANIM_MAGIC), sizeof(ANIM_MAGIC));
    out.write(reinterpret_cast<const char*>(&VERSION), sizeof(VERSION));

    WriteString(out, animation.GetName());
    float duration = animation.GetDuration();
    out.write(reinterpret_cast<const char*>(&duration), sizeof(duration));

    uint32_t numTracks = static_cast<uint32_t>(animation.GetNumTracks());
    out.write(reinterpret_cast<const char*>(&numTracks), sizeof(numTracks));

    const auto& tracks = animation.GetTracks();
    for (uint32_t i = 0; i < numTracks; ++i) {
        const auto& track = tracks[i];
        
        uint32_t nTrans = static_cast<uint32_t>(track.translations.size());
        out.write(reinterpret_cast<const char*>(&nTrans), sizeof(nTrans));
        if (nTrans > 0) {
            out.write(reinterpret_cast<const char*>(track.translations.data()), nTrans * sizeof(OzzKeyframeTranslation));
        }

        uint32_t nRot = static_cast<uint32_t>(track.rotations.size());
        out.write(reinterpret_cast<const char*>(&nRot), sizeof(nRot));
        if (nRot > 0) {
            out.write(reinterpret_cast<const char*>(track.rotations.data()), nRot * sizeof(OzzKeyframeRotation));
        }

        uint32_t nScale = static_cast<uint32_t>(track.scales.size());
        out.write(reinterpret_cast<const char*>(&nScale), sizeof(nScale));
        if (nScale > 0) {
            out.write(reinterpret_cast<const char*>(track.scales.data()), nScale * sizeof(OzzKeyframeScale));
        }
    }

    return true;
}

bool OzzSerializer::LoadAnimation(OzzAnimation& animation, const std::string& filepath) {
    std::ifstream in(filepath, std::ios::binary);
    if (!in.is_open()) return false;

    uint32_t magic = 0, ver = 0;
    if (!in.read(reinterpret_cast<char*>(&magic), sizeof(magic))) return false;
    if (!in.read(reinterpret_cast<char*>(&ver), sizeof(ver))) return false;
    if (magic != ANIM_MAGIC) return false;

    std::string name;
    if (!ReadString(in, name)) return false;
    animation.SetName(name);

    float duration = 0.0f;
    if (!in.read(reinterpret_cast<char*>(&duration), sizeof(duration))) return false;
    animation.SetDuration(duration);

    uint32_t numTracks = 0;
    if (!in.read(reinterpret_cast<char*>(&numTracks), sizeof(numTracks))) return false;

    std::vector<OzzJointTrack> tracks(numTracks);
    for (uint32_t i = 0; i < numTracks; ++i) {
        auto& track = tracks[i];

        uint32_t nTrans = 0;
        if (!in.read(reinterpret_cast<char*>(&nTrans), sizeof(nTrans))) return false;
        track.translations.resize(nTrans);
        if (nTrans > 0) {
            if (!in.read(reinterpret_cast<char*>(track.translations.data()), nTrans * sizeof(OzzKeyframeTranslation))) return false;
        }

        uint32_t nRot = 0;
        if (!in.read(reinterpret_cast<char*>(&nRot), sizeof(nRot))) return false;
        track.rotations.resize(nRot);
        if (nRot > 0) {
            if (!in.read(reinterpret_cast<char*>(track.rotations.data()), nRot * sizeof(OzzKeyframeRotation))) return false;
        }

        uint32_t nScale = 0;
        if (!in.read(reinterpret_cast<char*>(&nScale), sizeof(nScale))) return false;
        track.scales.resize(nScale);
        if (nScale > 0) {
            if (!in.read(reinterpret_cast<char*>(track.scales.data()), nScale * sizeof(OzzKeyframeScale))) return false;
        }
    }

    animation.SetTracks(std::move(tracks));
    return true;
}

} // namespace EngineAnim
