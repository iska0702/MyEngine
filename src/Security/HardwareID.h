#pragma once

#include <string>
#include <cstdint>

namespace EngineSecurity {

struct HardwareProfile {
    std::string cpuModel;
    std::string cpuVendor;
    std::string motherboardSerial;
    std::string biosUuid;
    std::string diskSerial;
    std::string primaryMac;
    std::string rawFingerprint;
};

/**
 * @brief Machine Hardware Fingerprinting & HWID Generation Engine.
 * 
 * Collects immutable hardware characteristics directly from processor
 * CPUID, SMBIOS firmware tables (Motherboard & BIOS UUID), physical drive
 * controller IOCTLs, and physical NIC MAC addresses.
 */
class HardwareID {
public:
    /**
     * @brief Generates cryptographically hashed HWID string formatted as:
     * "XXXX-XXXX-XXXX-XXXX-XXXX"
     */
    static std::string GetHWID();

    /**
     * @brief Returns 64-bit MBA-hashed HWID representation for fast in-VM validation.
     */
    static uint64_t GetHWIDHash64();

    /**
     * @brief Returns detailed diagnostic hardware profile.
     */
    static HardwareProfile GetProfile();

private:
    static std::string QueryCpuInfo();
    static void QuerySmbios(std::string& outUuid, std::string& outBoardSerial);
    static std::string QueryDiskSerial();
    static std::string QueryMacAddress();
};

} // namespace EngineSecurity
