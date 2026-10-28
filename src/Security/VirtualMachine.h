#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <memory>

namespace EngineSecurity {

// Virtual Machine Opcodes (Obfuscated non-contiguous values)
enum VmOpcode : uint8_t {
    VM_OP_NOP             = 0x13,
    VM_OP_MOV_IMM         = 0x48,
    VM_OP_MOV_REG         = 0x51,
    VM_OP_LOAD_64         = 0x67,
    VM_OP_STORE_64        = 0x72,
    VM_OP_PUSH            = 0x89,
    VM_OP_POP             = 0x93,
    VM_OP_ADD             = 0xA2,
    VM_OP_SUB             = 0xAF,
    VM_OP_MBA_ADD         = 0xB5,
    VM_OP_MBA_SUB         = 0xBD,
    VM_OP_MBA_XOR         = 0xC4,
    VM_OP_XOR             = 0xCB,
    VM_OP_AND             = 0xD1,
    VM_OP_OR              = 0xD8,
    VM_OP_SHL             = 0xDF,
    VM_OP_SHR             = 0xE3,
    VM_OP_CMP             = 0xE9,
    VM_OP_JMP             = 0xF0,
    VM_OP_JZ              = 0xF3,
    VM_OP_JNZ             = 0xF7,
    VM_OP_CALL            = 0xFA,
    VM_OP_RET             = 0xFC,
    VM_OP_SYS_INTEGRITY   = 0x33,
    VM_OP_SYS_HWID_CHECK  = 0x39,
    VM_OP_HALT            = 0xFE
};

// VM Flags
struct VmFlags {
    bool zero = false;
    bool carry = false;
    bool sign = false;
};

// Virtual Machine Execution Context
struct VmContext {
    uint64_t registers[16] = {};  // r0 (ret), r1-r13 (general), r14 (rfp), r15 (rsp)
    VmFlags flags;
    size_t ip = 0;               // Instruction pointer
    bool running = false;
    uint64_t cycleCount = 0;
    std::vector<uint64_t> stack;
    std::vector<uint8_t> memory;
};

/**
 * @brief Software Virtual Machine (Custom Bytecode Interpreter)
 * 
 * Executes proprietary bytecode inside an isolated, obfuscated virtualized
 * CPU environment. Bytecode instructions are decrypted on-the-fly, dispatch
 * tables are flattened with opaque predicates, and critical calculations
 * (licensing, integrity, key derivation) execute without native x86_64 assembly.
 */
class VirtualMachine {
public:
    VirtualMachine();
    ~VirtualMachine() = default;

    /**
     * @brief Executes bytecode in the virtual machine.
     * @param encryptedBytecode The bytecode buffer (encrypted with key)
     * @param decryptionKey The key to decrypt instructions dynamically
     * @param arg0 First argument passed in r1
     * @param arg1 Second argument passed in r2
     * @param arg2 Third argument passed in r3
     * @return uint64_t Return value from register r0
     */
    uint64_t Execute(const std::vector<uint8_t>& encryptedBytecode,
                     uint8_t decryptionKey = 0x5A,
                     uint64_t arg0 = 0,
                     uint64_t arg1 = 0,
                     uint64_t arg2 = 0);

    /**
     * @brief Builds encrypted bytecode for validating a hardware license.
     * In the VM, registers compare hardware hash against license token using
     * MBA subtraction and XOR, yielding 1 for authorized, 0 for unauthorized.
     */
    static std::vector<uint8_t> BuildLicenseValidationBytecode(uint8_t encryptionKey = 0x5A);

    /**
     * @brief Builds encrypted bytecode for calculating an anti-tamper signature
     * over a sequence of 64-bit blocks.
     */
    static std::vector<uint8_t> BuildIntegrityHashBytecode(uint8_t encryptionKey = 0x5A);

    /**
     * @brief Validates hardware license through VM bytecode execution.
     */
    static bool ValidateLicenseInVM(uint64_t currentHwidHash, uint64_t licenseHwidHash, uint64_t licenseExpiry);

    /**
     * @brief Evaluates memory integrity signature through VM bytecode execution.
     */
    static uint64_t EvaluateIntegrityInVM(const uint8_t* buffer, size_t size, uint64_t salt);

private:
    void Reset(uint64_t arg0, uint64_t arg1, uint64_t arg2);
    bool Step(const uint8_t* bytecode, size_t bytecodeSize, uint8_t decryptionKey);

    VmContext m_ctx;
};

} // namespace EngineSecurity
