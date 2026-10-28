#include "DedicatedServer.h"
#include <iostream>

int main(int argc, char* argv[]) {
    std::cout << "Starting Idontknow Dedicated Server..." << std::endl;

    EngineServer::DedicatedServer server;
    if (!server.Initialize(argc, argv)) {
        std::cerr << "Failed to initialize dedicated server. Exiting." << std::endl;
        return -1;
    }

    return server.Run();
}
