#pragma once
#include <string>
namespace EngineNetwork {
struct TlsOptions {
    bool enabled=false;
    std::string serverName; // Certificate DNS name, independent of the transport IP.
    std::string certificatePfx;
    std::string certificatePassword; // Prefer an environment variable at the server CLI.
    std::string caCertificate; // Optional private CA PEM bundle, scoped to this connection.
};
}
