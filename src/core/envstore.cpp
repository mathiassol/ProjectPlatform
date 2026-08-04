#include "core/envstore.hpp"

#include "core/secrets.hpp"
#include "util/output.hpp"
#include "util/paths.hpp"
#include "util/progress.hpp"

#include <algorithm>
#include <fstream>
#include <functional>
#include <sstream>

namespace pp {
namespace fs = std::filesystem;

static std::string trim(std::string s) {
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
  size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
  return s.substr(i);
}

static std::string unquote(std::string v) {
  v = trim(v);
  if (v.size() >= 2 && ((v.front() == '"' && v.back() == '"') || (v.front() == '\'' && v.back() == '\'')))
    return v.substr(1, v.size() - 2);
  return v;
}

std::string psEscape(const std::string& value) {
  std::string out = "'";
  for (char c : value) {
    if (c == '\'') out += "''";
    else out.push_back(c);
  }
  out += "'";
  return out;
}

EnvStorePaths envPaths(EnvScope scope, const fs::path& project) {
  EnvStorePaths p;
  if (scope == EnvScope::Global) {
    const auto root = appDataDir() / "env";
    p.vars_file = root / "vars.env";
    p.secrets_file = root / "secrets.env";
    p.remembered_file = root / "remembered.txt";
    p.session_file = root / "session.keys";
  } else {
    const auto root = project / ".pp";
    p.vars_file = root / "vars.env";
    p.secrets_file = root / "secrets.env";
    p.remembered_file = root / "remembered.txt";
    p.session_file = root / "session.keys";
  }
  return p;
}

bool parseDotEnvFile(const fs::path& path, std::vector<EnvVar>& out, bool) {
  std::ifstream in(path);
  if (!in) return false;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    if (line.rfind("export ", 0) == 0) line = trim(line.substr(7));
    const auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    EnvVar v;
    v.key = trim(line.substr(0, eq));
    v.value = unquote(trim(line.substr(eq + 1)));
    v.source = path.filename().string();
    if (!v.key.empty()) out.push_back(std::move(v));
  }
  return true;
}

bool loadVarsFile(const fs::path& path, std::map<std::string, EnvVar>& out) {
  std::vector<EnvVar> vars;
  if (!fs::exists(path)) return true;
  if (!parseDotEnvFile(path, vars, false)) return false;
  for (auto& v : vars) {
    v.secret = false;
    v.source = "store";
    out[v.key] = std::move(v);
  }
  return true;
}

bool saveVarsFile(const fs::path& path, const std::map<std::string, EnvVar>& vars) {
  ensureDir(path.parent_path());
  std::ofstream out(path);
  out << "# ProjectPlatform vars (non-secret)\n";
  for (const auto& [k, v] : vars) {
    if (!v.secret) out << k << "=" << v.value << "\n";
  }
  return static_cast<bool>(out);
}

bool loadSecretsFile(const fs::path& path, std::map<std::string, EnvVar>& out) {
  if (!fs::exists(path)) return true;
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    const auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    EnvVar v;
    v.key = trim(line.substr(0, eq));
    const auto enc = trim(line.substr(eq + 1));
    if (!dpapiDecrypt(enc, v.value)) continue;
    v.secret = true;
    v.source = "secret";
    out[v.key] = std::move(v);
  }
  return true;
}

bool saveSecretsFile(const fs::path& path, const std::map<std::string, EnvVar>& vars) {
  ensureDir(path.parent_path());
  std::ofstream out(path);
  out << "# ProjectPlatform secrets (DPAPI encrypted, user-local)\n";
  for (const auto& [k, v] : vars) {
    if (!v.secret) continue;
    std::string enc;
    if (!dpapiEncrypt(v.value, enc)) continue;
    out << k << "=" << enc << "\n";
  }
  return static_cast<bool>(out);
}

bool loadRememberedFiles(const fs::path& path, std::vector<std::string>& out) {
  out.clear();
  if (!fs::exists(path)) return true;
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    line = trim(line);
    if (!line.empty() && line[0] != '#') out.push_back(line);
  }
  return true;
}

