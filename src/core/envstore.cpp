#include "core/envstore.hpp"

#include "core/secrets.hpp"
#include "core/install.hpp"
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

static void stripUtf8Bom(std::string& s) {
  if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF &&
      static_cast<unsigned char>(s[1]) == 0xBB && static_cast<unsigned char>(s[2]) == 0xBF) {
    s.erase(0, 3);
  }
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
    stripUtf8Bom(line);
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    if (line.rfind("export ", 0) == 0) line = trim(line.substr(7));
    const auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    EnvVar v;
    v.key = trim(line.substr(0, eq));
    v.value = unquote(trim(line.substr(eq + 1)));
    while (!v.value.empty() && v.value.back() == ';') v.value.pop_back();
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
  out << "# ProjectPlatform secrets (encrypted, user-local)\n";
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

fs::path profilesDir(EnvScope scope, const fs::path& project) {
  if (scope == EnvScope::Global) return appDataDir() / "env" / "profiles";
  return project / ".pp" / "env" / "profiles";
}

fs::path profileFile(EnvScope scope, const fs::path& project, const std::string& name) {
  std::string n = name;
  if (n.size() >= 4 && n.substr(n.size() - 4) == ".env") n = n.substr(0, n.size() - 4);
  return profilesDir(scope, project) / (n + ".env");
}

fs::path activeProfileFile(EnvScope scope, const fs::path& project) {
  if (scope == EnvScope::Global) return appDataDir() / "env" / "active.profile";
  return project / ".pp" / "active.profile";
}

static fs::path bundledEnvTemplatesDir() {
  const auto exe = getExePath();
  if (exe.empty()) return {};
  fs::path p = exe;
  p = p.parent_path();
  for (int i = 0; i < 8; ++i) {
    const auto cand = p / "assets" / "env-templates";
    if (fs::exists(cand)) return cand;
    if (p == p.parent_path()) break;
    p = p.parent_path();
  }
  return appDataDir() / "env-templates";
}

fs::path envTemplatePath(const std::string& name) {
  std::string n = name;
  if (n.size() < 4 || n.substr(n.size() - 4) != ".env") n += ".env";
  const auto dir = bundledEnvTemplatesDir();
  const auto path = dir / n;
  if (fs::exists(path)) return path;
  return {};
}

std::vector<std::string> listProfiles(EnvScope scope, const fs::path& project) {
  std::vector<std::string> names;
  const auto dir = profilesDir(scope, project);
  if (!fs::exists(dir)) return names;
  for (const auto& entry : fs::directory_iterator(dir)) {
    if (!entry.is_regular_file()) continue;
    if (entry.path().extension() != ".env") continue;
    names.push_back(entry.path().stem().string());
  }
  std::sort(names.begin(), names.end());
  return names;
}

std::optional<std::string> getActiveProfile(EnvScope scope, const fs::path& project) {
  const auto path = activeProfileFile(scope, project);
  if (!fs::exists(path)) return std::nullopt;
  std::ifstream in(path);
  std::string line;
  if (!std::getline(in, line)) return std::nullopt;
  line = trim(line);
  if (line.empty() || line[0] == '#') return std::nullopt;
  return line;
}

bool setActiveProfile(EnvScope scope, const fs::path& project, const std::string& name) {
  const auto file = profileFile(scope, project, name);
  if (!fs::exists(file)) return false;
  ensureDir(activeProfileFile(scope, project).parent_path());
  std::ofstream out(activeProfileFile(scope, project));
  out << name << "\n";
  return static_cast<bool>(out);
}

static bool writeDefaultProfileTemplate(const fs::path& dest) {
  std::ofstream out(dest);
  out << "# PP env profile — edit with: pp env edit\n";
  out << "# export KEY=value\n\n";
  out << "export EXAMPLE_KEY=change-me\n";
  return static_cast<bool>(out);
}

