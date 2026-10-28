#pragma once

#include "NetworkTypes.h"
#include "TlsOptions.h"
#include <memory>
#include <string>
#include <vector>

namespace EngineNetwork {

class QuicTransport;
class NetworkSocket {
public:
    NetworkSocket();
    ~NetworkSocket();

    NetworkSocket(const NetworkSocket&)=delete;
    NetworkSocket& operator=(const NetworkSocket&)=delete;
    static bool GlobalInitialize();
    static void GlobalShutdown();

    bool Open(uint16_t port = 0, bool nonBlocking = true);
    bool OpenTls(const TlsOptions& options,bool server,uint16_t port,const NetworkAddress& remote=NetworkAddress());
    bool IsTls()const{return bool(m_tls);}
    std::string GetError()const;
    void ClosePeer(const NetworkAddress& address);
    bool IsPeerConnected(const NetworkAddress& address)const;
    void AcceptPeer(const NetworkAddress& address);
    void Close();
    bool IsOpen() const { return m_socket != INVALID_SOCKET || bool(m_tls); }

    bool Send(const NetworkAddress& destination, const void* data, size_t size);
    bool Receive(NetworkAddress& outSender, void* outBuffer, size_t maxBytes, size_t& outBytesRead);

    uint16_t GetBoundPort() const { return m_boundPort; }

private:
    std::shared_ptr<QuicTransport> m_tls;
    std::string m_error;
    SOCKET m_socket = INVALID_SOCKET;
    uint16_t m_boundPort = 0;
    static bool s_wsaInitialized;
};

} // namespace EngineNetwork
