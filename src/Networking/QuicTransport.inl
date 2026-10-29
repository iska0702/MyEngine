// Windows MsQuic backend. Application packets use one bounded, framed QUIC stream.
// QUIC authenticates its handshake with TLS 1.3; no 0-RTT or plaintext fallback.
#if defined(_WIN32)
#include "../../ThirdParty/Microsoft/MsQuic-2.6.3-OpenSSL/build/native/include/msquic.h"
#include <fstream>
#include <mutex>
#include <atomic>
#include <deque>
#include <unordered_map>
#include <memory>
namespace EngineNetwork {
class QuicTransport {
    struct Peer;
    struct Outgoing {QUIC_BUFFER buffer{};std::vector<uint8_t> bytes;};
    struct Peer {
        QuicTransport* owner;HQUIC connection=nullptr;std::atomic<HQUIC> stream{nullptr};
        NetworkAddress address;std::atomic<bool> connected{false},dead{false};
        std::mutex sendMutex;size_t queuedBytes=0;
        std::unordered_map<Outgoing*,std::unique_ptr<Outgoing>> sends;
        std::deque<Outgoing*> pending;std::vector<uint8_t> partial;
        std::atomic<unsigned> receivedQueued{0};
        bool admitted=false;uint64_t createdAt=GetTickCount64();
        explicit Peer(QuicTransport* p):owner(p){}
    };
    struct Incoming {Peer* peer;std::vector<uint8_t> bytes;};
    HMODULE library=nullptr;const QUIC_API_TABLE* api=nullptr;
    HQUIC registration=nullptr,configuration=nullptr,listener=nullptr;
    std::vector<std::unique_ptr<Peer>> peers;std::deque<Incoming> incoming;
    std::mutex mutex;std::string error;TlsOptions options;
    bool server=false;std::atomic<bool> closing{false};uint64_t nextSession=1;
    uint16_t boundPort=0;
    void Fail(const std::string& text){std::lock_guard<std::mutex> lock(mutex);error=text;}
    static std::vector<uint8_t> ReadFile(const std::string& path){
        std::ifstream file(path,std::ios::binary|std::ios::ate);auto size=file.tellg();
        if(size<=0||size>4*1024*1024)return {};file.seekg(0);return {std::istreambuf_iterator<char>(file),{}};
    }
    static void CompleteSend(Peer& peer,Outgoing* item){
        std::lock_guard<std::mutex> lock(peer.sendMutex);auto it=peer.sends.find(item);
        if(it!=peer.sends.end()){peer.queuedBytes-=it->second->bytes.size();peer.sends.erase(it);}
    }
    void Submit(Peer& peer,Outgoing* item){
        auto result=api->StreamSend(peer.stream,&item->buffer,1,QUIC_SEND_FLAG_NONE,item);
        if(QUIC_FAILED(result)){CompleteSend(peer,item);api->ConnectionShutdown(peer.connection,QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,1);}
    }
    static QUIC_STATUS QUIC_API StreamEvent(HQUIC,void* context,QUIC_STREAM_EVENT* event){
        auto& peer=*static_cast<Peer*>(context);auto& owner=*peer.owner;
        if(event->Type==QUIC_STREAM_EVENT_SEND_COMPLETE)CompleteSend(peer,static_cast<Outgoing*>(event->SEND_COMPLETE.ClientContext));
        else if(event->Type==QUIC_STREAM_EVENT_RECEIVE){
            if(event->RECEIVE.Flags&QUIC_RECEIVE_FLAG_0_RTT)return QUIC_STATUS_ABORTED;
            for(uint32_t i=0;i<event->RECEIVE.BufferCount;++i){auto& buffer=event->RECEIVE.Buffers[i];
                for(uint32_t j=0;j<buffer.Length;++j){
                    peer.partial.push_back(buffer.Buffer[j]);
                    if(peer.partial.size()<2)continue;
                    size_t length=(size_t(peer.partial[0])<<8)|peer.partial[1];
                    if(length<sizeof(PacketHeader)||length>MAX_PACKET_SIZE){owner.api->ConnectionShutdown(peer.connection,QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,2);return QUIC_STATUS_SUCCESS;}
                    if(peer.partial.size()==length+2){
                        bool overflow=false;
                        {std::lock_guard<std::mutex> lock(owner.mutex);
                            overflow=owner.incoming.size()>=1024||peer.receivedQueued>=128;
                            if(!overflow){owner.incoming.push_back({&peer,{peer.partial.begin()+2,peer.partial.end()}});++peer.receivedQueued;}
                        }
                        peer.partial.clear();
                        if(overflow){owner.api->ConnectionShutdown(peer.connection,QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,3);return QUIC_STATUS_SUCCESS;}
                    }
                }
            }
        }
        else if(event->Type==QUIC_STREAM_EVENT_PEER_SEND_ABORTED||event->Type==QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED||event->Type==QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN)
            owner.api->ConnectionShutdown(peer.connection,QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,4);
        return QUIC_STATUS_SUCCESS;
    }
    static QUIC_STATUS QUIC_API ConnectionEvent(HQUIC connection,void* context,QUIC_CONNECTION_EVENT* event){
        auto& peer=*static_cast<Peer*>(context);auto& owner=*peer.owner;
        switch(event->Type){
        case QUIC_CONNECTION_EVENT_CONNECTED: {
            if(owner.closing)return QUIC_STATUS_ABORTED;
            QUIC_HANDSHAKE_INFO info{};uint32_t size=sizeof(info);
            if(QUIC_FAILED(owner.api->GetParam(connection,QUIC_PARAM_TLS_HANDSHAKE_INFO,&size,&info))||info.TlsProtocolVersion!=QUIC_TLS_PROTOCOL_1_3){
                owner.Fail("TLS 1.3 was not negotiated");owner.api->ConnectionShutdown(connection,QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,5);break;
            }
            if(!owner.server){
                HQUIC stream=nullptr;
                if(QUIC_FAILED(owner.api->StreamOpen(connection,QUIC_STREAM_OPEN_FLAG_NONE,StreamEvent,&peer,&stream))){owner.api->ConnectionShutdown(connection,QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,6);break;}
                peer.stream=stream;
                if(QUIC_FAILED(owner.api->StreamStart(stream,QUIC_STREAM_START_FLAG_IMMEDIATE))){owner.api->ConnectionShutdown(connection,QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,6);break;}
            }
            peer.connected=true;break;
        }
        case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
            if(!owner.server||peer.stream||(event->PEER_STREAM_STARTED.Flags&QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL)){
                owner.api->StreamClose(event->PEER_STREAM_STARTED.Stream);owner.api->ConnectionShutdown(connection,QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,7);break;
            }
            owner.api->SetCallbackHandler(event->PEER_STREAM_STARTED.Stream,reinterpret_cast<void*>(StreamEvent),&peer);
            peer.stream=event->PEER_STREAM_STARTED.Stream;break;
        case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
            peer.connected=false;owner.Fail("QUIC/TLS connection failed (status "+std::to_string(event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status)+")");break;
        case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:peer.connected=false;owner.Fail("Secure connection closed by peer");break;
        case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:peer.connected=false;peer.dead=true;break;
        default:break;
        }
        return QUIC_STATUS_SUCCESS;
    }
    static QUIC_STATUS QUIC_API ListenerEvent(HQUIC,void* context,QUIC_LISTENER_EVENT* event){
        auto& owner=*static_cast<QuicTransport*>(context);
        if(event->Type!=QUIC_LISTENER_EVENT_NEW_CONNECTION)return QUIC_STATUS_SUCCESS;
        auto peer=std::make_unique<Peer>(&owner);peer->connection=event->NEW_CONNECTION.Connection;
        peer->address=NetworkAddress(event->NEW_CONNECTION.Info->RemoteAddress->Ipv4);
        {std::lock_guard<std::mutex> lock(owner.mutex);
            unsigned pendingFromIP=0;for(auto& p:owner.peers)if(p->address.ip==peer->address.ip&&!p->admitted&&!p->dead)++pendingFromIP;
            if(owner.closing||owner.peers.size()>=MAX_CLIENTS+16||pendingFromIP>=8)return QUIC_STATUS_CONNECTION_REFUSED;
            peer->address.transportId=owner.nextSession++;
            owner.api->SetCallbackHandler(peer->connection,reinterpret_cast<void*>(ConnectionEvent),peer.get());
            owner.peers.push_back(std::move(peer));
        }
        return owner.api->ConnectionSetConfiguration(event->NEW_CONNECTION.Connection,owner.configuration);
    }
public:
    ~QuicTransport(){Close();}
    bool Open(const TlsOptions& settings,bool isServer,uint16_t port,const NetworkAddress& remote){
        options=settings;server=isServer;
        wchar_t exe[32768]{};auto exeLength=GetModuleFileNameW(nullptr,exe,32768);
        if(!exeLength||exeLength>=32768){Fail("Cannot resolve application runtime directory");return false;}
        std::wstring path(exe);path.resize(path.find_last_of(L"\\/"));path+=L"\\ThirdParty\\Microsoft\\MsQuic\\msquic.dll";
        library=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!library){Fail("MsQuic runtime not found: ThirdParty/Microsoft/MsQuic/msquic.dll");return false;}
        auto open=reinterpret_cast<QUIC_STATUS(QUIC_API*)(uint32_t,const void**)>(GetProcAddress(library,"MsQuicOpenVersion"));
        if(!open||QUIC_FAILED(open(QUIC_API_VERSION_2,reinterpret_cast<const void**>(&api)))){Fail("MsQuic API unavailable");return false;}
        QUIC_REGISTRATION_CONFIG registrationOptions{"Idontknow",QUIC_EXECUTION_PROFILE_LOW_LATENCY};
        if(QUIC_FAILED(api->RegistrationOpen(&registrationOptions,&registration))){Fail("QUIC registration failed");return false;}
        QUIC_SETTINGS tuning{};tuning.IsSet.IdleTimeoutMs=TRUE;tuning.IdleTimeoutMs=10000;
        tuning.IsSet.HandshakeIdleTimeoutMs=TRUE;tuning.HandshakeIdleTimeoutMs=5000;
        tuning.IsSet.PeerBidiStreamCount=TRUE;tuning.PeerBidiStreamCount=server?1:0;
        tuning.IsSet.ConnFlowControlWindow=TRUE;tuning.ConnFlowControlWindow=65536;
        static const uint8_t protocol[]={'i','d','k','-','g','a','m','e','-','3'};
        QUIC_BUFFER alpn{sizeof(protocol),const_cast<uint8_t*>(protocol)};
        if(QUIC_FAILED(api->ConfigurationOpen(registration,&alpn,1,&tuning,sizeof(tuning),nullptr,&configuration))){Fail("QUIC configuration failed");return false;}
        QUIC_CREDENTIAL_CONFIG credentials{};std::vector<uint8_t> pfx;QUIC_CERTIFICATE_PKCS12 pkcs12{};
        if(server){
            pfx=ReadFile(options.certificatePfx);if(pfx.empty()||pfx.size()>4*1024*1024){Fail("Cannot load server PFX certificate");return false;}
            pkcs12.Asn1Blob=pfx.data();pkcs12.Asn1BlobLength=static_cast<uint32_t>(pfx.size());pkcs12.PrivateKeyPassword=options.certificatePassword.c_str();
            credentials.Type=QUIC_CREDENTIAL_TYPE_CERTIFICATE_PKCS12;credentials.CertificatePkcs12=&pkcs12;
        }else{
            if(options.serverName.empty()){Fail("TLS server name is required");return false;}
            credentials.Type=QUIC_CREDENTIAL_TYPE_NONE;credentials.Flags=QUIC_CREDENTIAL_FLAG_CLIENT;
            if(!options.caCertificate.empty()){
                credentials.Flags=static_cast<QUIC_CREDENTIAL_FLAGS>(credentials.Flags|QUIC_CREDENTIAL_FLAG_USE_TLS_BUILTIN_CERTIFICATE_VALIDATION|QUIC_CREDENTIAL_FLAG_SET_CA_CERTIFICATE_FILE);
                credentials.CaCertificateFile=options.caCertificate.c_str();
            }
        }
        auto credentialStatus=api->ConfigurationLoadCredential(configuration,&credentials);
        if(credentialStatus!=QUIC_STATUS_SUCCESS){Fail("TLS credential configuration rejected (status "+std::to_string(credentialStatus)+")");return false;}
        if(server){
            if(QUIC_FAILED(api->ListenerOpen(registration,ListenerEvent,this,&listener))){Fail("QUIC listener creation failed");return false;}
            QUIC_ADDR address{};address.Ipv4.sin_family=AF_INET;address.Ipv4.sin_port=htons(port);
            if(QUIC_FAILED(api->ListenerStart(listener,&alpn,1,&address))){Fail("QUIC listen failed");return false;}
            uint32_t length=sizeof(address);if(QUIC_FAILED(api->GetParam(listener,QUIC_PARAM_LISTENER_LOCAL_ADDRESS,&length,&address))){Fail("Cannot query QUIC listener port");return false;}boundPort=ntohs(address.Ipv4.sin_port);
        }else{
            auto peer=std::make_unique<Peer>(this);peer->address=remote;
            if(QUIC_FAILED(api->ConnectionOpen(registration,ConnectionEvent,peer.get(),&peer->connection))){Fail("QUIC connection creation failed");return false;}
            auto connection=peer->connection;peers.push_back(std::move(peer));
            QUIC_ADDR address{};address.Ipv4=remote.socketAddress;
            if(QUIC_FAILED(api->SetParam(connection,QUIC_PARAM_CONN_REMOTE_ADDRESS,sizeof(address),&address))||QUIC_FAILED(api->ConnectionStart(connection,configuration,QUIC_ADDRESS_FAMILY_INET,options.serverName.c_str(),remote.port))){Fail("QUIC connection start failed");return false;}
        }
        return true;
    }
    std::string Error(){std::lock_guard<std::mutex> lock(mutex);return error;}
    uint16_t Port()const{return boundPort;}
    bool Send(const NetworkAddress& address,const void* data,size_t size){
        if(!data||size<sizeof(PacketHeader)||size>MAX_PACKET_SIZE||closing)return false;
        Peer* peer=nullptr;{std::lock_guard<std::mutex> lock(mutex);for(auto& p:peers)if(p->address==address&&!p->dead){peer=p.get();break;}}
        if(!peer)return false;
        auto item=std::make_unique<Outgoing>();item->bytes.resize(size+2);item->bytes[0]=uint8_t(size>>8);item->bytes[1]=uint8_t(size);std::memcpy(item->bytes.data()+2,data,size);
        item->buffer={static_cast<uint32_t>(item->bytes.size()),item->bytes.data()};auto raw=item.get();
        {std::lock_guard<std::mutex> lock(peer->sendMutex);if(peer->queuedBytes+size+2>256*1024)return false;peer->queuedBytes+=size+2;peer->sends.emplace(raw,std::move(item));peer->pending.push_back(raw);}
        return true;
    }
    bool Receive(NetworkAddress& address,void* data,size_t capacity,size_t& size){
        // The application thread owns reaping and sends; callbacks only append data.
        std::vector<Peer*> active,expired;std::vector<std::unique_ptr<Peer>> retired;
        {std::lock_guard<std::mutex> lock(mutex);
            for(auto it=peers.begin();it!=peers.end();){
                if((*it)->dead&&(*it)->receivedQueued==0){retired.push_back(std::move(*it));it=peers.erase(it);}
                else{if(server&&!(*it)->admitted&&GetTickCount64()-(*it)->createdAt>5000)expired.push_back(it->get());
                    else if((*it)->connected&&(*it)->stream)active.push_back(it->get());++it;}
            }
        }
        for(auto peer:expired)api->ConnectionShutdown(peer->connection,QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,10);
        for(auto& peer:retired){if(peer->stream)api->StreamClose(peer->stream);api->ConnectionClose(peer->connection);}
        for(auto peer:active){
            std::deque<Outgoing*> pending;{std::lock_guard<std::mutex> lock(peer->sendMutex);pending.swap(peer->pending);}
            for(auto item:pending)Submit(*peer,item);
        }
        std::lock_guard<std::mutex> lock(mutex);
        while(!incoming.empty()){
            auto packet=std::move(incoming.front());incoming.pop_front();--packet.peer->receivedQueued;
            if(!packet.peer->connected||packet.bytes.size()>capacity)continue;
            address=packet.peer->address;size=packet.bytes.size();std::memcpy(data,packet.bytes.data(),size);return true;
        }
        return false;
    }

