#include "SkinnedMesh.h"
#include <cstring>
#include <algorithm>

namespace EngineAnim {

void SkinnedMesh::SetData(std::vector<SkinnedVertex> vertices, std::vector<uint32_t> indices, std::vector<Matrix4x4> invBindMatrices) {
    m_restVertices = std::move(vertices);
    m_indices = std::move(indices);
    m_inverseBindMatrices = std::move(invBindMatrices);

    m_deformedVertices.resize(m_restVertices.size());
    for (size_t i = 0; i < m_restVertices.size(); ++i) {
        m_deformedVertices[i].position = m_restVertices[i].position.ToXM();
        m_deformedVertices[i].normal = m_restVertices[i].normal.ToXM();
        m_deformedVertices[i].color = XMFLOAT4(1.0f, 1.0f, m_restVertices[i].uv.x, m_restVertices[i].uv.y);
    }
}

void SkinnedMesh::Deform(const std::vector<Matrix4x4>& currentModelMatrices) {
    if (m_restVertices.empty() || currentModelMatrices.empty()) return;

    size_t numBones = currentModelMatrices.size();
    std::vector<XMMATRIX> skinMatrices(numBones);

    for (size_t b = 0; b < numBones; ++b) {
        XMMATRIX currentMat = currentModelMatrices[b].ToXM();
        if (b < m_inverseBindMatrices.size()) {
            XMMATRIX invBind = m_inverseBindMatrices[b].ToXM();
            skinMatrices[b] = invBind * currentMat; // DirectX row-major (v * invBind * model)
        } else {
            skinMatrices[b] = currentMat;
        }
    }

    size_t numVerts = m_restVertices.size();
    if (m_deformedVertices.size() != numVerts) {
        m_deformedVertices.resize(numVerts);
    }

    for (size_t i = 0; i < numVerts; ++i) {
        const auto& rv = m_restVertices[i];
        XMVECTOR restPos = rv.position.ToVec();
        XMVECTOR restNorm = XMVectorSet(rv.normal.x, rv.normal.y, rv.normal.z, 0.0f);

        XMVECTOR blendedPos = XMVectorZero();
        XMVECTOR blendedNorm = XMVectorZero();

        float totalWeight = 0.0f;
        for (int k = 0; k < 4; ++k) {
            float w = rv.boneWeights[k];
            if (w > 1e-4f) {
                uint16_t bIdx = rv.boneIndices[k];
                if (bIdx < numBones) {
                    const XMMATRIX& sm = skinMatrices[bIdx];
                    blendedPos = XMVectorAdd(blendedPos, XMVectorScale(XMVector3TransformCoord(restPos, sm), w));
                    blendedNorm = XMVectorAdd(blendedNorm, XMVectorScale(XMVector3TransformNormal(restNorm, sm), w));
                    totalWeight += w;
                }
            }
        }

        if (totalWeight < 1e-4f) {
            blendedPos = restPos;
            blendedNorm = restNorm;
        } else {
            blendedNorm = XMVector3Normalize(blendedNorm);
        }

        XMStoreFloat3(&m_deformedVertices[i].position, blendedPos);
        XMStoreFloat3(&m_deformedVertices[i].normal, blendedNorm);
        m_deformedVertices[i].color = XMFLOAT4(1.0f, 1.0f, rv.uv.x, rv.uv.y);
    }
}

#if defined(_WIN32)
void SkinnedMesh::UploadToVertexBuffer(ID3D12Resource* pBuffer) {
    if (!pBuffer || m_deformedVertices.empty()) return;

    void* pData = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    if (SUCCEEDED(pBuffer->Map(0, &readRange, &pData)) && pData) {
        size_t byteSize = m_deformedVertices.size() * sizeof(Renderer::Vertex);
        std::memcpy(pData, m_deformedVertices.data(), byteSize);
        D3D12_RANGE writeRange = { 0, byteSize };
        pBuffer->Unmap(0, &writeRange);
    }
}
#else
void SkinnedMesh::UploadToVertexBuffer(ID3D12Resource* pBuffer) {
    (void)pBuffer;
}
#endif

} // namespace EngineAnim
