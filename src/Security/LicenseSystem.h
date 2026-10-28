#pragma once

#include <string>
#include <cstdint>
#include <vector>

namespace EngineSecurity {

enum class LicenseStatus {
    Valid = 0,
    MissingLicense,
    HardwareMismatch,
    Expired,
    SignatureTampered,
    CorruptedFormat
};

struct LicenseInfo {
    LicenseStatus status = LicenseStatus::MissingLicense;
    std::string boundHwid;
    std::string currentHwid;
    std::string productName;
    std::string licensee;
    uint64_t issueTimestamp = 0;
    uint64_t expiryTimestamp = 0;
    bool isPerpetual = true;
    std::string statusMessage;
};

/**
 * @brief Hardware-Bound License & Anti-Piracy Activation System.
 * 
 * Verifies that the current execution environment matches the cryptographic
 * hardware fingerprint sealed inside 'license.lic'. All critical validation
 * logic is processed through the Custom Virtual Machine (VM) and MBA arithmetic.
 */
class LicenseSystem {
public:
    static LicenseSystem& Get();

    /**
     * @brief Performs comprehensive startup validation of license.lic.
     * In Editor/Dev mode, auto-provisions a developer license if missing.
     * In Release mode, strictly requires valid signature and HWID match.
     */
    LicenseStatus ValidateLicense();

    /**
     * @brief Generates and writes a cryptographically signed license file.
     * @param targetHwid The HWID string to bind to (empty = current machine)
     * @param outputPath Target file path (default = "license.lic")
     * @param expiryTimestamp Expiry UNIX timestamp (0 = lifetime)
     */
    bool GenerateLicenseFile(const std::string& targetHwid = "",
                            const std::string& outputPath = "license.lic",
                            uint64_t expiryTimestamp = 0);

    /**
     * @brief Returns current license inspection details.
     */
    const LicenseInfo& GetLicenseInfo() const { return m_info; }

    /**
     * @brief Creates an activation request payload (base64 token) for server registration.
     */
    std::string CreateActivationRequest();

    /**
     * @brief Activates the engine with a signed activation token received from server.
     */
    bool ApplyActivationToken(const std::string& token);

private:
    LicenseSystem() = default;
    ~LicenseSystem() = default;

    LicenseSystem(const LicenseSystem&) = delete;
    LicenseSystem& operator=(const LicenseSystem&) = delete;

    bool ReadLicenseFile(const std::string& path, std::vector<uint8_t>& outData);
    bool WriteLicenseFile(const std::string& path, const std::vector<uint8_t>& data);
    bool ReadLicenseFromPak(const std::wstring& exeDir, std::vector<uint8_t>& outData);

    std::string ComputeSignature(const uint8_t* payload, size_t len);

    LicenseInfo m_info;
};

} // namespace EngineSecurity
