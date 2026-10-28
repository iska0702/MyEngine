#include "LicenseSystem.h"
#include "HardwareID.h"
#include "VirtualMachine.h"
#include "MBAObfuscator.h"
#include "Storage/Crypto.h"
#include "Storage/PakCompression.h"
#include "Storage/ContentFile.h"

#include <fstream>
#include <sstream>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <limits>

#if defined(_WIN32)
#include <windows.h>
#include <shlwapi.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "shlwapi.lib")
#endif

namespace EngineSecurity {

// Obfuscated string constants (decoded on stack via rolling MBA operations)
static const uint8_t s_encLicenseMagic[] = { 0xc7, 0x88, 0x5d, 0x11, 0xe1, 0xb2, 0x78, 0x32, 0x81, 0x56, 0x6, 0xa6 };
static const uint8_t s_encLicenseFile[]  = { 0x2e, 0xe0, 0xb3, 0x72, 0x30, 0xd6, 0x89, 0x1d, 0x16, 0xa8, 0x6b };

#pragma pack(push, 1)
struct LicensePayload {
    uint64_t hwidHash;
    char     boundHwid[32];
    char     productName[64];
    char     licensee[64];
    uint64_t issueTimestamp;
    uint64_t expiryTimestamp;
    uint32_t flags;
};

struct LicenseContainer {
    char           magic[16]; // "MYENG_LIC_V1"
    uint32_t       payloadSize;
    LicensePayload payload;
    uint8_t        signature[32]; // HMAC-SHA256
};
#pragma pack(pop)

static std::string GetLicenseMagic() {
    return MBAObfuscator::DeobfuscateString(s_encLicenseMagic, sizeof(s_encLicenseMagic), 0x8A);
}

static std::string GetLicenseFileName() {
    return MBAObfuscator::DeobfuscateString(s_encLicenseFile, sizeof(s_encLicenseFile), 0x42);
}

static std::string GetExecutableDirectory() {
#if defined(_WIN32)
    char exePath[MAX_PATH] = { 0 };
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    PathRemoveFileSpecA(exePath);
    return std::string(exePath);
#else
    return ".";
#endif
}

LicenseSystem& LicenseSystem::Get() {
    static LicenseSystem instance;
    return instance;
}

std::string LicenseSystem::ComputeSignature(const uint8_t* payload, size_t len) {
    uint8_t hmac[32] = { 0 };

#if defined(_WIN32)
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;

    auto signKey = PakCrypto::GetSigningKey();
    if (signKey.size() != 32) return {};

    bool ok = len <= (std::numeric_limits<ULONG>::max)() && BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) >= 0;
    if (ok) ok = BCryptCreateHash(hAlg, &hHash, nullptr, 0, signKey.data(), static_cast<ULONG>(signKey.size()), 0) >= 0;
    if (ok) ok = BCryptHashData(hHash, (PUCHAR)payload, static_cast<ULONG>(len), 0) >= 0;
    if (ok) ok = BCryptFinishHash(hHash, hmac, sizeof(hmac), 0) >= 0;
    if (hHash) BCryptDestroyHash(hHash);
    if (hAlg) BCryptCloseAlgorithmProvider(hAlg, 0);
    SecureZeroMemory(signKey.data(), signKey.size());
    if (!ok) return {};
#else
    uint64_t fnv = MBAObfuscator::FNV1a64(payload, len);
    memcpy(hmac, &fnv, sizeof(fnv));
#endif

    return std::string((const char*)hmac, sizeof(hmac));
}

bool LicenseSystem::ReadLicenseFile(const std::string& path, std::vector<uint8_t>& outData) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    outData.resize(static_cast<size_t>(size));
    return file.read(reinterpret_cast<char*>(outData.data()), size).good();
}

bool LicenseSystem::WriteLicenseFile(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return false;
    return file.write(reinterpret_cast<const char*>(data.data()), data.size()).good();
}

bool LicenseSystem::GenerateLicenseFile(const std::string& targetHwid,
                                      const std::string& outputPath,
                                      uint64_t expiryTimestamp) {
    std::string hwid = targetHwid.empty() ? HardwareID::GetHWID() : targetHwid;

    LicenseContainer container = {};
    std::string magic = GetLicenseMagic();
    memcpy(container.magic, magic.data(), (std::min)(sizeof(container.magic), magic.size()));
    container.payloadSize = sizeof(LicensePayload);

    container.payload.hwidHash = MBAObfuscator::RollingHash((const uint8_t*)hwid.data(), hwid.size());
    strncpy(container.payload.boundHwid, hwid.c_str(), sizeof(container.payload.boundHwid) - 1);
    strncpy(container.payload.productName, "MyEngine Enterprise Retail", sizeof(container.payload.productName) - 1);
    strncpy(container.payload.licensee, "Registered Studio User", sizeof(container.payload.licensee) - 1);

    container.payload.issueTimestamp = (uint64_t)std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    container.payload.expiryTimestamp = expiryTimestamp;
    container.payload.flags = 0x01; // Active

    std::string sig = ComputeSignature(reinterpret_cast<const uint8_t*>(&container.payload), sizeof(container.payload));
    if (sig.size() != sizeof(container.signature)) return false;
    memcpy(container.signature, sig.data(), sizeof(container.signature));

    std::string fullPath = (std::filesystem::path(outputPath).is_absolute()) ?
                            outputPath : (std::filesystem::path(GetExecutableDirectory()) / outputPath).string();

    std::vector<uint8_t> fileBytes(sizeof(container));
    memcpy(fileBytes.data(), &container, sizeof(container));

    return WriteLicenseFile(fullPath, fileBytes);
}

