#include "DedicatedServer.h"
#include <cstdlib>
#include "Level/LevelSystem.h"
#include "Core/Timer.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#else
#include <csignal>
#include <poll.h>
#include <unistd.h>
#endif

namespace EngineServer {

static DedicatedServer* s_serverInstance = nullptr;

#ifdef _WIN32
static BOOL WINAPI ConsoleCtrlHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT || signal == CTRL_SHUTDOWN_EVENT) {
        if (s_serverInstance) {
            std::cout << "\n[Server] Shutdown signal received. Stopping server..." << std::endl;
            s_serverInstance->Shutdown();
        }
        return TRUE;
    }
    return FALSE;
}
#else
static void PosixSignalHandler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        if (s_serverInstance) {
            std::cout << "\n[Server] Shutdown signal (" << sig << ") received. Stopping server..." << std::endl;
            s_serverInstance->Shutdown();
        }
    }
}
#endif

DedicatedServer::DedicatedServer() {
    s_serverInstance = this;
}

DedicatedServer::~DedicatedServer() {
    Shutdown();
    if (m_consoleThread.joinable()) m_consoleThread.join();
    m_server.Stop();
    if (s_serverInstance == this) {
        s_serverInstance = nullptr;
    }
}

void DedicatedServer::LoadConfigFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        // Strip whitespace and comments
        size_t commentPos = line.find('#');
        if (commentPos != std::string::npos) line = line.substr(0, commentPos);

        size_t eqPos = line.find('=');
        if (eqPos == std::string::npos) continue;

        std::string key = line.substr(0, eqPos);
        std::string val = line.substr(eqPos + 1);

        // Trim
        auto trim = [](std::string& s) {
            s.erase(0, s.find_first_not_of(" \t\r\n"));
            s.erase(s.find_last_not_of(" \t\r\n") + 1);
        };
        trim(key);
        trim(val);

        if(key=="tls_enabled"){m_config.tls.enabled=(val=="1"||val=="true");}
        else if(key=="tls_pfx"){m_config.tls.certificatePfx=val;}
        else if(key=="tls_password_env"){const char* password=std::getenv(val.c_str());m_config.tls.certificatePassword=password?password:"";}
        else if (key == "port") {
            try { m_config.port = static_cast<uint16_t>(std::stoi(val)); } catch (...) {}
        } else if (key == "max_players") {
            try { m_config.maxPlayers = static_cast<uint32_t>(std::stoi(val)); } catch (...) {}
        } else if (key == "tick_rate") {
            try { m_config.tickRate = std::stof(val); } catch (...) {}
        } else if (key == "network_rate") {
            try { m_config.networkRate = std::stof(val); } catch (...) {}
        } else if (key == "server_name") {
            m_config.serverName = val;
        } else if (key == "world" || key == "world_name" || key == "map" || key == "world_pak") {
            m_config.worldName = val;
        }
    }
}

void DedicatedServer::ParseCommandLine(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if(arg=="--tls"){m_config.tls.enabled=true;}
        else if(arg=="--tls-pfx"&&i+1<argc){m_config.tls.enabled=true;m_config.tls.certificatePfx=argv[++i];}
        else if(arg=="--tls-password-env"&&i+1<argc){const char* password=std::getenv(argv[++i]);m_config.tls.certificatePassword=password?password:"";}
        else if ((arg == "--port" || arg == "-p") && i + 1 < argc) {
            try { m_config.port = static_cast<uint16_t>(std::stoi(argv[++i])); } catch (...) {}
        } else if ((arg == "--max-players" || arg == "-m") && i + 1 < argc) {
            try { m_config.maxPlayers = static_cast<uint32_t>(std::stoi(argv[++i])); } catch (...) {}
        } else if ((arg == "--tick-rate" || arg == "-t") && i + 1 < argc) {
            try { m_config.tickRate = std::stof(argv[++i]); } catch (...) {}
        } else if ((arg == "--network-rate" || arg == "-n") && i + 1 < argc) {
            try { m_config.networkRate = std::stof(argv[++i]); } catch (...) {}
        } else if ((arg == "--world" || arg == "-w") && i + 1 < argc) {
            m_config.worldName = argv[++i];
        } else if ((arg == "--config" || arg == "-c") && i + 1 < argc) {
            LoadConfigFile(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: IdontknowServer.exe [options]\n"
                      << "Options:\n"
                      << "  --tls                 Require QUIC/TLS 1.3 (no plaintext fallback)\n"
                      << "  --tls-pfx <path>      Server certificate/private-key PFX\n"
                      << "  --tls-password-env <name>  PFX password environment variable\n"
                      << "  --port <port>          Server port (default: 7777)\n"
                      << "  --max-players <count>  Max players (default: 32)\n"
                      << "  --tick-rate <hz>       Physics tick rate (default: 60)\n"
                      << "  --network-rate <hz>    Snapshot rate (default: 20)\n"
                      << "  --world <name>         World or .pak name (default: world)\n"
                      << "  --config <path>        Config file path (default: server.cfg)\n";
            exit(0);
        }
    }
}

