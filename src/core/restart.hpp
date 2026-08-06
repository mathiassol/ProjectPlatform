#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pp {

struct RestartSession {
  std::string cwd;
  std::string project;
  std::string project_path;
  std::vector<std::string> env_keys;
  std::map<std::string, std::string> env_vars;
};

std::filesystem::path restartSessionPath();
bool saveRestartSession(const RestartSession& session);
std::optional<RestartSession> loadRestartSession(const std::filesystem::path& path);
bool clearRestartSession(const std::filesystem::path& path);
bool captureRestartSession(RestartSession& session);
bool spawnRestartTerminal(const std::filesystem::path& sessionPath);
bool restoreRestartSession();

}  // namespace pp
