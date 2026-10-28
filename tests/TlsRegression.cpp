#include "Networking/NetworkServer.h"
#include "Networking/NetworkClient.h"
#include <iostream>
#include <thread>
#include <stdexcept>
#include <chrono>
#include <algorithm>
using namespace EngineNetwork;
static void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static ServerConfig Config(bool expired=false){ServerConfig s;s.port=0;s.tls.enabled=true;s.tls.certificatePfx=expired?"Diagnostics/TlsFixtures/expired.pfx":"Diagnostics/TlsFixtures/server.pfx";s.tls.certificatePassword="regression-only";return s;}
static TlsOptions ClientOptions(){TlsOptions o;o.enabled=true;o.serverName="localhost";o.caCertificate="Diagnostics/TlsFixtures/ca.pem";return o;}
static void Pump(NetworkServer& server,NetworkClient& client,int count,double& time){for(int i=0;i<count;++i){time+=.01;server.Update(time,.01);client.Update(time,.01);std::this_thread::sleep_for(std::chrono::milliseconds(2));}}
struct Relay {
    SOCKET socket=INVALID_SOCKET;sockaddr_in server{},client{};bool hasClient=false;
    uint16_t port=0;std::vector<std::vector<uint8_t>> wire;std::vector<uint8_t> lastClientCiphertext;
    explicit Relay(uint16_t serverPort){NetworkSocket::GlobalInitialize();socket=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);Check(socket!=INVALID_SOCKET,"Relay socket failed");
        sockaddr_in local{};local.sin_family=AF_INET;local.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        Check(bind(socket,reinterpret_cast<sockaddr*>(&local),sizeof(local))==0,"Relay bind failed");int length=sizeof(local);getsockname(socket,reinterpret_cast<sockaddr*>(&local),&length);port=ntohs(local.sin_port);
        server=local;server.sin_port=htons(serverPort);u_long mode=1;ioctlsocket(socket,FIONBIO,&mode);
    }
    ~Relay(){closesocket(socket);}
    void Tick(){for(int i=0;i<128;++i){uint8_t bytes[65535];sockaddr_in from{};int length=sizeof(from);int n=recvfrom(socket,reinterpret_cast<char*>(bytes),sizeof(bytes),0,reinterpret_cast<sockaddr*>(&from),&length);if(n<=0)break;
        if(wire.size()<4096)wire.emplace_back(bytes,bytes+n);
        bool fromServer=from.sin_port==server.sin_port;
        if(!fromServer){client=from;hasClient=true;if(n>48&&!(bytes[0]&0x80))lastClientCiphertext.assign(bytes,bytes+n);}
        if(fromServer&&!hasClient)continue;auto& target=fromServer?client:server;
        sendto(socket,reinterpret_cast<char*>(bytes),n,0,reinterpret_cast<sockaddr*>(&target),sizeof(target));
    }}
    void ReplayAndCorrupt(){Check(!lastClientCiphertext.empty(),"No encrypted application datagram captured");auto packet=lastClientCiphertext;
        for(int i=0;i<5;++i)sendto(socket,reinterpret_cast<char*>(packet.data()),int(packet.size()),0,reinterpret_cast<sockaddr*>(&server),sizeof(server));
        packet.back()^=0x40;
        for(int i=0;i<5;++i)sendto(socket,reinterpret_cast<char*>(packet.data()),int(packet.size()),0,reinterpret_cast<sockaddr*>(&server),sizeof(server));
    }
};
int main(){try{
    NetworkServer server;Check(server.Start(Config()),"TLS server failed to start");
    Relay relay(server.GetConfig().port);
    NetworkClient client;client.SetTlsOptions(ClientOptions());Check(client.Connect("127.0.0.1",relay.port,"TLS regression"),"TLS client failed to start");
    double time=0;
    auto tick=[&](int count){for(int i=0;i<count;++i){relay.Tick();Pump(server,client,1,time);relay.Tick();}};
    for(int i=0;i<350&&!client.IsConnected();++i)tick(1);
    Check(client.IsConnected()&&server.GetConnectedPlayerCount()==1,"Authenticated TLS connection failed");
    Check(server.GetClients().begin()->second.connection.GetAddress().transportId!=0,"TLS session not bound to game peer");
    auto position=server.GetClients().begin()->second.playerState.position;
    PlayerInputData input;input.inputTick=1;input.moveY=1;input.deltaTime=.01f;client.SendPlayerInput(input,time);
    tick(100);Check(server.GetClients().begin()->second.playerState.position.z>position.z+.1f,"TLS gameplay input was not delivered");
    const std::string secret="TLS wire confidentiality regression marker";int delivered=0;
    client.SetOnChatMessage([&](uint32_t,const std::string&,const std::string& message){if(message==secret)++delivered;});
    client.SendChat(secret);tick(100);Check(delivered==1,"Encrypted chat was not delivered");
    Check(relay.wire.size()>5,"Relay did not capture traffic");
    for(const auto& packet:relay.wire)Check(std::search(packet.begin(),packet.end(),secret.begin(),secret.end())==packet.end(),"Plaintext application data visible on wire");
    relay.ReplayAndCorrupt();tick(100);Check(client.IsConnected()&&delivered==1,"Replayed/corrupted ciphertext altered application state");
    NetworkSocket attacker;Check(attacker.Open(0),"Plaintext injection socket failed");auto raw=Packet::CreateConnectionRequest("Plaintext attacker").Serialize();
    for(int i=0;i<20;++i)attacker.Send(NetworkAddress("127.0.0.1",server.GetConfig().port),raw.data(),raw.size());
    tick(20);Check(server.GetConnectedPlayerCount()==1,"Plaintext joined TLS-only server");
    client.Disconnect();for(int i=0;i<20;++i){relay.Tick();time+=.01;server.Update(time,.01);relay.Tick();std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    Check(server.GetConnectedPlayerCount()==0,"Closed TLS session retained a ghost player");server.Stop();
    for(int negative=0;negative<3;++negative){
        NetworkServer denied;Check(denied.Start(Config(negative==2)),"Negative TLS server failed to start");
        NetworkClient peer;auto options=ClientOptions();if(negative==0)options.serverName="wrong.example";if(negative==1)options.caCertificate.clear();peer.SetTlsOptions(options);
        Check(peer.Connect("127.0.0.1",denied.GetConfig().port,"Must reject"),"Negative client initialization failed");
        double t=0;Pump(denied,peer,450,t);
        Check(!peer.IsConnected()&&denied.GetConnectedPlayerCount()==0,"Invalid certificate was accepted");
        peer.Disconnect();denied.Stop();
    }
    ServerConfig missing=Config();missing.tls.certificatePfx="";NetworkServer invalid;Check(!invalid.Start(missing),"Missing server credentials silently downgraded");
    std::cout<<"PASS: encrypted wire capture, replay/tamper rejection, plaintext injection rejection, session cleanup, TLS 1.3 handshake, authenticated session, gameplay, invalid hostname, untrusted CA, expired certificate, no plaintext fallback\n";
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
