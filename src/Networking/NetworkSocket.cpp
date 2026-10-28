#include "NetworkSocket.h"
#include <iostream>
#include <cstring>
#include "QuicTransport.inl"

#ifdef _WIN32
#pragma comment(lib, "ws2_32.lib")
#endif

namespace EngineNetwork {

bool NetworkSocket::s_wsaInitialized = false;

NetworkSocket::NetworkSocket() {}

NetworkSocket::~NetworkSocket() {
    Close();
}

bool NetworkSocket::GlobalInitialize() {
#ifdef _WIN32
    if (s_wsaInitialized) return true;
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        return false;
    }
    s_wsaInitialized = true;
    return true;
#else
    return true;
#endif
}

void NetworkSocket::GlobalShutdown() {
#ifdef _WIN32
    if (s_wsaInitialized) {
        WSACleanup();
        s_wsaInitialized = false;
    }
#endif
}

bool NetworkSocket::Open(uint16_t port, bool nonBlocking) {
    if (!GlobalInitialize()) return false;
    Close();

    m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_socket == INVALID_SOCKET) {
        return false;
    }

    // Exclusive UDP binding prevents another local process from stealing packets.
#ifdef _WIN32
    BOOL reuse = TRUE;
    if(setsockopt(m_socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&reuse), sizeof(reuse))!=0){Close();return false;}
#else
    // POSIX sockets are exclusive unless address sharing is explicitly enabled.
#endif

    // Increase send/recv socket buffer sizes for reliable networking
    int bufSize = 1024 * 512; // 512 KB
#ifdef _WIN32
    setsockopt(m_socket, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&bufSize), sizeof(bufSize));
    setsockopt(m_socket, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&bufSize), sizeof(bufSize));
#else
    setsockopt(m_socket, SOL_SOCKET, SO_RCVBUF, &bufSize, sizeof(bufSize));
    setsockopt(m_socket, SOL_SOCKET, SO_SNDBUF, &bufSize, sizeof(bufSize));
#endif

    // Set non-blocking mode
    if (nonBlocking) {
#ifdef _WIN32
        u_long mode = 1;
        if (ioctlsocket(m_socket, FIONBIO, &mode) != 0) {
            Close();
            return false;
        }
#else
        int flags = fcntl(m_socket, F_GETFL, 0);
        if (flags < 0 || fcntl(m_socket, F_SETFL, flags | O_NONBLOCK) < 0) {
            Close();
            return false;
        }
#endif
    }

    // Bind socket
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(m_socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
        Close();
        return false;
    }

    if (port == 0) {
        sockaddr_in boundAddr = {};
#ifdef _WIN32
        int len = sizeof(boundAddr);
#else
        socklen_t len = sizeof(boundAddr);
#endif
        if (getsockname(m_socket, reinterpret_cast<sockaddr*>(&boundAddr), &len) == 0) {
            m_boundPort = ntohs(boundAddr.sin_port);
        }
    } else {
        m_boundPort = port;
    }

    return true;
}

bool NetworkSocket::OpenTls(const TlsOptions& options,bool server,uint16_t port,const NetworkAddress& remote){
    Close();m_error.clear();
#ifdef _WIN32
    if(!GlobalInitialize())return false;
    auto transport=std::make_shared<QuicTransport>();
    if(!transport->Open(options,server,port,remote)){m_error=transport->Error();return false;}
    m_boundPort=transport->Port();m_tls=std::move(transport);return true;
#else
    m_error="TLS/QUIC runtime is not available on this platform build";return false;
#endif
}
std::string NetworkSocket::GetError()const{
#ifdef _WIN32
    if(m_tls)return m_tls->Error();
#endif
    return m_error;
}
void NetworkSocket::AcceptPeer(const NetworkAddress& address){
#ifdef _WIN32
    if(m_tls)m_tls->AcceptPeer(address);
#endif
}
bool NetworkSocket::IsPeerConnected(const NetworkAddress& address)const{
#ifdef _WIN32
    if(m_tls)return m_tls->IsPeerConnected(address);
#endif
    return IsOpen();
}
void NetworkSocket::ClosePeer(const NetworkAddress& address){
#ifdef _WIN32
    if(m_tls)m_tls->ClosePeer(address);
#endif
}
void NetworkSocket::Close() {
    m_tls.reset();m_boundPort=0;
    if (m_socket != INVALID_SOCKET) {
#ifdef _WIN32
        closesocket(m_socket);
#else
        close(m_socket);
#endif
        m_socket = INVALID_SOCKET;
        m_boundPort = 0;
    }
}

bool NetworkSocket::Send(const NetworkAddress& destination, const void* data, size_t size) {
#ifdef _WIN32
    if(m_tls)return m_tls->Send(destination,data,size);
#endif
    if (m_socket == INVALID_SOCKET || !data || size == 0 || size>MAX_PACKET_SIZE) return false;

#ifdef _WIN32
    int sent = sendto(
        m_socket,
        static_cast<const char*>(data),
        static_cast<int>(size),
        0,
        reinterpret_cast<const sockaddr*>(&destination.socketAddress),
        sizeof(destination.socketAddress)
    );
#else
    ssize_t sent = sendto(
        m_socket,
        data,
        size,
        0,
        reinterpret_cast<const sockaddr*>(&destination.socketAddress),
        sizeof(destination.socketAddress)
    );
#endif

    return sent == static_cast<int>(size);
}

bool NetworkSocket::Receive(NetworkAddress& outSender, void* outBuffer, size_t maxBytes, size_t& outBytesRead) {
    outBytesRead = 0;
    if(!outBuffer||maxBytes==0)return false;
#ifdef _WIN32
    if(m_tls)return m_tls->Receive(outSender,outBuffer,maxBytes,outBytesRead);
#endif
    if (m_socket == INVALID_SOCKET || !outBuffer || maxBytes == 0) return false;

    sockaddr_in fromAddr = {};
#ifdef _WIN32
    int fromLen = sizeof(fromAddr);
    int received = recvfrom(
        m_socket,
        static_cast<char*>(outBuffer),
        static_cast<int>(maxBytes),
        0,
        reinterpret_cast<sockaddr*>(&fromAddr),
        &fromLen
    );
#else
    socklen_t fromLen = sizeof(fromAddr);
    ssize_t received = recvfrom(
        m_socket,
        outBuffer,
        maxBytes,
        0,
        reinterpret_cast<sockaddr*>(&fromAddr),
        &fromLen
    );
#endif

    if (received > 0) {
        outSender = NetworkAddress(fromAddr);
        outBytesRead = static_cast<size_t>(received);
        return true;
    }

    return false;
}

} // namespace EngineNetwork
