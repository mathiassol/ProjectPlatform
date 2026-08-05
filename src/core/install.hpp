#pragma once

#include <filesystem>
#include <string>

namespace pp {

bool installSelf();
bool uninstallSelf();
bool installHook();
bool uninstallHook();
bool refreshHookScript();
bool ensureHookScriptFresh();
bool installBinaryToPath(const std::filesystem::path& src, bool updatePath);
bool replaceInstalledBinary(const std::filesystem::path& src, std::string& errorOut);
bool addInstallDirToPath();
std::string getExePath();

}  // namespace pp
