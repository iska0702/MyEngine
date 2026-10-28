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

class BitReader {
public:
    BitReader();
    BitReader(const void* data, size_t byteSize);

    void SetBuffer(const void* data, size_t byteSize);

    bool ReadBit(bool& outBit);
    bool ReadBits(uint32_t& outValue, size_t bitCount);
    bool ReadBytes(void* outData, size_t byteCount);

    bool ReadUInt8(uint8_t& outValue);
    bool ReadUInt16(uint16_t& outValue);
    bool ReadUInt32(uint32_t& outValue);
    bool ReadUInt64(uint64_t& outValue);

    bool ReadInt8(int8_t& outValue);
    bool ReadInt16(int16_t& outValue);
    bool ReadInt32(int32_t& outValue);
    bool ReadInt64(int64_t& outValue);

    bool ReadFloat(float& outValue);
    bool ReadDouble(double& outValue);
    bool ReadBool(bool& outValue);

    bool ReadQuantizedFloat(float& outValue, float minVal, float maxVal, size_t bits = 16);

    bool ReadString(std::string& outStr);
    bool ReadVector3(DirectX::XMFLOAT3& outVec);
    bool ReadQuaternion(DirectX::XMFLOAT4& outQuat);

    size_t GetRemainingBits() const;
    size_t GetRemainingBytes() const;
    bool HasOverflowed() const { return m_overflow; }
    size_t GetBitIndex() const { return m_bitIndex; }

private:
    const uint8_t* m_data = nullptr;
    size_t m_totalBits = 0;
    size_t m_bitIndex = 0;
    bool m_overflow = false;
};

} // namespace EngineNetwork
