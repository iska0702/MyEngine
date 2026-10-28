#include "Texture.h"
#include "stb_image.h"

Texture::Texture(const std::string& filepath)
    : m_filepath(filepath), m_state(TextureState::Unloaded),
      m_width(0), m_height(0), m_numChannels(0), m_cpuData(nullptr),
      m_srvIndex(0), m_fenceValue(0) {
}

Texture::~Texture() {
    FreeCpuData();
}

void Texture::FreeCpuData() {
    if (m_cpuData) {
        stbi_image_free(m_cpuData);
        m_cpuData = nullptr;
    }
}
