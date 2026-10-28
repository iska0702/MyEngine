#include "VirtualMachine.h"
#include "MBAObfuscator.h"
#include <chrono>
#include <cstring>

namespace EngineSecurity {

VirtualMachine::VirtualMachine() {
    m_ctx.stack.reserve(4096);
    m_ctx.memory.resize(65536, 0);
}

void VirtualMachine::Reset(uint64_t arg0, uint64_t arg1, uint64_t arg2) {
    for (int i = 0; i < 16; ++i) {
        m_ctx.registers[i] = 0;
    }
    m_ctx.registers[1] = arg0;
    m_ctx.registers[2] = arg1;
    m_ctx.registers[3] = arg2;
    m_ctx.flags = {};
    m_ctx.ip = 0;
    m_ctx.running = true;
    m_ctx.cycleCount = 0;
    m_ctx.stack.clear();
}

static inline uint8_t DecryptByte(uint8_t encByte, size_t ip, uint8_t key) {
    uint8_t rollingKey = (uint8_t)((key + (ip * 13)) & 0xFF);
    return (uint8_t)MBAObfuscator::Xor((uint64_t)encByte, (uint64_t)rollingKey);
}

static inline uint8_t EncryptByte(uint8_t rawByte, size_t ip, uint8_t key) {
    uint8_t rollingKey = (uint8_t)((key + (ip * 13)) & 0xFF);
    return (uint8_t)MBAObfuscator::Xor((uint64_t)rawByte, (uint64_t)rollingKey);
}

uint64_t VirtualMachine::Execute(const std::vector<uint8_t>& encryptedBytecode,
                                 uint8_t decryptionKey,
                                 uint64_t arg0,
                                 uint64_t arg1,
                                 uint64_t arg2) {
    if (encryptedBytecode.empty()) return 0;

    Reset(arg0, arg1, arg2);

    const size_t size = encryptedBytecode.size();
    const uint8_t* code = encryptedBytecode.data();

    const uint64_t maxCycles = 1000000;

    while (m_ctx.running && m_ctx.ip < size && m_ctx.cycleCount < maxCycles) {
        m_ctx.cycleCount++;

        // Opaque Predicate: (x * (x + 1)) is always even. Bit 0 is always 0.
        volatile uint64_t opq = m_ctx.cycleCount;
        if (((opq * (opq + 1)) & 1) != 0) {
            // Bogus code never executed, confuses symbolic analyzers
            m_ctx.registers[0] ^= 0x55AA55AA55AA55AAULL;
        }

        if (!Step(code, size, decryptionKey)) {
            break;
        }
    }

    return m_ctx.registers[0];
}

bool VirtualMachine::Step(const uint8_t* bytecode, size_t bytecodeSize, uint8_t decryptionKey) {
    if (m_ctx.ip >= bytecodeSize) {
        m_ctx.running = false;
        return false;
    }

    uint8_t opByte = DecryptByte(bytecode[m_ctx.ip], m_ctx.ip, decryptionKey);
    m_ctx.ip++;

    switch (opByte) {
        case VM_OP_NOP: {
            break;
        }

        case VM_OP_HALT: {
            m_ctx.running = false;
            return false;
        }

        case VM_OP_MOV_IMM: {
            if (m_ctx.ip + 9 > bytecodeSize) return false;
            uint8_t regByte = DecryptByte(bytecode[m_ctx.ip], m_ctx.ip, decryptionKey);
            m_ctx.ip++;
            uint8_t dst = (regByte >> 4) & 0x0F;

            uint64_t imm = 0;
            for (size_t b = 0; b < 8; ++b) {
                uint8_t ib = DecryptByte(bytecode[m_ctx.ip + b], m_ctx.ip + b, decryptionKey);
                imm |= ((uint64_t)ib << (b * 8));
            }
            m_ctx.ip += 8;

            m_ctx.registers[dst] = imm;
            break;
        }

        case VM_OP_MOV_REG: {
            if (m_ctx.ip + 1 > bytecodeSize) return false;
            uint8_t regByte = DecryptByte(bytecode[m_ctx.ip], m_ctx.ip, decryptionKey);
            m_ctx.ip++;
            uint8_t dst = (regByte >> 4) & 0x0F;
            uint8_t src = regByte & 0x0F;
            m_ctx.registers[dst] = m_ctx.registers[src];
            break;
        }

        case VM_OP_PUSH: {
            if (m_ctx.ip + 1 > bytecodeSize) return false;
            uint8_t regByte = DecryptByte(bytecode[m_ctx.ip], m_ctx.ip, decryptionKey);
            m_ctx.ip++;
            uint8_t reg = (regByte >> 4) & 0x0F;
            m_ctx.stack.push_back(m_ctx.registers[reg]);
            break;
        }

        case VM_OP_POP: {
            if (m_ctx.ip + 1 > bytecodeSize) return false;
            uint8_t regByte = DecryptByte(bytecode[m_ctx.ip], m_ctx.ip, decryptionKey);
            m_ctx.ip++;
            uint8_t reg = (regByte >> 4) & 0x0F;
            if (!m_ctx.stack.empty()) {
                m_ctx.registers[reg] = m_ctx.stack.back();
                m_ctx.stack.pop_back();
            }
            break;
        }

        case VM_OP_ADD: {
            if (m_ctx.ip + 1 > bytecodeSize) return false;
            uint8_t regByte = DecryptByte(bytecode[m_ctx.ip], m_ctx.ip, decryptionKey);
            m_ctx.ip++;
            uint8_t dst = (regByte >> 4) & 0x0F;
            uint8_t src = regByte & 0x0F;
            m_ctx.registers[dst] += m_ctx.registers[src];
            break;
        }

        case VM_OP_SUB: {
            if (m_ctx.ip + 1 > bytecodeSize) return false;
            uint8_t regByte = DecryptByte(bytecode[m_ctx.ip], m_ctx.ip, decryptionKey);
            m_ctx.ip++;
            uint8_t dst = (regByte >> 4) & 0x0F;
            uint8_t src = regByte & 0x0F;
            m_ctx.registers[dst] -= m_ctx.registers[src];
            break;
        }

        case VM_OP_MBA_ADD: {
            if (m_ctx.ip + 1 > bytecodeSize) return false;
            uint8_t regByte = DecryptByte(bytecode[m_ctx.ip], m_ctx.ip, decryptionKey);
            m_ctx.ip++;
            uint8_t dst = (regByte >> 4) & 0x0F;
            uint8_t src = regByte & 0x0F;
            m_ctx.registers[dst] = MBAObfuscator::AddComplex(m_ctx.registers[dst], m_ctx.registers[src]);
            break;
        }

        case VM_OP_MBA_SUB: {
            if (m_ctx.ip + 1 > bytecodeSize) return false;
            uint8_t regByte = DecryptByte(bytecode[m_ctx.ip], m_ctx.ip, decryptionKey);
            m_ctx.ip++;
            uint8_t dst = (regByte >> 4) & 0x0F;
            uint8_t src = regByte & 0x0F;
            m_ctx.registers[dst] = MBAObfuscator::Sub(m_ctx.registers[dst], m_ctx.registers[src]);
            break;
        }

        case VM_OP_MBA_XOR: {
            if (m_ctx.ip + 1 > bytecodeSize) return false;
            uint8_t regByte = DecryptByte(bytecode[m_ctx.ip], m_ctx.ip, decryptionKey);
            m_ctx.ip++;
            uint8_t dst = (regByte >> 4) & 0x0F;
            uint8_t src = regByte & 0x0F;
            m_ctx.registers[dst] = MBAObfuscator::Xor(m_ctx.registers[dst], m_ctx.registers[src]);
            break;
        }

        case VM_OP_XOR: {
            if (m_ctx.ip + 1 > bytecodeSize) return false;
            uint8_t regByte = DecryptByte(bytecode[m_ctx.ip], m_ctx.ip, decryptionKey);
            m_ctx.ip++;
            uint8_t dst = (regByte >> 4) & 0x0F;
            uint8_t src = regByte & 0x0F;
            m_ctx.registers[dst] ^= m_ctx.registers[src];
            break;
        }

        case VM_OP_AND: {
            if (m_ctx.ip + 1 > bytecodeSize) return false;
            uint8_t regByte = DecryptByte(bytecode[m_ctx.ip], m_ctx.ip, decryptionKey);
            m_ctx.ip++;
            uint8_t dst = (regByte >> 4) & 0x0F;
            uint8_t src = regByte & 0x0F;
            m_ctx.registers[dst] = MBAObfuscator::And(m_ctx.registers[dst], m_ctx.registers[src]);
            break;
        }

        case VM_OP_OR: {
            if (m_ctx.ip + 1 > bytecodeSize) return false;
            uint8_t regByte = DecryptByte(bytecode[m_ctx.ip], m_ctx.ip, decryptionKey);
            m_ctx.ip++;
            uint8_t dst = (regByte >> 4) & 0x0F;
            uint8_t src = regByte & 0x0F;
            m_ctx.registers[dst] = MBAObfuscator::Or(m_ctx.registers[dst], m_ctx.registers[src]);
            break;
        }

        case VM_OP_CMP: {
            if (m_ctx.ip + 1 > bytecodeSize) return false;
            uint8_t regByte = DecryptByte(bytecode[m_ctx.ip], m_ctx.ip, decryptionKey);
            m_ctx.ip++;
            uint8_t dst = (regByte >> 4) & 0x0F;
            uint8_t src = regByte & 0x0F;
            uint64_t a = m_ctx.registers[dst];
            uint64_t b = m_ctx.registers[src];

            m_ctx.flags.zero = MBAObfuscator::Equal(a, b);
            m_ctx.flags.carry = (a < b);
            m_ctx.flags.sign = ((int64_t)(a - b) < 0);
            break;
        }

        case VM_OP_JMP: {
            if (m_ctx.ip + 8 > bytecodeSize) return false;
            uint64_t target = 0;
            for (size_t b = 0; b < 8; ++b) {
                uint8_t ib = DecryptByte(bytecode[m_ctx.ip + b], m_ctx.ip + b, decryptionKey);
                target |= ((uint64_t)ib << (b * 8));
            }
            m_ctx.ip = (size_t)target;
            break;
        }

        case VM_OP_JZ: {
            if (m_ctx.ip + 8 > bytecodeSize) return false;
            uint64_t target = 0;
            for (size_t b = 0; b < 8; ++b) {
                uint8_t ib = DecryptByte(bytecode[m_ctx.ip + b], m_ctx.ip + b, decryptionKey);
                target |= ((uint64_t)ib << (b * 8));
            }
            m_ctx.ip += 8;
            if (m_ctx.flags.zero) {
                m_ctx.ip = (size_t)target;
            }
            break;
        }

        case VM_OP_JNZ: {
            if (m_ctx.ip + 8 > bytecodeSize) return false;
            uint64_t target = 0;
            for (size_t b = 0; b < 8; ++b) {
                uint8_t ib = DecryptByte(bytecode[m_ctx.ip + b], m_ctx.ip + b, decryptionKey);
                target |= ((uint64_t)ib << (b * 8));
            }
            m_ctx.ip += 8;
            if (!m_ctx.flags.zero) {
                m_ctx.ip = (size_t)target;
            }
            break;
        }

        case VM_OP_CALL: {
            if (m_ctx.ip + 8 > bytecodeSize) return false;
            uint64_t target = 0;
            for (size_t b = 0; b < 8; ++b) {
                uint8_t ib = DecryptByte(bytecode[m_ctx.ip + b], m_ctx.ip + b, decryptionKey);
                target |= ((uint64_t)ib << (b * 8));
            }
            m_ctx.ip += 8;
            m_ctx.stack.push_back((uint64_t)m_ctx.ip);
            m_ctx.ip = (size_t)target;
            break;
        }

        case VM_OP_RET: {
            if (m_ctx.stack.empty()) {
                m_ctx.running = false;
                return false;
            }
            m_ctx.ip = (size_t)m_ctx.stack.back();
            m_ctx.stack.pop_back();
            break;
        }

        case VM_OP_SYS_INTEGRITY: {
            // Evaluates MBA hash of r1 with salt r2 -> stores result in r0
            uint64_t val = m_ctx.registers[1];
            uint64_t salt = m_ctx.registers[2];
            uint64_t mix = MBAObfuscator::Xor(val, salt);
            m_ctx.registers[0] = MBAObfuscator::RollingHash((const uint8_t*)&mix, sizeof(mix));
            break;
        }

        case VM_OP_SYS_HWID_CHECK: {
            // r1 = current HWID hash
            // r2 = license HWID hash
            // r3 = license expiry timestamp (0 = lifetime)
            // r0 = 1 if valid, 0 if invalid
            uint64_t currentHwid = m_ctx.registers[1];
            uint64_t licenseHwid = m_ctx.registers[2];
            uint64_t expiry = m_ctx.registers[3];

            bool isUniversal = (licenseHwid == 0 || licenseHwid == 0xFFFFFFFFFFFFFFFFULL);
            bool hwidMatch = isUniversal || MBAObfuscator::Equal(currentHwid, licenseHwid);
            bool timeValid = true;

            if (expiry != 0) {
                uint64_t nowSec = (uint64_t)std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                if (nowSec > expiry) {
                    timeValid = false;
                }
            }

            m_ctx.registers[0] = (hwidMatch && timeValid) ? 1ULL : 0ULL;
            break;
        }

        default:
            // Illegal or tampered opcode -> terminate VM
            m_ctx.running = false;
            return false;
    }

    return true;
}

static void EmitByte(std::vector<uint8_t>& stream, uint8_t b, uint8_t key) {
    size_t ip = stream.size();
    stream.push_back(EncryptByte(b, ip, key));
}

static void EmitImm64(std::vector<uint8_t>& stream, uint64_t val, uint8_t key) {
    for (size_t i = 0; i < 8; ++i) {
        uint8_t b = (uint8_t)((val >> (i * 8)) & 0xFF);
        EmitByte(stream, b, key);
    }
}

std::vector<uint8_t> VirtualMachine::BuildLicenseValidationBytecode(uint8_t encryptionKey) {
    std::vector<uint8_t> code;

    // 1. VM_OP_SYS_HWID_CHECK (compares r1, r2, r3 and writes verdict into r0)
    EmitByte(code, VM_OP_SYS_HWID_CHECK, encryptionKey);

    // 2. VM_OP_HALT
    EmitByte(code, VM_OP_HALT, encryptionKey);

    return code;
}

std::vector<uint8_t> VirtualMachine::BuildIntegrityHashBytecode(uint8_t encryptionKey) {
    std::vector<uint8_t> code;

    // 1. VM_OP_SYS_INTEGRITY
    EmitByte(code, VM_OP_SYS_INTEGRITY, encryptionKey);

    // 2. VM_OP_HALT
    EmitByte(code, VM_OP_HALT, encryptionKey);

    return code;
}

bool VirtualMachine::ValidateLicenseInVM(uint64_t currentHwidHash, uint64_t licenseHwidHash, uint64_t licenseExpiry) {
    const uint8_t vmKey = 0xA7;
    std::vector<uint8_t> bytecode = BuildLicenseValidationBytecode(vmKey);

    VirtualMachine vm;
    uint64_t result = vm.Execute(bytecode, vmKey, currentHwidHash, licenseHwidHash, licenseExpiry);
    return (result == 1ULL);
}

uint64_t VirtualMachine::EvaluateIntegrityInVM(const uint8_t* buffer, size_t size, uint64_t salt) {
    if (!buffer || size == 0) return 0;

    const uint8_t vmKey = 0x3D;
    std::vector<uint8_t> bytecode = BuildIntegrityHashBytecode(vmKey);

    uint64_t initialFnv = MBAObfuscator::FNV1a64(buffer, size);

    VirtualMachine vm;
    return vm.Execute(bytecode, vmKey, initialFnv, salt, 0);
}

} // namespace EngineSecurity