bool saveRememberedFiles(const fs::path& path, const std::vector<std::string>& files) {
  ensureDir(path.parent_path());
  std::ofstream out(path);
  out << "# Files auto-loaded by pp env apply\n";
  for (const auto& f : files) out << f << "\n";
  return static_cast<bool>(out);
}

static void loadScopeInto(std::map<std::string, EnvVar>& merged, EnvScope scope,
                          const fs::path& project) {
  const auto paths = envPaths(scope, project);
  std::map<std::string, EnvVar> vars, secrets;
  loadVarsFile(paths.vars_file, vars);
  loadSecretsFile(paths.secrets_file, secrets);
  for (const auto& [k, v] : vars) merged[k] = v;
  for (const auto& [k, v] : secrets) merged[k] = v;
}

bool setVar(EnvScope scope, const fs::path& project, const std::string& key,
            const std::string& value, bool secret) {
  const auto paths = envPaths(scope, project);
  std::map<std::string, EnvVar> vars, secrets;
  loadVarsFile(paths.vars_file, vars);
  loadSecretsFile(paths.secrets_file, secrets);

  EnvVar v{key, value, secret, secret ? "secret" : "store"};
  if (secret) {
    secrets[key] = v;
    vars.erase(key);
    saveSecretsFile(paths.secrets_file, secrets);
    saveVarsFile(paths.vars_file, vars);
  } else {
    vars[key] = v;
    secrets.erase(key);
    saveVarsFile(paths.vars_file, vars);
    saveSecretsFile(paths.secrets_file, secrets);
  }
  return true;
}

bool unsetVar(EnvScope scope, const fs::path& project, const std::string& key) {
  const auto paths = envPaths(scope, project);
  std::map<std::string, EnvVar> vars, secrets;
  loadVarsFile(paths.vars_file, vars);
  loadSecretsFile(paths.secrets_file, secrets);
  vars.erase(key);
  secrets.erase(key);
  saveVarsFile(paths.vars_file, vars);
  saveSecretsFile(paths.secrets_file, secrets);
  return true;
}

std::optional<EnvVar> getVar(EnvScope scope, const fs::path& project, const std::string& key) {
  const auto paths = envPaths(scope, project);
  std::map<std::string, EnvVar> vars, secrets;
  loadVarsFile(paths.vars_file, vars);
  loadSecretsFile(paths.secrets_file, secrets);
  if (secrets.count(key)) return secrets[key];
  if (vars.count(key)) return vars[key];
  return std::nullopt;
}

std::vector<EnvVar> listVars(EnvScope scope, const fs::path& project, bool showSecrets) {
  std::map<std::string, EnvVar> merged;
  loadScopeInto(merged, scope, project);
  std::vector<EnvVar> out;
  for (const auto& [k, v] : merged) {
    EnvVar copy = v;
    if (copy.secret && !showSecrets) copy.value = "********";
    out.push_back(std::move(copy));
  }
  return out;
}

bool rememberFile(EnvScope scope, const fs::path& project, const fs::path& filePath) {
  const auto paths = envPaths(scope, project);
  std::vector<std::string> files;
  loadRememberedFiles(paths.remembered_file, files);
  const auto rel = filePath.generic_string();
  if (std::find(files.begin(), files.end(), rel) == files.end()) files.push_back(rel);
  return saveRememberedFiles(paths.remembered_file, files);
}

bool forgetFile(EnvScope scope, const fs::path& project, const fs::path& filePath) {
  const auto paths = envPaths(scope, project);
  std::vector<std::string> files;
  loadRememberedFiles(paths.remembered_file, files);
  const auto rel = filePath.generic_string();
  files.erase(std::remove(files.begin(), files.end(), rel), files.end());
  return saveRememberedFiles(paths.remembered_file, files);
}

