#pragma once

#include <vector>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace PakCompression {

    bool DecompressDeflate(const uint8_t* compressedData, size_t compressedSize,
                           size_t expectedUncompressedSize,
                           std::vector<uint8_t>& outUncompressed);

    bool CompressDeflate(const uint8_t* uncompressedData, size_t uncompressedSize,
                         std::vector<uint8_t>& outCompressed);

}