bool createProfile(EnvScope scope, const fs::path& project, const std::string& name,
                   const fs::path& from, const std::string& templateName) {
  const auto dest = profileFile(scope, project, name);
  if (fs::exists(dest)) return false;
  ensureDir(dest.parent_path());

  if (!from.empty() && fs::exists(from)) {
    std::error_code ec;
    fs::copy_file(from, dest, ec);
    return !ec;
  }
  if (!templateName.empty()) {
    const auto tpl = envTemplatePath(templateName);
    if (!tpl.empty() && fs::exists(tpl)) {
      std::error_code ec;
      fs::copy_file(tpl, dest, ec);
      return !ec;
    }
  }
  return writeDefaultProfileTemplate(dest);
}

bool importProfile(EnvScope scope, const fs::path& project, const std::string& name,
                   const fs::path& source) {
  if (!fs::exists(source)) return false;
  const auto dest = profileFile(scope, project, name);
  ensureDir(dest.parent_path());
  std::error_code ec;
  fs::copy_file(source, dest, fs::copy_options::overwrite_existing, ec);
  return !ec;
}

bool installEnvTemplates() {
  const auto src = bundledEnvTemplatesDir();
  if (src.empty() || !fs::exists(src)) return true;
  const auto dest = appDataDir() / "env-templates";
  ensureDir(dest);
  for (const auto& entry : fs::directory_iterator(src)) {
    if (!entry.is_regular_file()) continue;
    std::error_code ec;
    fs::copy_file(entry.path(), dest / entry.path().filename(),
                  fs::copy_options::overwrite_existing, ec);
  }
  return true;
}

bool resolveProfilePath(EnvScope scope, const fs::path& project, const std::string& nameOrPath,
                        fs::path& out) {
  if (nameOrPath.find('/') != std::string::npos || nameOrPath.find('\\') != std::string::npos ||
      (nameOrPath.size() > 1 && nameOrPath[1] == ':')) {
    out = fs::path(nameOrPath);
    return fs::exists(out);
  }
  out = profileFile(scope, project, nameOrPath);
  return fs::exists(out);
}

static void mergeProfileFile(std::map<std::string, EnvVar>& merged, const fs::path& file) {
  std::vector<EnvVar> fromFile;
  if (!parseDotEnvFile(file, fromFile, false)) return;
  for (auto& v : fromFile) {
    v.source = "profile:" + file.stem().string();
    merged[v.key] = std::move(v);
  }
}

EnvBundle collectEnv(EnvScope scope, const fs::path& project, bool includeRemembered,
                     const std::string& profileName) {
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
        mergeProfileFile(merged, file);
      }
    }
  }

  std::string active = profileName;
  if (active.empty()) {
    if (auto a = getActiveProfile(scope, project)) active = *a;
  }
  if (!active.empty()) {
    const auto pf = profileFile(scope, project, active);
    if (fs::exists(pf)) mergeProfileFile(merged, pf);
  }

  for (const auto& [k, v] : merged) {
    bundle.vars.push_back(v);
    bundle.keys.push_back(k);
  }
  return bundle;
}

static std::string jsonEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += c;
    }
  }
  return out;
}

std::string formatJsonApply(const EnvBundle& bundle) {
  std::ostringstream o;
  o << "{\"keys\":[";
  for (size_t i = 0; i < bundle.keys.size(); ++i) {
    if (i) o << ',';
    o << '"' << jsonEscape(bundle.keys[i]) << '"';
  }
  o << "],\"vars\":{";
  for (size_t i = 0; i < bundle.vars.size(); ++i) {
    if (i) o << ',';
    o << '"' << jsonEscape(bundle.vars[i].key) << "\":\"" << jsonEscape(bundle.vars[i].value) << '"';
  }
  o << "}}";
  return o.str();
}

std::string formatJsonClear(const std::vector<std::string>& keys) {
  std::ostringstream o;
  o << "{\"clear\":true,\"keys\":[";
  for (size_t i = 0; i < keys.size(); ++i) {
    if (i) o << ',';
    o << '"' << jsonEscape(keys[i]) << '"';
  }
  o << "]}";
  return o.str();
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