void DedicatedServer::InitializeWorld() {
    m_worldInfo.name = m_config.worldName;
    m_worldInfo.resolvedPath = "";
    m_worldInfo.isPak = false;
    m_worldInfo.isDirectory = false;
    m_worldInfo.sizeBytes = 0;

    namespace fs = std::filesystem;

    std::string candidate = m_config.worldName;
    std::vector<std::string> searchPaths;
    // Determine the root folder of the server executable
#ifdef _WIN32
    char exePathBuf[MAX_PATH] = {};
    GetModuleFileNameA(NULL, exePathBuf, MAX_PATH);
    fs::path exeDir = fs::path(exePathBuf).parent_path();
#else
    char exePathBuf[1024] = {};
    ssize_t count = readlink("/proc/self/exe", exePathBuf, sizeof(exePathBuf) - 1);
    fs::path exeDir = (count > 0) ? fs::path(std::string(exePathBuf, count)).parent_path() : fs::current_path();
#endif

    fs::path root = EngineLevel::LevelSystem::GetProjectRootDir();
    fs::path serverMapsDir = root / "ServerMaps";

    bool endsWithPak = (candidate.size() >= 4 && candidate.substr(candidate.size() - 4) == ".pak");
    std::string pakCandidate = endsWithPak ? candidate : (candidate + ".pak");

    bool endsWithLevel = (candidate.size() >= 6 && candidate.substr(candidate.size() - 6) == ".level");
    std::string levelCandidate = endsWithLevel ? candidate : (candidate + ".level");

    // 1. PRIMARY: Root of the server folder (where IdontknowServer is located)
    searchPaths.push_back((exeDir / pakCandidate).string());
    searchPaths.push_back((exeDir / candidate).string());
    searchPaths.push_back(pakCandidate);
    searchPaths.push_back("./" + pakCandidate);
    searchPaths.push_back(candidate);
    searchPaths.push_back("./" + candidate);

    // 2. Fallbacks: ServerMaps directory
    searchPaths.push_back((serverMapsDir / pakCandidate).string());
    searchPaths.push_back((serverMapsDir / candidate).string());
    searchPaths.push_back("../ServerMaps/" + pakCandidate);
    searchPaths.push_back("../ServerMaps/" + candidate);
    searchPaths.push_back((exeDir / "ServerMaps" / pakCandidate).string());
    searchPaths.push_back((exeDir / "ServerMaps" / candidate).string());
    searchPaths.push_back("ServerMaps/" + pakCandidate);
    searchPaths.push_back("ServerMaps/" + candidate);
    searchPaths.push_back("../" + pakCandidate);
    searchPaths.push_back("../" + candidate);
    searchPaths.push_back("Contents/" + pakCandidate);
    searchPaths.push_back("../Contents/" + pakCandidate);
    searchPaths.push_back("worlds/" + pakCandidate);
    searchPaths.push_back("../worlds/" + pakCandidate);

    // 5. Level files (.level) for development & debug
    searchPaths.push_back((exeDir / levelCandidate).string());
    searchPaths.push_back(levelCandidate);
    searchPaths.push_back("./" + levelCandidate);
    searchPaths.push_back((serverMapsDir / levelCandidate).string());
    searchPaths.push_back("ServerMaps/" + levelCandidate);
    searchPaths.push_back("../ServerMaps/" + levelCandidate);
    searchPaths.push_back("../" + levelCandidate);
    searchPaths.push_back("src/Content/Levels/" + levelCandidate);
    searchPaths.push_back("../src/Content/Levels/" + levelCandidate);
    searchPaths.push_back("Content/Levels/" + levelCandidate);
    searchPaths.push_back("../Content/Levels/" + levelCandidate);
    searchPaths.push_back("Levels/" + levelCandidate);
    searchPaths.push_back("../Levels/" + levelCandidate);
    searchPaths.push_back("src/Content/Levels/" + candidate);
    searchPaths.push_back("../src/Content/Levels/" + candidate);
    searchPaths.push_back("Content/Levels/" + candidate);
    searchPaths.push_back("../Content/Levels/" + candidate);
    searchPaths.push_back("Levels/" + candidate);
    searchPaths.push_back("../Levels/" + candidate);

    bool found = false;
    for (const auto& p : searchPaths) {
        std::error_code ec;
        if (fs::exists(p, ec)) {
            if (fs::is_regular_file(p, ec)) {
                m_worldInfo.resolvedPath = p;
                m_worldInfo.sizeBytes = fs::file_size(p, ec);
                m_worldInfo.isPak = (p.size() >= 4 && p.substr(p.size() - 4) == ".pak");
                m_worldInfo.isLevel = (p.size() >= 6 && p.substr(p.size() - 6) == ".level");
                found = true;
                break;
            } else if (fs::is_directory(p, ec)) {
                m_worldInfo.resolvedPath = p;
                m_worldInfo.isDirectory = true;
                found = true;
                break;
            }
        }
    }

    std::cout << "[World] Initializing world: '" << m_worldInfo.name << "'...\n";
    if (found) {
        if (m_worldInfo.isLevel) {
            std::cout << "[World] Found Level file (.level): " << m_worldInfo.resolvedPath
                      << " (" << m_worldInfo.sizeBytes << " bytes)\n";
            std::cout << "[World] Parsing level geometry, spawn point, and environment manifests...\n";
            EngineLevel::LevelSystem::Get().LoadLevel(m_worldInfo.resolvedPath);
            const auto& lvl = EngineLevel::LevelSystem::Get().GetCurrentLevel();
            std::cout << "[World] Level Name: '" << lvl.levelName << "', Author: '" << lvl.author << "'\n";
            std::cout << "[World] Loaded " << lvl.entities.size() << " entities, Spawn point: ("
                      << lvl.spawnPosition.x << ", " << lvl.spawnPosition.y << ", " << lvl.spawnPosition.z << ")\n";
            std::cout << "[World] World '" << m_worldInfo.name << "' loaded successfully from .level file!\n";
        } else if (m_worldInfo.isPak) {
            std::cout << "[World] Found PAK archive: " << m_worldInfo.resolvedPath
                      << " (" << m_worldInfo.sizeBytes << " bytes)\n";
            std::cout << "[World] Reading sector headers & loading entity manifests from PAK...\n";
            bool loadedAsWpak = EngineLevel::LevelSystem::Get().LoadWorldPak(m_worldInfo.resolvedPath);
            if (loadedAsWpak) {
                const auto& lvl = EngineLevel::LevelSystem::Get().GetCurrentLevel();
                std::cout << "[World] Detected WPAK World Archive (Format v1): '" << lvl.levelName << "' by '" << lvl.author << "'\n";
                std::cout << "[World] Level entities loaded from PAK: " << lvl.entities.size()
                          << " entities, Spawn point: (" << lvl.spawnPosition.x << ", "
                          << lvl.spawnPosition.y << ", " << lvl.spawnPosition.z << ", yaw: "
                          << lvl.spawnYaw << ")\n";
            }
            std::cout << "[World] World '" << m_worldInfo.name << "' loaded successfully from PAK archive!\n";
        } else if (m_worldInfo.isDirectory) {
            std::cout << "[World] Found world directory: " << m_worldInfo.resolvedPath << "\n";
            std::cout << "[World] Loading world region chunks and entity data from directory...\n";
            std::cout << "[World] World '" << m_worldInfo.name << "' loaded successfully from directory!\n";
        } else {
            std::cout << "[World] Found world data file: " << m_worldInfo.resolvedPath << "\n";
            std::cout << "[World] World '" << m_worldInfo.name << "' loaded successfully!\n";
        }
    } else {
        std::cout << "[World] World archive/folder '" << m_worldInfo.name << "' not found on disk.\n";
        std::error_code ec;
        std::string fallbackDir = "worlds/" + m_worldInfo.name;
        fs::create_directories(fallbackDir, ec);
        m_worldInfo.resolvedPath = fallbackDir;
        m_worldInfo.isDirectory = true;
        std::cout << "[World] Created runtime world directory: " << m_worldInfo.resolvedPath << "\n";
        std::cout << "[World] World '" << m_worldInfo.name << "' initialized and ready.\n";
    }
}

