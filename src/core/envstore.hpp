#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pp {

enum class EnvScope { Global, Project };

struct EnvVar {
  std::string key;
  std::string value;
  bool secret = false;
  std::string source;  // "store", ".env", etc.
};

struct EnvStorePaths {
  std::filesystem::path vars_file;
  std::filesystem::path secrets_file;
  std::filesystem::path remembered_file;
  std::filesystem::path session_file;
};

EnvStorePaths envPaths(EnvScope scope, const std::filesystem::path& project = {});

bool parseDotEnvFile(const std::filesystem::path& path, std::vector<EnvVar>& out,
                     bool secretsFromSeparate = false);
bool loadVarsFile(const std::filesystem::path& path, std::map<std::string, EnvVar>& out);
bool saveVarsFile(const std::filesystem::path& path, const std::map<std::string, EnvVar>& vars);

bool loadSecretsFile(const std::filesystem::path& path, std::map<std::string, EnvVar>& out);
bool saveSecretsFile(const std::filesystem::path& path, const std::map<std::string, EnvVar>& vars);

bool loadRememberedFiles(const std::filesystem::path& path, std::vector<std::string>& out);
bool saveRememberedFiles(const std::filesystem::path& path, const std::vector<std::string>& files);

bool setVar(EnvScope scope, const std::filesystem::path& project, const std::string& key,
            const std::string& value, bool secret);
bool unsetVar(EnvScope scope, const std::filesystem::path& project, const std::string& key);
std::optional<EnvVar> getVar(EnvScope scope, const std::filesystem::path& project,
                             const std::string& key);
std::vector<EnvVar> listVars(EnvScope scope, const std::filesystem::path& project, bool showSecrets);

bool rememberFile(EnvScope scope, const std::filesystem::path& project,
                  const std::filesystem::path& filePath);
bool forgetFile(EnvScope scope, const std::filesystem::path& project,
                const std::filesystem::path& filePath);
std::vector<std::filesystem::path> listRememberedFiles(EnvScope scope,
                                                      const std::filesystem::path& project);

std::vector<std::filesystem::path> discoverEnvFiles(const std::filesystem::path& project);

struct EnvBundle {
  std::vector<EnvVar> vars;
  std::vector<std::string> keys;
};

EnvBundle collectEnv(EnvScope scope, const std::filesystem::path& project, bool includeRemembered,
                     const std::string& profileName = {});
std::string formatJsonApply(const EnvBundle& bundle);
std::string formatJsonClear(const std::vector<std::string>& keys);
std::string formatPowerShellApply(const EnvBundle& bundle);
std::string formatPowerShellClear(const std::vector<std::string>& keys);
bool saveSessionKeys(const std::filesystem::path& sessionFile, const std::vector<std::string>& keys);
std::vector<std::string> loadSessionKeys(const std::filesystem::path& sessionFile);

std::filesystem::path profilesDir(EnvScope scope, const std::filesystem::path& project = {});
std::filesystem::path profileFile(EnvScope scope, const std::filesystem::path& project,
                                  const std::string& name);
std::filesystem::path activeProfileFile(EnvScope scope, const std::filesystem::path& project);
std::filesystem::path envTemplatePath(const std::string& name);

std::vector<std::string> listProfiles(EnvScope scope, const std::filesystem::path& project);
std::optional<std::string> getActiveProfile(EnvScope scope, const std::filesystem::path& project);
bool setActiveProfile(EnvScope scope, const std::filesystem::path& project,
                      const std::string& name);
bool createProfile(EnvScope scope, const std::filesystem::path& project, const std::string& name,
                   const std::filesystem::path& from = {}, const std::string& templateName = {});
bool importProfile(EnvScope scope, const std::filesystem::path& project, const std::string& name,
                   const std::filesystem::path& source);
bool resolveProfilePath(EnvScope scope, const std::filesystem::path& project,
                        const std::string& nameOrPath, std::filesystem::path& out);
bool installEnvTemplates();

std::string psEscape(const std::string& value);

}  // namespace pp