std::vector<fs::path> listRememberedFiles(EnvScope scope, const fs::path& project) {
  const auto paths = envPaths(scope, project);
  std::vector<std::string> files;
  loadRememberedFiles(paths.remembered_file, files);
  std::vector<fs::path> out;
  for (const auto& f : files) out.push_back(f);
  return out;
}

std::vector<fs::path> discoverEnvFiles(const fs::path& project) {
  static const char* kNames[] = {".env", ".env.local", ".envrc", ".env.development", ".env.production"};
  std::vector<fs::path> found;
  for (const char* name : kNames) {
    const auto p = project / name;
    if (fs::exists(p)) found.push_back(p);
  }
  return found;
}

EnvBundle collectEnv(EnvScope scope, const fs::path& project, bool includeRemembered) {
  EnvBundle bundle;
  std::map<std::string, EnvVar> merged;

  if (scope == EnvScope::Global || scope == EnvScope::Project) {
    loadScopeInto(merged, EnvScope::Global, {});
  }
  if (scope == EnvScope::Project && !project.empty()) {
    loadScopeInto(merged, EnvScope::Project, project);

    if (includeRemembered) {
      const auto paths = envPaths(EnvScope::Project, project);
      std::vector<std::string> remembered;
      loadRememberedFiles(paths.remembered_file, remembered);
      for (const auto& rel : remembered) {
        const auto file = project / fs::path(rel);
        std::vector<EnvVar> fromFile;
        if (parseDotEnvFile(file, fromFile, false)) {
          for (auto& v : fromFile) merged[v.key] = v;
        }
      }
      for (const auto& discovered : discoverEnvFiles(project)) {
        const auto rel = fs::relative(discovered, project).generic_string();
        if (std::find(remembered.begin(), remembered.end(), rel) != remembered.end()) continue;
      }
    }
  }

  for (const auto& [k, v] : merged) {
    bundle.vars.push_back(v);
    bundle.keys.push_back(k);
  }
  return bundle;
}

std::string formatPowerShellApply(const EnvBundle& bundle) {
  std::ostringstream ps;
  ps << "$script:PP_ENV_KEYS = @(";
  for (size_t i = 0; i < bundle.keys.size(); ++i) {
    if (i) ps << ',';
    ps << psEscape(bundle.keys[i]);
  }
  ps << ")\n";
  for (const auto& v : bundle.vars) ps << "$env:" << v.key << " = " << psEscape(v.value) << "\n";
  ps << "$env:PP_ENV_LOADED = '1'\n";
  return ps.str();
}

std::string formatPowerShellClear(const std::vector<std::string>& keys) {
  std::ostringstream ps;
  ps << "if ($script:PP_ENV_KEYS) { $keys = $script:PP_ENV_KEYS } elseif ($env:PP_ENV_KEYS) { $keys = $env:PP_ENV_KEYS -split ',' } else { $keys = @() }\n";
  ps << "foreach ($k in $keys) { Remove-Item -Path (\"Env:\" + $k) -ErrorAction SilentlyContinue }\n";
  ps << "$script:PP_ENV_KEYS = @()\n";
  ps << "Remove-Item Env:PP_ENV_LOADED -ErrorAction SilentlyContinue\n";
  for (const auto& k : keys)
    ps << "Remove-Item Env:" << k << " -ErrorAction SilentlyContinue\n";
  return ps.str();
}

bool saveSessionKeys(const fs::path& sessionFile, const std::vector<std::string>& keys) {
  ensureDir(sessionFile.parent_path());
  std::ofstream out(sessionFile);
  for (const auto& k : keys) out << k << "\n";
  return static_cast<bool>(out);
}

std::vector<std::string> loadSessionKeys(const fs::path& sessionFile) {
  std::vector<std::string> keys;
  if (!fs::exists(sessionFile)) return keys;
  std::ifstream in(sessionFile);
  std::string line;
  while (std::getline(in, line)) {
    line = trim(line);
    if (!line.empty()) keys.push_back(line);
  }
  return keys;
}

}  // namespace pp
