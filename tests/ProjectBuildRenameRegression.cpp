#include "Core/ProjectBuildRename.h"
#include <iostream>
#include <thread>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    namespace fs = std::filesystem;
    const auto root = fs::path(argv[1]) / ("rename-test-" + std::to_string(GetCurrentProcessId()));
    const auto source = root / "stage", destination = root / "Build";
    fs::create_directories(source);
    HANDLE held = CreateFileW(source.c_str(), FILE_READ_ATTRIBUTES,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (held == INVALID_HANDLE_VALUE) return 3;
    std::thread release([held] { std::this_thread::sleep_for(std::chrono::milliseconds(700)); CloseHandle(held); });
    const auto started = std::chrono::steady_clock::now();
    const auto error = EngineProject::RenameBuildDirectory(source, destination);
    release.join();
    const auto elapsed = std::chrono::steady_clock::now() - started;
    if (error || !fs::is_directory(destination) || elapsed < std::chrono::milliseconds(600)) return 4;
    fs::create_directory(source);
    const auto conflict = EngineProject::RenameBuildDirectory(source, destination);
    if (!conflict || !fs::is_directory(source) || !fs::is_directory(destination)) return 5;
    std::cout << "PASS: transient directory lock retried; existing destination preserved\n";
    return 0;
}