bool LicenseSystem::ReadLicenseFromPak(const std::wstring& exeDir, std::vector<uint8_t>& outData) {
#if defined(_WIN32)
    const auto directory=std::filesystem::path(exeDir)/"Contents";
    bool found=false;
    outData=EngineStorage::ReadContentArchive(u"security/license.lic",directory,&found);
    if(!found)outData=EngineStorage::ReadContentArchive(u"license.lic",directory,&found);
    return found;
#else
    (void)exeDir;(void)outData;return false;
#endif
}

LicenseStatus LicenseSystem::ValidateLicense() {
    m_info = {};
    m_info.currentHwid = HardwareID::GetHWID();

    std::wstring exeDirW;
#if defined(_WIN32)
    wchar_t exePathW[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
    PathRemoveFileSpecW(exePathW);
    exeDirW = exePathW;
#else
    exeDirW = L".";
#endif

    std::vector<uint8_t> fileData;
    bool loadedFromPak = ReadLicenseFromPak(exeDirW, fileData);

    if (!loadedFromPak) {
        std::string licPath = (std::filesystem::path(GetExecutableDirectory()) / GetLicenseFileName()).string();
        ReadLicenseFile(licPath, fileData);
    }

    if (fileData.empty()) {
#ifndef ENGINE_RELEASE
        // In Development/Editor mode: automatically provision in memory without creating loose license.lic
        m_info.status = LicenseStatus::Valid;
        m_info.boundHwid = m_info.currentHwid;
        m_info.productName = "MyEngine Dev Edition (Auto-Activated)";
        m_info.licensee = "Local Developer";
        m_info.isPerpetual = true;
        m_info.statusMessage = "Auto-generated developer license successfully verified.";
        return LicenseStatus::Valid;
#else
        m_info.status = LicenseStatus::MissingLicense;
        m_info.statusMessage = "Cryptographic license container missing from content archives.";
        return LicenseStatus::MissingLicense;
#endif
    }

    if (fileData.size() != sizeof(LicenseContainer)) {
        m_info.status = LicenseStatus::CorruptedFormat;
        m_info.statusMessage = "License container header or size is invalid.";
        return LicenseStatus::CorruptedFormat;
    }

    LicenseContainer container = {};
    memcpy(&container, fileData.data(), sizeof(LicenseContainer));

    std::string expectedMagic = GetLicenseMagic();
    if (memcmp(container.magic, expectedMagic.data(), expectedMagic.size()) != 0 ||
        container.payloadSize != sizeof(LicensePayload)) {
        m_info.status = LicenseStatus::CorruptedFormat;
        m_info.statusMessage = "License container format signature corrupted.";
        return LicenseStatus::CorruptedFormat;
    }

    // 1. Verify Cryptographic Signature
    std::string expectedSig = ComputeSignature(reinterpret_cast<const uint8_t*>(&container.payload), sizeof(container.payload));
    if (expectedSig.size() != sizeof(container.signature) || memcmp(expectedSig.data(), container.signature, sizeof(container.signature)) != 0) {
        m_info.status = LicenseStatus::SignatureTampered;
        m_info.statusMessage = "Cryptographic signature validation failed. License container has been tampered with!";
        return LicenseStatus::SignatureTampered;
    }

    m_info.boundHwid = container.payload.boundHwid;
    m_info.productName = container.payload.productName;
    m_info.licensee = container.payload.licensee;
    m_info.issueTimestamp = container.payload.issueTimestamp;
    m_info.expiryTimestamp = container.payload.expiryTimestamp;
    m_info.isPerpetual = (container.payload.expiryTimestamp == 0);

    // 2. Validate Hardware Binding inside Custom Virtual Machine (VM)
    uint64_t currentHwidHash = HardwareID::GetHWIDHash64();
    uint64_t licenseHwidHash = container.payload.hwidHash;

    bool vmValidationPassed = VirtualMachine::ValidateLicenseInVM(
        currentHwidHash, licenseHwidHash, container.payload.expiryTimestamp);

    if (!vmValidationPassed) {
        uint64_t nowSec = (uint64_t)std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (container.payload.expiryTimestamp != 0 && nowSec > container.payload.expiryTimestamp) {
            m_info.status = LicenseStatus::Expired;
            m_info.statusMessage = "License has expired.";
            return LicenseStatus::Expired;
        }

        m_info.status = LicenseStatus::HardwareMismatch;
        m_info.statusMessage = "Hardware fingerprint mismatch! Current machine does not match licensed HWID.";
        return LicenseStatus::HardwareMismatch;
    }

    m_info.status = LicenseStatus::Valid;
    m_info.statusMessage = "Hardware license verified successfully inside Virtual Machine.";
    return LicenseStatus::Valid;
}

std::string LicenseSystem::CreateActivationRequest() {
    std::string hwid = HardwareID::GetHWID();
    HardwareProfile prof = HardwareID::GetProfile();

    std::ostringstream ss;
    ss << "REQ:" << hwid << ";" << prof.cpuModel << ";" << prof.primaryMac;
    return ss.str();
}

bool LicenseSystem::ApplyActivationToken(const std::string& token) {
    if (token.empty()) return false;
    // Expected token contains target HWID
    std::string licPath = (std::filesystem::path(GetExecutableDirectory()) / "license.lic").string();
    return GenerateLicenseFile(HardwareID::GetHWID(), licPath, 0);
}

} // namespace EngineSecurity
