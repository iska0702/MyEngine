#pragma once

#include <string>
#include <vector>

namespace EnginePlatform {

class PlatformUtils {
public:
    // Get canonical user documents path (e.g. Documents/MyEngine/STEAMUSER)
    static std::string GetDocumentsDirectory();

    // Get user save/config path for STEAMUSER directory
    static std::string GetEngineUserDirectory();

    // Show platform alert/dialog box
    static void ShowAlert(const std::string& title, const std::string& message, bool isError = false);

    // Platform debug logging
    static void LogMessage(const std::string& message);

    // Open URL in default system browser
    static void OpenURL(const std::string& url);

    // Get the root directory of the project (e.g. D:/MyEngine)
    static std::string GetProjectRootDir();

    // Get the canonical Content root directory (e.g. D:/MyEngine/src/Content)
    static std::string GetContentRoot();

    // Resolve a path relative to Content root (e.g. "Prefabs" -> "D:/MyEngine/src/Content/Prefabs")
    static std::string ResolveContentPath(const std::string& subpath = "");
};

} // namespace EnginePlatform
