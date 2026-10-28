#include "BitWriter.h"
#include <algorithm>
#include <cmath>

namespace EngineNetwork {

BitWriter::BitWriter(size_t initialCapacity) {
    m_buffer.reserve(initialCapacity);
}

void BitWriter::EnsureCapacity(size_t bitsNeeded) {
    size_t requiredBytes = (m_bitIndex + bitsNeeded + 7) / 8;
    if (requiredBytes > m_buffer.size()) {
        m_buffer.resize((std::max)(requiredBytes, m_buffer.size() * 2 + 16), 0);
    }
}

void BitWriter::WriteBit(bool bit) {
    EnsureCapacity(1);
    size_t byteIdx = m_bitIndex / 8;
    size_t bitOffset = m_bitIndex % 8;

    if (bit) {
        m_buffer[byteIdx] |= (1 << bitOffset);
    } else {
        m_buffer[byteIdx] &= ~(1 << bitOffset);
    }
    m_bitIndex++;
}

void BitWriter::WriteBits(uint32_t value, size_t bitCount) {
    for (size_t i = 0; i < bitCount; ++i) {
        WriteBit((value & (1u << i)) != 0);
    }
}

void BitWriter::WriteBytes(const void* data, size_t byteCount) {
    if (!data || byteCount == 0) return;
    const uint8_t* ptr = static_cast<const uint8_t*>(data);

    // If byte aligned, direct memcpy optimization
    if ((m_bitIndex % 8) == 0) {
        size_t startByte = m_bitIndex / 8;
        m_buffer.resize(startByte + byteCount);
        memcpy(&m_buffer[startByte], ptr, byteCount);
        m_bitIndex += byteCount * 8;
    } else {
        for (size_t i = 0; i < byteCount; ++i) {
            WriteBits(ptr[i], 8);
        }
    }
}

void BitWriter::WriteUInt8(uint8_t value) {
    WriteBits(value, 8);
}

void BitWriter::WriteUInt16(uint16_t value) {
    WriteBits(value, 16);
}

void BitWriter::WriteUInt32(uint32_t value) {
    WriteBits(value, 32);
}

void BitWriter::WriteUInt64(uint64_t value) {
    uint32_t low = static_cast<uint32_t>(value & 0xFFFFFFFF);
    uint32_t high = static_cast<uint32_t>((value >> 32) & 0xFFFFFFFF);
    WriteBits(low, 32);
    WriteBits(high, 32);
}

void BitWriter::WriteInt8(int8_t value) {
    WriteUInt8(static_cast<uint8_t>(value));
}

void BitWriter::WriteInt16(int16_t value) {
    WriteUInt16(static_cast<uint16_t>(value));
}

void BitWriter::WriteInt32(int32_t value) {
    WriteUInt32(static_cast<uint32_t>(value));
}

void BitWriter::WriteInt64(int64_t value) {
    WriteUInt64(static_cast<uint64_t>(value));
}

void BitWriter::WriteFloat(float value) {
    uint32_t raw;
    memcpy(&raw, &value, sizeof(float));
    WriteUInt32(raw);
}

void BitWriter::WriteDouble(double value) {
    uint64_t raw;
    memcpy(&raw, &value, sizeof(double));
    WriteUInt64(raw);
}

void BitWriter::WriteBool(bool value) {
    WriteBit(value);
}

void BitWriter::WriteQuantizedFloat(float value, float minVal, float maxVal, size_t bits) {
    float clamped = (std::clamp)(value, minVal, maxVal);
    float normalized = (clamped - minVal) / (maxVal - minVal);
    uint32_t maxInt = (1u << bits) - 1;
    uint32_t quantized = static_cast<uint32_t>(std::round(normalized * maxInt));
    WriteBits(quantized, bits);
}

void BitWriter::WriteString(const std::string& str) {
    uint16_t length = static_cast<uint16_t>((std::min)(str.length(), size_t(65535)));
    WriteUInt16(length);
    if (length > 0) {
        WriteBytes(str.data(), length);
    }
}

void BitWriter::WriteVector3(const DirectX::XMFLOAT3& vec) {
    WriteFloat(vec.x);
    WriteFloat(vec.y);
    WriteFloat(vec.z);
}

void BitWriter::WriteQuaternion(const DirectX::XMFLOAT4& quat) {
    WriteFloat(quat.x);
    WriteFloat(quat.y);
    WriteFloat(quat.z);
    WriteFloat(quat.w);
}

void BitWriter::FlushBits() {
    size_t byteCount = (m_bitIndex + 7) / 8;
    if (m_buffer.size() < byteCount) {
        m_buffer.resize(byteCount, 0);
    }
}

const uint8_t* BitWriter::GetData() const {
    return m_buffer.data();
}

uint8_t* BitWriter::GetData() {
    return m_buffer.data();
}

size_t BitWriter::GetByteSize() const {
    return (m_bitIndex + 7) / 8;
}

void BitWriter::Reset() {
    m_bitIndex = 0;
    std::fill(m_buffer.begin(), m_buffer.end(), 0);
}

} // namespace EngineNetwork
