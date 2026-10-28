void Renderer::InitPortalVisuals() {
    bool hasPortalA = false, hasPortalB = false;
    for (const auto& m : m_models) {
        if (m.name == "Portal A") hasPortalA = true;
        if (m.name == "Portal B") hasPortalB = true;
    }

    if (!hasPortalA) {
        RenderModel mA("Portal A", m_planeIndexCount, m_planeStartIndex, m_planeBaseVertex, m_planeVertexCount);
        mA.numLODs = m_planeNumLODs;
        for (uint32_t i = 0; i < m_planeNumLODs; ++i) mA.lods[i] = m_planeLOD[i];
        mA.visible = false;
        mA.hasCollision = false;
        mA.material.albedo = { 0.65f, 0.15f, 0.95f }; // Minecraft Nether Purple
        mA.material.emission = 4.0f;
        mA.material.roughness = 0.05f;
        mA.material.metallic = 0.9f;
        mA.material.ior = 9.9f; // Nether Portal A Special Identifier Tag for HLSL
        m_models.push_back(mA);
    }

    if (!hasPortalB) {
        RenderModel mB("Portal B", m_planeIndexCount, m_planeStartIndex, m_planeBaseVertex, m_planeVertexCount);
        mB.numLODs = m_planeNumLODs;
        for (uint32_t i = 0; i < m_planeNumLODs; ++i) mB.lods[i] = m_planeLOD[i];
        mB.visible = false;
        mB.hasCollision = false;
        mB.material.albedo = { 0.75f, 0.12f, 0.85f }; // Nether Portal B Purple
        mB.material.emission = 4.0f;
        mB.material.roughness = 0.05f;
        mB.material.metallic = 0.9f;
        mB.material.ior = 9.8f; // Nether Portal B Special Identifier Tag for HLSL
        m_models.push_back(mB);
    }

    UpdateModelMeshletRanges();
}

void Renderer::UpdatePortalVisuals() {
    auto& portalSys = PortalSystem::Get();
    bool hasPortalA = false, hasPortalB = false;
    for (const auto& m : m_models) {
        if (m.name == "Portal A") hasPortalA = true;
        if (m.name == "Portal B") hasPortalB = true;
    }
    if (!hasPortalA || !hasPortalB) {
        InitPortalVisuals();
    }

    for (auto& model : m_models) {
        if (model.name == "Portal A") {
            const auto& p = portalSys.GetPortal(PortalID::Blue_A);
            model.visible = p.isPlaced;
            if (p.isPlaced) {
                model.position = p.position;
                model.material.albedo = { p.color.x, p.color.y, p.color.z };
                model.material.emission = p.glowIntensity;
                model.material.ior = 9.9f; // Tag for Nether Portal A
                // Plane local orientation: Normal is Y=(0,1,0), Up is Z=(0,0,1), Right is X=(1,0,0)
                // We construct worldMatrix so that local (X, Y, Z) maps to (Right * width*0.5, Normal, Up * height*0.5)
                XMVECTOR r = XMLoadFloat3(&p.right) * (p.width * 0.5f);
                XMVECTOR n = XMLoadFloat3(&p.normal);
                XMVECTOR u = XMLoadFloat3(&p.up) * (p.height * 0.5f);
                XMVECTOR pos = XMLoadFloat3(&p.position);
                XMMATRIX W;
                W.r[0] = r;
                W.r[1] = n;
                W.r[2] = u;
                W.r[3] = XMVectorSetW(pos, 1.0f);
                XMStoreFloat4x4(&model.worldMatrix, W);
            }
        } else if (model.name == "Portal B") {
            const auto& p = portalSys.GetPortal(PortalID::Orange_B);
            model.visible = p.isPlaced;
            if (p.isPlaced) {
                model.position = p.position;
                model.material.albedo = { p.color.x, p.color.y, p.color.z };
                model.material.emission = p.glowIntensity;
                model.material.ior = 9.8f; // Tag for Nether Portal B
                XMVECTOR r = XMLoadFloat3(&p.right) * (p.width * 0.5f);
                XMVECTOR n = XMLoadFloat3(&p.normal);
                XMVECTOR u = XMLoadFloat3(&p.up) * (p.height * 0.5f);
                XMVECTOR pos = XMLoadFloat3(&p.position);
                XMMATRIX W;
                W.r[0] = r;
                W.r[1] = n;
                W.r[2] = u;
                W.r[3] = XMVectorSetW(pos, 1.0f);
                XMStoreFloat4x4(&model.worldMatrix, W);
            }
        }
    }
}