    void AcceptPeer(const NetworkAddress& address){std::lock_guard<std::mutex> lock(mutex);for(auto& peer:peers)if(peer->address==address)peer->admitted=true;}
    bool IsPeerConnected(const NetworkAddress& address){
        std::lock_guard<std::mutex> lock(mutex);for(auto& peer:peers)if(peer->address==address)return peer->connected&&!peer->dead;return false;
    }
    void ClosePeer(const NetworkAddress& address){
        Peer* peer=nullptr;{std::lock_guard<std::mutex> lock(mutex);for(auto& p:peers)if(p->address==address){peer=p.get();break;}}
        if(peer)api->ConnectionShutdown(peer->connection,QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,8);
    }
    void Close(){
        closing=true;
        if(listener){api->ListenerClose(listener);listener=nullptr;}
        // No new peers after ListenerClose; ConnectionClose drains callbacks.
        for(auto& peer:peers){if(peer->connection)api->ConnectionShutdown(peer->connection,QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,0);}
        auto deadline=GetTickCount64()+3000;
        while(GetTickCount64()<deadline){bool pending=false;for(auto& peer:peers)pending|=!peer->dead;if(!pending)break;Sleep(1);}
        for(auto& peer:peers){if(peer->stream)api->StreamClose(peer->stream);if(peer->connection)api->ConnectionClose(peer->connection);}
        peers.clear();incoming.clear();
        if(configuration)api->ConfigurationClose(configuration);if(registration)api->RegistrationClose(registration);
        configuration=registration=nullptr;
        if(api&&library){auto close=reinterpret_cast<void(QUIC_API*)(const QUIC_API_TABLE*)>(GetProcAddress(library,"MsQuicClose"));if(close)close(api);}
        api=nullptr;if(library)FreeLibrary(library);library=nullptr;
    }
};
}
#endif
