#include "BitReader.h"
#include <algorithm>
#include <cmath>

namespace EngineNetwork {

BitReader::BitReader() : m_data(nullptr), m_totalBits(0), m_bitIndex(0), m_overflow(false) {}

BitReader::BitReader(const void* data, size_t byteSize) {
    SetBuffer(data, byteSize);
}

void BitReader::SetBuffer(const void* data, size_t byteSize) {
    m_data = static_cast<const uint8_t*>(data);
    m_totalBits = byteSize * 8;
    m_bitIndex = 0;
    m_overflow = false;
}

bool BitReader::ReadBit(bool& outBit) {
    if (m_bitIndex >= m_totalBits || !m_data) {
        m_overflow = true;
        outBit = false;
        return false;
    }

    size_t byteIdx = m_bitIndex / 8;
    size_t bitOffset = m_bitIndex % 8;
    outBit = (m_data[byteIdx] & (1 << bitOffset)) != 0;
    m_bitIndex++;
    return true;
}

bool BitReader::ReadBits(uint32_t& outValue, size_t bitCount) {
    outValue = 0;
    for (size_t i = 0; i < bitCount; ++i) {
        bool bit = false;
        if (!ReadBit(bit)) return false;
        if (bit) {
            outValue |= (1u << i);
        }
    }
    return true;
}

bool BitReader::ReadBytes(void* outData, size_t byteCount) {
    if (!outData || byteCount == 0) return true;
    uint8_t* ptr = static_cast<uint8_t*>(outData);

    if ((m_bitIndex % 8) == 0 && (m_bitIndex + byteCount * 8 <= m_totalBits)) {
        size_t startByte = m_bitIndex / 8;
        memcpy(ptr, &m_data[startByte], byteCount);
        m_bitIndex += byteCount * 8;
        return true;
    }

    for (size_t i = 0; i < byteCount; ++i) {
        uint32_t b = 0;
        if (!ReadBits(b, 8)) return false;
        ptr[i] = static_cast<uint8_t>(b);
    }
    return true;
}

bool BitReader::ReadUInt8(uint8_t& outValue) {
    uint32_t val = 0;
    if (!ReadBits(val, 8)) return false;
    outValue = static_cast<uint8_t>(val);
    return true;
}

bool BitReader::ReadUInt16(uint16_t& outValue) {
    uint32_t val = 0;
    if (!ReadBits(val, 16)) return false;
    outValue = static_cast<uint16_t>(val);
    return true;
}

bool BitReader::ReadUInt32(uint32_t& outValue) {
    return ReadBits(outValue, 32);
}

bool BitReader::ReadUInt64(uint64_t& outValue) {
    uint32_t low = 0, high = 0;
    if (!ReadBits(low, 32)) return false;
    if (!ReadBits(high, 32)) return false;
    outValue = (static_cast<uint64_t>(high) << 32) | low;
    return true;
}

bool BitReader::ReadInt8(int8_t& outValue) {
    uint8_t val = 0;
    if (!ReadUInt8(val)) return false;
    outValue = static_cast<int8_t>(val);
    return true;
}

bool BitReader::ReadInt16(int16_t& outValue) {
    uint16_t val = 0;
    if (!ReadUInt16(val)) return false;
    outValue = static_cast<int16_t>(val);
    return true;
}

bool BitReader::ReadInt32(int32_t& outValue) {
    uint32_t val = 0;
    if (!ReadUInt32(val)) return false;
    outValue = static_cast<int32_t>(val);
    return true;
}

bool BitReader::ReadInt64(int64_t& outValue) {
    uint64_t val = 0;
    if (!ReadUInt64(val)) return false;
    outValue = static_cast<int64_t>(val);
    return true;
}

bool BitReader::ReadFloat(float& outValue) {
    uint32_t raw = 0;
    if (!ReadUInt32(raw)) return false;
    memcpy(&outValue, &raw, sizeof(float));
    return true;
}

bool BitReader::ReadDouble(double& outValue) {
    uint64_t raw = 0;
    if (!ReadUInt64(raw)) return false;
    memcpy(&outValue, &raw, sizeof(double));
    return true;
}

bool BitReader::ReadBool(bool& outValue) {
    return ReadBit(outValue);
}

bool BitReader::ReadQuantizedFloat(float& outValue, float minVal, float maxVal, size_t bits) {
    uint32_t raw = 0;
    if (!ReadBits(raw, bits)) return false;
    uint32_t maxInt = (1u << bits) - 1;
    float normalized = static_cast<float>(raw) / static_cast<float>(maxInt);
    outValue = minVal + normalized * (maxVal - minVal);
    return true;
}

bool BitReader::ReadString(std::string& outStr) {
    uint16_t length = 0;
    if (!ReadUInt16(length)) return false;
    if (length == 0) {
        outStr.clear();
        return true;
    }

    if (m_bitIndex + length * 8 > m_totalBits) {
        m_overflow = true;
        return false;
    }

    outStr.resize(length);
    return ReadBytes(&outStr[0], length);
}

bool BitReader::ReadVector3(DirectX::XMFLOAT3& outVec) {
    return ReadFloat(outVec.x) && ReadFloat(outVec.y) && ReadFloat(outVec.z);
}

bool BitReader::ReadQuaternion(DirectX::XMFLOAT4& outQuat) {
    return ReadFloat(outQuat.x) && ReadFloat(outQuat.y) && ReadFloat(outQuat.z) && ReadFloat(outQuat.w);
}

size_t BitReader::GetRemainingBits() const {
    return (m_bitIndex < m_totalBits) ? (m_totalBits - m_bitIndex) : 0;
}

size_t BitReader::GetRemainingBytes() const {
    return GetRemainingBits() / 8;
}

} // namespace EngineNetwork
