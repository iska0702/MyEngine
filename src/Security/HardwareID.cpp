#include "HardwareID.h"
#include "MBAObfuscator.h"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <intrin.h>
#include <iphlpapi.h>
#include <bcrypt.h>
#include <winioctl.h>
#include <vector>
#include <sstream>
#include <iomanip>
#include <algorithm>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "bcrypt.lib")

namespace EngineSecurity {

// SMBIOS structures
#pragma pack(push, 1)
struct RawSMBIOSData {
    uint8_t  Used20CallingMethod;
    uint8_t  SMBIOSMajorVersion;
    uint8_t  SMBIOSMinorVersion;
    uint8_t  DmiRevision;
    uint32_t Length;
    uint8_t  SMBIOSTableData[1];
};

struct SMBIOSHeader {
    uint8_t Type;
    uint8_t Length;
    uint16_t Handle;
};
#pragma pack(pop)

std::string HardwareID::QueryCpuInfo() {
    int cpuInfo[4] = { 0 };
    __cpuid(cpuInfo, 0);

    char vendor[13] = { 0 };
    *reinterpret_cast<int*>(vendor) = cpuInfo[1];
    *reinterpret_cast<int*>(vendor + 4) = cpuInfo[3];
    *reinterpret_cast<int*>(vendor + 8) = cpuInfo[2];

    __cpuid(cpuInfo, 1);
    uint32_t signature = static_cast<uint32_t>(cpuInfo[0]);
    uint32_t features = static_cast<uint32_t>(cpuInfo[3]);

    char brand[49] = { 0 };
    __cpuid(cpuInfo, 0x80000000);
    if (static_cast<uint32_t>(cpuInfo[0]) >= 0x80000004) {
        __cpuid(reinterpret_cast<int*>(brand), 0x80000002);
        __cpuid(reinterpret_cast<int*>(brand + 16), 0x80000003);
        __cpuid(reinterpret_cast<int*>(brand + 32), 0x80000004);
    }

    std::ostringstream ss;
    ss << vendor << "_" << std::hex << signature << "_" << features << "_" << brand;
    return ss.str();
}

static const char* GetSMBIOSString(const uint8_t* pHeader, uint8_t length, uint8_t stringIndex) {
    if (stringIndex == 0) return "";
    const char* str = reinterpret_cast<const char*>(pHeader + length);
    uint8_t idx = 1;
    while (*str) {
        if (idx == stringIndex) return str;
        str += strlen(str) + 1;
        idx++;
    }
    return "";
}

void HardwareID::QuerySmbios(std::string& outUuid, std::string& outBoardSerial) {
    outUuid = "00000000-0000-0000-0000-000000000000";
    outBoardSerial = "UNKNOWN_BOARD";

    DWORD size = GetSystemFirmwareTable('RSMB', 0, nullptr, 0);
    if (size == 0) return;

    std::vector<uint8_t> buffer(size);
    if (GetSystemFirmwareTable('RSMB', 0, buffer.data(), size) == 0) return;

    RawSMBIOSData* smbios = reinterpret_cast<RawSMBIOSData*>(buffer.data());
    const uint8_t* pData = smbios->SMBIOSTableData;
    const uint8_t* pEnd = buffer.data() + size;

    while (pData + sizeof(SMBIOSHeader) <= pEnd) {
        const SMBIOSHeader* header = reinterpret_cast<const SMBIOSHeader*>(pData);
        if (header->Length == 0) break;

        // Type 1: System Information (UUID)
        if (header->Type == 1 && header->Length >= 0x19) {
            const uint8_t* pUuid = pData + 0x08;
            char uuidStr[64] = { 0 };
            snprintf(uuidStr, sizeof(uuidStr),
                     "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
                     pUuid[0], pUuid[1], pUuid[2], pUuid[3],
                     pUuid[4], pUuid[5], pUuid[6], pUuid[7],
                     pUuid[8], pUuid[9], pUuid[10], pUuid[11],
                     pUuid[12], pUuid[13], pUuid[14], pUuid[15]);
            outUuid = uuidStr;
        }

        // Type 2: Baseboard / Motherboard Information (Serial Number)
        if (header->Type == 2 && header->Length >= 0x08) {
            uint8_t serialIdx = pData[0x07];
            const char* s = GetSMBIOSString(pData, header->Length, serialIdx);
            if (s && strlen(s) > 0) {
                outBoardSerial = s;
            }
        }

        // Advance to next SMBIOS record (skip strings section ending in double-null)
        const uint8_t* next = pData + header->Length;
        while (next + 1 < pEnd && !(next[0] == 0 && next[1] == 0)) {
            next++;
        }
        pData = next + 2;
    }
}

std::string HardwareID::QueryDiskSerial() {
    HANDLE hDrive = CreateFileW(L"\\\\.\\PhysicalDrive0", 0,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_EXISTING, 0, nullptr);
    if (hDrive != INVALID_HANDLE_VALUE) {
        STORAGE_PROPERTY_QUERY query = {};
        query.PropertyId = StorageDeviceProperty;
        query.QueryType = PropertyStandardQuery;

        uint8_t descBuf[1024] = { 0 };
        DWORD bytesReturned = 0;
        if (DeviceIoControl(hDrive, IOCTL_STORAGE_QUERY_PROPERTY,
                            &query, sizeof(query),
                            descBuf, sizeof(descBuf),
                            &bytesReturned, nullptr)) {
            STORAGE_DEVICE_DESCRIPTOR* desc = reinterpret_cast<STORAGE_DEVICE_DESCRIPTOR*>(descBuf);
            if (desc->SerialNumberOffset != 0 && desc->SerialNumberOffset < bytesReturned) {
                const char* serial = reinterpret_cast<const char*>(descBuf + desc->SerialNumberOffset);
                CloseHandle(hDrive);
                std::string s = serial;
                s.erase(std::remove_if(s.begin(), s.end(), ::isspace), s.end());
                if (!s.empty()) return s;
            }
        }
        CloseHandle(hDrive);
    }

    // Fallback: Volume serial number of C: drive
    DWORD volumeSerial = 0;
    if (GetVolumeInformationW(L"C:\\", nullptr, 0, &volumeSerial, nullptr, nullptr, nullptr, 0)) {
        char buf[32] = { 0 };
        snprintf(buf, sizeof(buf), "VOL_%08X", volumeSerial);
        return buf;
    }

    return "DEFAULT_DISK_SERIAL";
}

std::string HardwareID::QueryMacAddress() {
    ULONG outBufLen = 15000;
    std::vector<uint8_t> buffer(outBufLen);
    PIP_ADAPTER_ADDRESSES pAddresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());

