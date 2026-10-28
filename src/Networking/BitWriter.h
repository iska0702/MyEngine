#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#ifdef _WIN32
#include <DirectXMath.h>
#else
#include "NetworkTypes.h"
#endif

namespace EngineNetwork {

class BitWriter {
public:
    BitWriter(size_t initialCapacity = 256);

    void WriteBit(bool bit);
    void WriteBits(uint32_t value, size_t bitCount);
    void WriteBytes(const void* data, size_t byteCount);

    void WriteUInt8(uint8_t value);
    void WriteUInt16(uint16_t value);
    void WriteUInt32(uint32_t value);
    void WriteUInt64(uint64_t value);

    void WriteInt8(int8_t value);
    void WriteInt16(int16_t value);
    void WriteInt32(int32_t value);
    void WriteInt64(int64_t value);

    void WriteFloat(float value);
    void WriteDouble(double value);
    void WriteBool(bool value);

    // Quantized float serialization to save network bandwidth
    // Normalizes value in range [minVal, maxVal] to integer with given bits (e.g. 16 bits = 65535 steps)
    void WriteQuantizedFloat(float value, float minVal, float maxVal, size_t bits = 16);

    void WriteString(const std::string& str);
    void WriteVector3(const DirectX::XMFLOAT3& vec);
    void WriteQuaternion(const DirectX::XMFLOAT4& quat);

    // Finalize pending bits and get byte buffer
    void FlushBits();
    
    const uint8_t* GetData() const;
    uint8_t* GetData();
    size_t GetByteSize() const;
    size_t GetBitCount() const { return m_bitIndex; }
    void Reset();

private:
    void EnsureCapacity(size_t bitsNeeded);

    std::vector<uint8_t> m_buffer;
    size_t m_bitIndex = 0;
};

} // namespace EngineNetwork