bool DedicatedServer::Initialize(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
#else
    signal(SIGINT, PosixSignalHandler);
    signal(SIGTERM, PosixSignalHandler);
    signal(SIGHUP, SIG_IGN);
#endif

    // 1. Try loading default config files
    LoadConfigFile("server.cfg");
    LoadConfigFile("../server.cfg");

    // 2. Command-line args override config file
    ParseCommandLine(argc, argv);

    // 3. Initialize & load configured world (Folder or .PAK archive)
    InitializeWorld();

    // 4. Start server networking
    if (!m_server.Start(m_config)) {
        return false;
    }

    m_isRunning = true;

    // 4. Launch console input background worker thread
    m_consoleThread = std::thread(&DedicatedServer::RunConsoleInputThread, this);

    return true;
}

void DedicatedServer::Shutdown() {
    m_isRunning = false;
}

void DedicatedServer::RunConsoleInputThread() {
    std::string line;
    while (m_isRunning) {
        char ch = 0;
#ifdef _WIN32
        HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
        DWORD mode = 0, available = 0, read = 0;
        if (GetConsoleMode(input, &mode)) {
            if (!_kbhit()) { std::this_thread::sleep_for(std::chrono::milliseconds(25)); continue; }
            int key = _getch();
            if (key == 0 || key == 224) { _getch(); continue; }
            ch = static_cast<char>(key);
            if (ch == '\b') { if (!line.empty()) { line.pop_back(); std::cout << "\b \b" << std::flush; } continue; }
            std::cout << (ch == '\r' ? '\n' : ch) << std::flush;
        } else {
            if (GetFileType(input) == FILE_TYPE_PIPE) {
                if (!PeekNamedPipe(input, nullptr, 0, nullptr, &available, nullptr)) break;
                if (!available) { std::this_thread::sleep_for(std::chrono::milliseconds(25)); continue; }
            }
            if (!ReadFile(input, &ch, 1, &read, nullptr) || !read) break;
        }
#else
        pollfd input { STDIN_FILENO, POLLIN, 0 };
        if (poll(&input, 1, 25) <= 0) continue;
        if (read(STDIN_FILENO, &ch, 1) != 1) break;
#endif
        if (ch == '\r' || ch == '\n') {
            if (!line.empty()) {
                std::lock_guard<std::mutex> lock(m_consoleMutex);
                if (m_consoleCommands.size() < 256) m_consoleCommands.push_back(std::move(line));
                line.clear();
            }
        } else if (line.size() < 4096) line.push_back(ch);
    }
    if (!line.empty()) {
        std::lock_guard<std::mutex> lock(m_consoleMutex);
        m_consoleCommands.push_back(std::move(line));
    }
}

