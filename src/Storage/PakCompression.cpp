#include "PakCompression.h"

#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_ARCHIVE_WRITING_APIS
#define MINIZ_NO_ZLIB_APIS

// Include miniz implementation
#include "../../ThirdParty/Libraries/miniz/miniz.h"

namespace PakCompression {

    bool DecompressDeflate(const uint8_t* compressedData, size_t compressedSize,
                           size_t expectedUncompressedSize,
                           std::vector<uint8_t>& outUncompressed) {
        if (!compressedData || compressedSize == 0 || expectedUncompressedSize == 0) {
            return false;
        }

        outUncompressed.resize(expectedUncompressedSize);
        // flags = 0 -> Raw Deflate (RFC 1951) as produced by .NET DeflateStream
        size_t result = tinfl_decompress_mem_to_mem(
            outUncompressed.data(), outUncompressed.size(),
            compressedData, compressedSize,
            0 // 0 = raw deflate, no zlib header
        );

        if (result == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED || result != expectedUncompressedSize) {
            outUncompressed.clear();
            return false;
        }

        return true;
    }

    bool CompressDeflate(const uint8_t* uncompressedData, size_t uncompressedSize,
                         std::vector<uint8_t>& outCompressed) {
        if (!uncompressedData || uncompressedSize == 0) {
            return false;
        }

        size_t maxCompressedSize = uncompressedSize + (uncompressedSize / 8) + 1024;
        outCompressed.resize(maxCompressedSize);

        // flags = 0 -> raw Deflate (no zlib/gzip headers, optimal ratio)
        size_t compressedLen = tdefl_compress_mem_to_mem(
            outCompressed.data(), outCompressed.size(),
            uncompressedData, uncompressedSize,
            0
        );

        if (compressedLen == 0) {
            outCompressed.clear();
            return false;
        }

        outCompressed.resize(compressedLen);
        return true;
    }

}
