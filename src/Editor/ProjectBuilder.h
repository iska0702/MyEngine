#pragma once
#if defined(_WIN32) && defined(ENGINE_EDITOR)
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
#include <memory>
namespace EngineProject {
struct BuildSnapshot {
    bool running=false,succeeded=false,cancelled=false;
    float progress=0;
    std::string phase,error;
    std::filesystem::path project,output,logFile;
    std::vector<std::string> lines;
};
class ProjectBuilder {
public:
    static ProjectBuilder& Get();
    ProjectBuilder();
    ~ProjectBuilder();
    ProjectBuilder(const ProjectBuilder&)=delete;
    void Start(const std::filesystem::path& project,const std::string& platform="");
    bool BuildBlocking(const std::filesystem::path& project,const std::string& platform="");
    void Cancel();
    BuildSnapshot Snapshot() const;
    static std::filesystem::path SupportRoot();
    static bool HasRuntime(const std::string& platform);
    // Engine SDK preparation only. Normal project builds use the installed runtime.
    static void InstallRuntime(const std::filesystem::path& sourceBuild,const std::string& platform,
                               const std::filesystem::path& textureCooker);
private:
    struct State;std::unique_ptr<State> state;
    void Run(std::filesystem::path project,std::string platform);
};
}
#endif
