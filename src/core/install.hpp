#pragma once

#include <filesystem>
#include <string>

namespace pp {

bool installSelf();
bool uninstallSelf();
bool installHook();
bool uninstallHook();
bool refreshHookScript();
bool installBinaryToPath(const std::filesystem::path& src, bool updatePath);
bool addInstallDirToPath();
std::string getExePath();

}  // namespace pp
