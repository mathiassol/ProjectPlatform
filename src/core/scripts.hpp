#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace pp {

enum class ScriptScope { Global, Project };

struct ScriptInfo {
  std::string name;
  std::string ext;
  std::filesystem::path path;
  ScriptScope scope;
};

std::vector<ScriptInfo> listScripts(ScriptScope scope, const std::filesystem::path& project = {});
bool runScript(const std::string& name, ScriptScope scope, const std::filesystem::path& project,
               const std::vector<std::string>& args);
bool createScript(const std::string& name, const std::string& ext, ScriptScope scope,
                  const std::filesystem::path& project);
bool deleteScript(const std::string& name, ScriptScope scope, const std::filesystem::path& project,
                  bool force);

}  // namespace pp
