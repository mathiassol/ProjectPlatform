#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pp {

namespace fs = std::filesystem;

struct Config {
  fs::path projects_dir;
  fs::path templates_dir;
  std::string version = "1.0.0";
  std::string editor;  // "zed", path to exe, or empty for OS default
};

Config loadConfig();
void saveConfig(const Config& cfg);

fs::path appDataDir();
fs::path installDir();
fs::path configPath();
fs::path statePath();
fs::path defaultProjectsDir();
fs::path defaultTemplatesDir();
fs::path globalScriptsDir();
fs::path projectScriptsDir(const fs::path& project);
fs::path hookScriptPath();

fs::path knownFolderDocuments();
fs::path expandEnv(const std::string& s);
fs::path resolvePath(const fs::path& p);

bool ensureDir(const fs::path& p);
std::string toDisplayPath(const fs::path& p);

}  // namespace pp
