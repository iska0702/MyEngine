#pragma once
#include "CharacterAsset.h"
#include <vector>
#include <memory>
#include <string>

class Renderer;
class Window;
class Camera;

namespace EngineAnim {

class AnimationSystem {
public:
#if defined(_WIN32)
    static AnimationSystem& Get();

    void Initialize(Renderer* renderer);
    void Update(float dt, Window* window, Camera* camera);
    void DrawUI(Renderer* renderer, Camera* camera);
    std::shared_ptr<CharacterAsset> FindCharacterForModel(Renderer* renderer,uint32_t modelId) const;
    bool SetRagdollEnabled(Renderer* renderer,uint32_t modelId,bool enabled);
    bool DrawRagdollInspector(Renderer* renderer,uint32_t modelId);

    void ImportCharacterDialog(Renderer* renderer);
    std::shared_ptr<CharacterAsset> ImportCharacter(const std::string& fbxPath, Renderer* renderer);

    const std::vector<std::shared_ptr<CharacterAsset>>& GetCharacters() const { return m_characters; }
    std::vector<std::shared_ptr<CharacterAsset>>& GetCharacters() { return m_characters; }

    std::shared_ptr<CharacterAsset> GetSelectedCharacter() const;
    int GetSelectedCharacterIndex() const { return m_selectedCharIndex; }
    void SetSelectedCharacterIndex(int idx) { m_selectedCharIndex = idx; }

    int GetSelectedBoneIndex() const { return m_selectedBoneIndex; }
    void SetSelectedBoneIndex(int idx) { m_selectedBoneIndex = idx; }

    bool IsBoneMappingWindowOpen() const;
    void SetBoneMappingWindowOpen(bool open);
    void ToggleBoneMappingWindow();
#else
    static AnimationSystem& Get() {
        static AnimationSystem s_instance;
        return s_instance;
    }

    void Initialize(Renderer*) {}
    void Update(float, Window*, Camera*) {}
    void DrawUI(Renderer*, Camera*) {}
    std::shared_ptr<CharacterAsset> FindCharacterForModel(Renderer*,uint32_t) const{return nullptr;}
    bool DrawRagdollInspector(Renderer*,uint32_t){return false;}

    void ImportCharacterDialog(Renderer*) {}
    std::shared_ptr<CharacterAsset> ImportCharacter(const std::string&, Renderer*) { return nullptr; }

    const std::vector<std::shared_ptr<CharacterAsset>>& GetCharacters() const { return m_characters; }
    std::vector<std::shared_ptr<CharacterAsset>>& GetCharacters() { return m_characters; }

    std::shared_ptr<CharacterAsset> GetSelectedCharacter() const { return nullptr; }
    int GetSelectedCharacterIndex() const { return -1; }
    void SetSelectedCharacterIndex(int) {}

    int GetSelectedBoneIndex() const { return -1; }
    void SetSelectedBoneIndex(int) {}

    bool IsBoneMappingWindowOpen() const { return false; }
    void SetBoneMappingWindowOpen(bool) {}
    void ToggleBoneMappingWindow() {}
#endif

private:
    AnimationSystem() = default;

    Renderer* m_renderer = nullptr;
    std::vector<std::shared_ptr<CharacterAsset>> m_characters;
    int m_selectedCharIndex = -1;
    int m_selectedBoneIndex = -1;

    void DrawCharacterHierarchy(std::shared_ptr<CharacterAsset> character);
    void DrawBoneTree(const OzzSkeleton& skeleton, int boneIdx);
    void DrawAnimationInspector(std::shared_ptr<CharacterAsset> character);
    void DrawMotionMatchingDebug(std::shared_ptr<CharacterAsset> character);
    void DrawTrajectoryInWorld(std::shared_ptr<CharacterAsset> character, Camera* camera);
};

} // namespace EngineAnim
