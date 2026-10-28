#include "PlatformUtils.h"
#include "Core/Project.h"
#include "Core/EngineLog.h"
#include <iostream>
#include <filesystem>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <shlobj.h>
    #include <shellapi.h>
#elif defined(__APPLE__)
    #include <cstdlib>
    #include <unistd.h>
    #include <pwd.h>
#else
    #include <cstdlib>
    #include <unistd.h>
#endif

namespace EnginePlatform {

std::string PlatformUtils::GetDocumentsDirectory() {
#if defined(_WIN32)
    wchar_t myDocsPath[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_MYDOCUMENTS, NULL, 0, myDocsPath))) {
        char pathUtf8[MAX_PATH * 3] = {};
        WideCharToMultiByte(CP_UTF8, 0, myDocsPath, -1, pathUtf8, sizeof(pathUtf8), NULL, NULL);
        return std::string(pathUtf8);
    }
    const char* userProfile = std::getenv("USERPROFILE");
    if (userProfile) {
        return (std::filesystem::path(userProfile) / "Documents").string();
    }
    return "Documents";
#elif defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
    return "/data/Idontknow";
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        if (pw) home = pw->pw_dir;
    }
    if (home) {
        return (std::filesystem::path(home) / "Documents").string();
    }
    return "Documents";
#else
    const char* home = std::getenv("HOME");
    if (home) {
        return (std::filesystem::path(home) / "Documents").string();
    }
    return "Documents";
#endif
}

std::string PlatformUtils::GetEngineUserDirectory() {
    if(EngineProject::active){auto path=EngineProject::current.Root()/"Saved";std::filesystem::create_directories(path);return path.string();}
#if defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
    return "/data/Idontknow/USER";
#else
    std::filesystem::path base = GetDocumentsDirectory();
    std::filesystem::path full = base / "MyEngine" / "STEAMUSER";
    std::error_code ec;
    std::filesystem::create_directories(full, ec);
    return full.string();
#endif
}

void PlatformUtils::ShowAlert(const std::string& title, const std::string& message, bool isError) {
#if defined(_WIN32)
    UINT type = MB_OK | (isError ? (MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND) : MB_ICONINFORMATION);
    MessageBoxA(nullptr, message.c_str(), title.c_str(), type);
#else
    std::cerr << "[" << (isError ? "ERROR" : "INFO") << "] " << title << ": " << message << std::endl;
#endif
}

void PlatformUtils::LogMessage(const std::string& message) {
#if defined(_WIN32)
    EngineLog::Write(message);
#else
    std::cout << message << std::endl;
#endif
}

void PlatformUtils::OpenURL(const std::string& url) {
#if defined(_WIN32)
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    std::string cmd = "open \"" + url + "\"";
    std::system(cmd.c_str());
#else
    std::string cmd = "xdg-open \"" + url + "\"";
    std::system(cmd.c_str());
#endif
}

std::string PlatformUtils::GetProjectRootDir() {
    if(EngineProject::active)return EngineProject::Utf8(EngineProject::current.Root());
#if defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
    return "/app0";
#elif defined(_WIN32)
    namespace fs = std::filesystem;
    std::error_code ec;
    wchar_t exePath[MAX_PATH] = {};
    if (GetModuleFileNameW(NULL, exePath, MAX_PATH) > 0) {
        fs::path exeDir = fs::path(exePath).parent_path();
        std::vector<fs::path> candidates = {
            exeDir.parent_path(),
            exeDir,
            fs::current_path(),
            "D:/MyEngine",
            "d:/MyEngine"
        };
        for (const auto& c : candidates) {
            if (c.empty()) continue;
            if (fs::exists(c / "src" / "Content", ec) || fs::exists(c / "build.bat", ec)) {
                return fs::weakly_canonical(c, ec).generic_string();
            }
        }
        std::string folderName = exeDir.filename().string();
        if (folderName == "EditorBuild" || folderName == "ReleaseBuild" ||
            folderName == "ServerBuild" || folderName == "ServerBuild_Linux") {
            return fs::weakly_canonical(exeDir.parent_path(), ec).generic_string();
        }
    }
    if (fs::exists("src/Content", ec)) return fs::weakly_canonical(".", ec).generic_string();
    if (fs::exists("../src/Content", ec)) return fs::weakly_canonical("..", ec).generic_string();
    return fs::current_path().generic_string();
#else
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::exists("src/Content", ec)) return fs::weakly_canonical(".", ec).generic_string();
    if (fs::exists("../src/Content", ec)) return fs::weakly_canonical("..", ec).generic_string();
    return fs::current_path().generic_string();
#endif
}

std::string PlatformUtils::GetContentRoot() {
    if(EngineProject::active)return EngineProject::Utf8(EngineProject::current.Resolve(EngineProject::current.content));
#if defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
    return "/app0/Contents";
#endif
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path projectRoot = GetProjectRootDir();
    fs::path srcContent = projectRoot / "src" / "Content";
    if (fs::exists(srcContent, ec)) {
        return srcContent.generic_string();
    }
    fs::path contentsDir = fs::path(projectRoot) / "Contents";
    if (fs::exists(contentsDir, ec)) {
        return contentsDir.generic_string();
    }
    return srcContent.generic_string();
}

std::string PlatformUtils::ResolveContentPath(const std::string& subpath) {
    std::string root = GetContentRoot();
    if (subpath.empty()) return root;

    std::string p = subpath;
    for (char& c : p) {
        if (c == '\\') c = '/';
    }

    if (p.rfind("src/Content/", 0) == 0) {
        return root + "/" + p.substr(12);
    }
    if (p == "src/Content") {
        return root;
    }
    if (p.rfind("../src/Content/", 0) == 0) {
        return root + "/" + p.substr(15);
    }
    if (p == "../src/Content") {
        return root;
    }
    if (p.rfind("Contents/", 0) == 0) {
        return root + "/" + p.substr(9);
    }
    if (p == "Contents") {
        return root;
    }
    if (!p.empty() && p[0] == '/') {
        return root + p;
    }
    return root + "/" + p;
}

} // namespace EnginePlatform
