#pragma once

#include "../Networking/NetworkServer.h"
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <deque>

namespace EngineServer {

class DedicatedServer {
public:
    DedicatedServer();
    ~DedicatedServer();

    bool Initialize(int argc, char* argv[]);
    int Run();
    void Shutdown();

private:
    void LoadConfigFile(const std::string& filepath);
    void ParseCommandLine(int argc, char* argv[]);
    void InitializeWorld();
    void RunConsoleInputThread();
    void ProcessConsoleCommand(const std::string& cmdLine);

    struct LoadedWorldInfo {
        std::string name = "world";
        std::string resolvedPath;
        bool isPak = false;
        bool isLevel = false;
        bool isDirectory = false;
        size_t sizeBytes = 0;
    };

    LoadedWorldInfo m_worldInfo;
    EngineNetwork::ServerConfig m_config;
    EngineNetwork::NetworkServer m_server;

    std::atomic<bool> m_isRunning = false;
    std::thread m_consoleThread;
    std::mutex m_consoleMutex;
    std::deque<std::string> m_consoleCommands;
};

} // namespace EngineServer
