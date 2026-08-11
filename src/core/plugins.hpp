#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pp {

enum class PluginScope { Global, Project };

struct PluginCommand {
  std::string name;
  std::string script;
  std::string description;
};

struct PluginInfo {
  std::string name;
  std::string version;
  std::string description;
  std::string prefix;
  PluginScope scope = PluginScope::Global;
  std::filesystem::path root;
  std::map<std::string, std::string> vars;
  std::vector<PluginCommand> commands;
  // Empty = all platforms. Values: "win", "windows", "darwin", "macos", "mac".
  std::vector<std::string> platforms;
};

std::filesystem::path globalPluginsDir();
std::filesystem::path projectsGlobalPluginsDir();
std::filesystem::path projectPluginsDir(const std::filesystem::path& project);

bool loadPluginManifest(const std::filesystem::path& pluginRoot, PluginInfo& out);
std::vector<PluginInfo> discoverPlugins(const std::filesystem::path& project = {});
std::optional<PluginInfo> findPluginByPrefix(const std::string& prefix,
                                             const std::filesystem::path& project = {});
std::optional<PluginCommand> findPluginCommand(const PluginInfo& plugin, const std::string& name);

bool runPluginCommand(const PluginInfo& plugin, const std::string& commandName,
                      const std::vector<std::string>& args,
                      const std::filesystem::path& project = {});

bool installBundledPlugins();
void printPluginHelp(const PluginInfo& plugin);

}  // namespace pp