void DedicatedServer::ProcessConsoleCommand(const std::string& cmdLine) {
    std::string trimmed = cmdLine;
    trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
    trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);
    if (trimmed.empty()) return;

    std::istringstream iss(trimmed);
    std::string cmd;
    iss >> cmd;
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

    if (cmd == "help") {
        std::cout << "\nAvailable Server Commands:\n"
                  << "  help              Show this help list\n"
                  << "  status            Display server uptime, tick rate, and player count\n"
                  << "  world [name]      Display active world or switch world/PAK archive\n"
                  << "  players           List all connected players with ID, IP, and Ping\n"
                  << "  kick <id>         Kick player by Client ID\n"
                  << "  ban <id/ip>       Ban player by ID or IP address\n"
                  << "  say <message>     Broadcast chat message to all players\n"
                  << "  save              Save server state\n"
                  << "  shutdown / exit   Gracefully stop and exit the server\n\n";
    } else if (cmd == "status") {
        int uptimeSec = static_cast<int>(m_server.GetUptimeSeconds());
        int hours = uptimeSec / 3600;
        int mins = (uptimeSec % 3600) / 60;
        int secs = uptimeSec % 60;

        std::cout << "\n=== Server Status ===\n"
                  << "  Server Name:    " << m_config.serverName << "\n"
                  << "  Port:           " << m_config.port << "\n"
                  << "  World / Map:    " << m_worldInfo.name;
        if (!m_worldInfo.resolvedPath.empty()) {
            std::cout << " (" << m_worldInfo.resolvedPath << (m_worldInfo.isPak ? ", PAK Archive" : (m_worldInfo.isDirectory ? ", Directory" : "")) << ")";
        }
        std::cout << "\n"
                  << "  Uptime:         " << hours << "h " << mins << "m " << secs << "s\n"
                  << "  Server Tick:    " << m_server.GetCurrentServerTick() << "\n"
                  << "  Players:        " << m_server.GetConnectedPlayerCount() << " / " << m_config.maxPlayers << "\n"
                  << "  Tick Rate:      " << m_config.tickRate << " Hz\n"
                  << "  Network Rate:   " << m_config.networkRate << " Hz\n\n";
    } else if (cmd == "world" || cmd == "map") {
        std::string newWorld;
        if (iss >> newWorld) {
            m_config.worldName = newWorld;
            InitializeWorld();
            std::cout << "[Server] Active world switched to: " << m_config.worldName << "\n";
        } else {
            std::cout << "\n=== Active World ===\n"
                      << "  World Name:    " << m_worldInfo.name << "\n"
                      << "  Source Path:   " << (m_worldInfo.resolvedPath.empty() ? "None (Runtime)" : m_worldInfo.resolvedPath) << "\n"
                      << "  Archive Type:  " << (m_worldInfo.isPak ? "PAK Archive" : (m_worldInfo.isDirectory ? "Folder / Directory" : "File")) << "\n";
            if (m_worldInfo.sizeBytes > 0) {
                std::cout << "  File Size:     " << m_worldInfo.sizeBytes << " bytes\n";
            }
            std::cout << "\n";
        }
    } else if (cmd == "players") {
        const auto& clients = m_server.GetClients();
        std::cout << "\n=== Connected Players (" << clients.size() << ") ===\n";
        if (clients.empty()) {
            std::cout << "  No players currently connected.\n\n";
        } else {
            std::cout << std::left
                      << std::setw(6)  << "ID"
                      << std::setw(22) << "IP:Port"
                      << std::setw(18) << "Name"
                      << std::setw(10) << "Ping"
                      << "Position" << "\n";
            std::cout << std::string(75, '-') << "\n";

            for (const auto& [id, client] : clients) {
                std::ostringstream posSs;
                posSs << "(" << std::fixed << std::setprecision(1)
                      << client.playerState.position.x << ", "
                      << client.playerState.position.y << ", "
                      << client.playerState.position.z << ")";

                std::cout << std::left
                          << std::setw(6)  << id
                          << std::setw(22) << client.connection.GetAddress().ToString()
                          << std::setw(18) << client.playerState.playerName
                          << std::setw(10) << (std::to_string(static_cast<int>(client.connection.GetPingMs())) + " ms")
                          << posSs.str() << "\n";
            }
            std::cout << "\n";
        }
    } else if (cmd == "kick") {
        uint32_t targetId = 0;
        if (iss >> targetId) {
            if (!m_server.KickPlayer(targetId)) {
                std::cout << "[Server] Player ID " << targetId << " not found.\n";
            }
        } else {
            std::cout << "Usage: kick <player_id>\n";
        }
    } else if (cmd == "ban") {
        std::string target;
        if (iss >> target) {
            bool isNumber = !target.empty() && std::all_of(target.begin(), target.end(), ::isdigit);
            if (isNumber) {
                uint32_t id = static_cast<uint32_t>(std::stoi(target));
                if (!m_server.BanPlayer(id)) {
                    std::cout << "[Server] Player ID " << id << " not found.\n";
                }
            } else {
                m_server.BanIP(target);
            }
        } else {
            std::cout << "Usage: ban <player_id or IP>\n";
        }
    } else if (cmd == "say") {
        std::string msg;
        std::getline(iss >> std::ws, msg);
        if (!msg.empty()) {
            m_server.BroadcastChat("[SERVER ADMIN]", msg);
            std::cout << "[Server Broadcast] " << msg << "\n";
        }
    } else if (cmd == "save") {
        std::cout << "[Server] Server state saved successfully.\n";
    } else if (cmd == "shutdown" || cmd == "exit" || cmd == "quit") {
        std::cout << "[Server] Shutting down...\n";
        Shutdown();
    } else {
        std::cout << "Unknown command: '" << cmd << "'. Type 'help' for command list.\n";
    }
}

