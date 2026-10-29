#pragma once

#if defined(_WIN32)
#include <windows.h>
#include <chrono>
#include <filesystem>
#include <system_error>
#include <thread>

namespace EngineProject {
// Directory handles held briefly by Windows services can block a rename.
// Never replace an existing destination; return the original error to the caller.
inline std::error_code RenameBuildDirectory(const std::filesystem::path& from,
                                            const std::filesystem::path& to) {
    std::error_code error;
    for (int attempt = 0; attempt < 25; ++attempt) {
        if (MoveFileW(from.c_str(), to.c_str())) return {};
        error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
        const int code = error.value();
        if (code != ERROR_ACCESS_DENIED && code != ERROR_SHARING_VIOLATION && code != ERROR_LOCK_VIOLATION)
            break;
        if (attempt != 24) std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return error;
}
}
#endif
