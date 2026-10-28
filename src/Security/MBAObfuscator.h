#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace EngineSecurity {

/**
 * @brief Mixed-Boolean Arithmetic (MBA) Obfuscation Engine.
 * 
 * Replaces standard arithmetic and logical operations with mathematically
 * equivalent expressions that interleave bitwise boolean algebra (&, |, ^, ~)
 * and arithmetic operations (+, -, *).
 * 
 * This creates non-linear polynomial expansions that thwart static decompilation
 * (IDA Pro, Ghidra) and cause state-space explosion in symbolic execution / SMT
 * solvers (angr, Triton, Z3).
 */
class MBAObfuscator {
public:
    // --- 64-bit Core Mixed-Boolean Arithmetic Operations ---

    /**
     * @brief Computes (a + b) via Mixed-Boolean Arithmetic identity:
     * (a ^ b) + 2 * (a & b)
     */
    static inline uint64_t Add(uint64_t a, uint64_t b) {
        return (a ^ b) + ((a & b) << 1);
    }

    /**
     * @brief Computes (a + b) with higher polynomial MBA complexity:
     * (((a | b) * 2) - (a ^ b))
     */
    static inline uint64_t AddComplex(uint64_t a, uint64_t b) {
        uint64_t orVal = (a | b);
        uint64_t xorVal = (a ^ b);
        return (orVal << 1) - xorVal;
    }

    /**
     * @brief Computes (a - b) via Mixed-Boolean Arithmetic identity:
     * (a ^ ~b) + 1 + 2 * (~a & ~b) -> simplifies to: (a ^ b) - 2 * (~a & b)
     */
    static inline uint64_t Sub(uint64_t a, uint64_t b) {
        return (a ^ b) - ((~a & b) << 1);
    }

    /**
     * @brief Computes (a ^ b) via Mixed-Boolean Arithmetic identity:
     * (a | b) - (a & b)
     */
    static inline uint64_t Xor(uint64_t a, uint64_t b) {
        return (a | b) - (a & b);
    }

    /**
     * @brief Computes (a ^ b) via alternative disjoint MBA identity:
     * (a & ~b) | (~a & b)
     */
    static inline uint64_t XorDisjoint(uint64_t a, uint64_t b) {
        return (a & ~b) | (~a & b);
    }

    /**
     * @brief Computes (a & b) via Mixed-Boolean Arithmetic identity:
     * (a + b) - (a | b)
     */
    static inline uint64_t And(uint64_t a, uint64_t b) {
        return Sub(Add(a, b), (a | b));
    }

    /**
     * @brief Computes (a | b) via Mixed-Boolean Arithmetic identity:
     * (a + b) - (a & b)
     */
    static inline uint64_t Or(uint64_t a, uint64_t b) {
        return Sub(Add(a, b), (a & b));
    }

    /**
     * @brief Computes (~a) via Mixed-Boolean Arithmetic identity:
     * (-a) - 1
     */
    static inline uint64_t Not(uint64_t a) {
        return Sub((uint64_t)(-(int64_t)a), 1);
    }

    /**
     * @brief Evaluates whether (a == b) through MBA reduction.
     * Returns true if equal, false otherwise, without direct comparison operators.
     */
    static inline bool Equal(uint64_t a, uint64_t b) {
        uint64_t diff = Xor(a, b);
        // If diff == 0, then diff | -diff has high bit 0, otherwise high bit 1
        uint64_t check = diff | (uint64_t)(-(int64_t)diff);
        return ((check >> 63) == 0);
    }

    // --- 32-bit Core MBA Operations ---

    static inline uint32_t Add32(uint32_t a, uint32_t b) {
        return (uint32_t)Add(a, b);
    }

    static inline uint32_t Sub32(uint32_t a, uint32_t b) {
        return (uint32_t)Sub(a, b);
    }

    static inline uint32_t Xor32(uint32_t a, uint32_t b) {
        return (uint32_t)Xor(a, b);
    }

    static inline bool Equal32(uint32_t a, uint32_t b) {
        return Equal((uint64_t)a, (uint64_t)b);
    }

    // --- MBA-Protected Cryptographic Hashing & Checksums ---

    /**
     * @brief Calculates CRC32 of memory data where every table lookup, shift,
     * XOR, and accumulation is executed via MBA transformations.
     */
    static uint32_t CRC32(const uint8_t* data, size_t length, uint32_t seed = 0xFFFFFFFF);

    /**
     * @brief Calculates FNV-1a 64-bit hash using MBA multiplications and XORs.
     */
    static uint64_t FNV1a64(const uint8_t* data, size_t length);

    /**
     * @brief Obfuscated rolling polynomial hash for memory blocks.
     */
    static uint64_t RollingHash(const uint8_t* data, size_t length, uint64_t seed = 0xCBF29CE484222325ULL);

    /**
     * @brief Obfuscated constant decoder (decodes string on stack via rolling MBA XOR).
     */
    static std::string DeobfuscateString(const uint8_t* encrypted, size_t len, uint8_t key);

    /**
     * @brief Obfuscated wide-character constant decoder.
     */
    static std::wstring DeobfuscateStringW(const uint8_t* encrypted, size_t len, uint8_t key);

    /**
     * @brief Dynamically derives the master HMAC signing key on stack via MBA equations
     * so that the raw contiguous 32-byte key never exists in static PE data sections.
     */
    static void SynthesizeMasterKey(uint8_t outKey[32]);
};

} // namespace EngineSecurity
