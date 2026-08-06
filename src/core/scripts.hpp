#pragma once

#include <filesystem>
#include <optional>
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
std::optional<ScriptInfo> resolveScript(const std::string& name, ScriptScope scope,
                                        const std::filesystem::path& project, bool explicitScope);
bool runScript(const std::string& name, ScriptScope scope, const std::filesystem::path& project,
               const std::vector<std::string>& args, bool explicitScope = false);
bool createScript(const std::string& name, const std::string& ext, ScriptScope scope,
                  const std::filesystem::path& project);
bool deleteScript(const std::string& name, ScriptScope scope, const std::filesystem::path& project,
                  bool force, bool explicitScope = false);
bool editScript(const std::string& name, ScriptScope scope, const std::filesystem::path& project,
                bool explicitScope = false);

}  // namespace pp