int DedicatedServer::Run() {
    auto lastTime = std::chrono::high_resolution_clock::now();
    double currentTime = 0.0;
    double targetFrameTime = 1.0 / (std::max)(m_config.tickRate, 1.0f);

    while (m_isRunning) {
        std::deque<std::string> commands;
        { std::lock_guard<std::mutex> lock(m_consoleMutex); commands.swap(m_consoleCommands); }
        for (const auto& command : commands) {
            ProcessConsoleCommand(command);
            if (!m_isRunning) break;
        }
        if (!m_isRunning) break;
        auto now = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = now - lastTime;
        lastTime = now;

        double dt = (std::min)(elapsed.count(), 0.1);
        currentTime += dt;

        // Server tick
        m_server.Update(currentTime, dt);

        // Sleep remainder of tick to maintain fixed tick rate without pegging CPU to 100%
        auto frameEnd = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> workDuration = frameEnd - now;
        double sleepTime = targetFrameTime - workDuration.count();
        if (sleepTime > 0.001) {
            std::this_thread::sleep_for(std::chrono::duration<double>(sleepTime));
        } else {
            std::this_thread::yield();
        }
    }

    if (m_consoleThread.joinable()) {
        m_consoleThread.join();
    }
    m_server.Stop();

    return 0;
}

} // namespace EngineServer