    DWORD ret = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, pAddresses, &outBufLen);
    if (ret == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(outBufLen);
        pAddresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        ret = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, pAddresses, &outBufLen);
    }

    if (ret == NO_ERROR) {
        for (PIP_ADAPTER_ADDRESSES pCurr = pAddresses; pCurr != nullptr; pCurr = pCurr->Next) {
            // Filter out loopback and non-Ethernet/Wi-Fi
            if (pCurr->IfType == IF_TYPE_ETHERNET_CSMACD || pCurr->IfType == IF_TYPE_IEEE80211) {
                if (pCurr->PhysicalAddressLength == 6) {
                    char mac[32] = { 0 };
                    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                             pCurr->PhysicalAddress[0], pCurr->PhysicalAddress[1],
                             pCurr->PhysicalAddress[2], pCurr->PhysicalAddress[3],
                             pCurr->PhysicalAddress[4], pCurr->PhysicalAddress[5]);
                    return mac;
                }
            }
        }
    }

    return "00:00:00:00:00:00";
}

HardwareProfile HardwareID::GetProfile() {
    HardwareProfile prof;
    prof.cpuModel = QueryCpuInfo();
    QuerySmbios(prof.biosUuid, prof.motherboardSerial);
    prof.diskSerial = QueryDiskSerial();
    prof.primaryMac = QueryMacAddress();

    prof.rawFingerprint = prof.cpuModel + "|" +
                          prof.biosUuid + "|" +
                          prof.motherboardSerial + "|" +
                          prof.diskSerial + "|" +
                          prof.primaryMac;
    return prof;
}

std::string HardwareID::GetHWID() {
    HardwareProfile prof = GetProfile();
    const std::string& raw = prof.rawFingerprint;

    // Cryptographic SHA-256 hash using BCrypt
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    uint8_t hash[32] = { 0 };

    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0) {
        if (BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0) >= 0) {
            BCryptHashData(hHash, (PUCHAR)raw.data(), (ULONG)raw.size(), 0);
            BCryptFinishHash(hHash, hash, sizeof(hash), 0);
            BCryptDestroyHash(hHash);
        }
        BCryptCloseAlgorithmProvider(hAlg, 0);
    } else {
        // Software fallback
        uint64_t fnv = MBAObfuscator::FNV1a64((const uint8_t*)raw.data(), raw.size());
        memcpy(hash, &fnv, sizeof(fnv));
    }

    // Format as 5 chunks of 4 hex uppercase characters: "XXXX-XXXX-XXXX-XXXX-XXXX"
    char formatted[32] = { 0 };
    snprintf(formatted, sizeof(formatted), "%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X",
             hash[0], hash[1], hash[2], hash[3],
             hash[4], hash[5], hash[6], hash[7],
             hash[8], hash[9]);

    return std::string(formatted);
}

uint64_t HardwareID::GetHWIDHash64() {
    std::string hwid = GetHWID();
    return MBAObfuscator::RollingHash((const uint8_t*)hwid.data(), hwid.size());
}

} // namespace EngineSecurity

#else

namespace EngineSecurity {
std::string HardwareID::GetHWID() { return "GENERIC-0000-0000-0000-0000"; }
uint64_t HardwareID::GetHWIDHash64() { return 0x123456789ABCDEF0ULL; }
HardwareProfile HardwareID::GetProfile() { return {}; }
}

#endif
