#pragma once
#include "AnimationCommon.h"
#include "Renderer/Renderer.h"
#include <vector>
#include <memory>

namespace EngineAnim {

struct SkinnedVertex {
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
    float boneWeights[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
    uint16_t boneIndices[4] = { 0, 0, 0, 0 };
};

class SkinnedMesh {
public:
    SkinnedMesh() = default;

    void SetData(std::vector<SkinnedVertex> vertices, std::vector<uint32_t> indices, std::vector<Matrix4x4> invBindMatrices);
    void Deform(const std::vector<Matrix4x4>& currentModelMatrices);
    
    const std::vector<SkinnedVertex>& GetRestVertices() const { return m_restVertices; }
    const std::vector<uint32_t>& GetIndices() const { return m_indices; }
    const std::vector<Renderer::Vertex>& GetDeformedVertices() const { return m_deformedVertices; }

    void UploadToVertexBuffer(ID3D12Resource* pBuffer);

    size_t GetVertexCount() const { return m_restVertices.size(); }
    size_t GetIndexCount() const { return m_indices.size(); }

private:
    std::vector<SkinnedVertex> m_restVertices;
    std::vector<uint32_t> m_indices;
    std::vector<Matrix4x4> m_inverseBindMatrices;
    std::vector<Renderer::Vertex> m_deformedVertices;
};

} // namespace EngineAnim
