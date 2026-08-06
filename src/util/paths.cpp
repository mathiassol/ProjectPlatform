#include "util/paths.hpp"

#include <cstdlib>
#include <fstream>
#include <regex>
#include <windows.h>

namespace pp {

static fs::path envPath(const char* name) {
  const char* v = std::getenv(name);
  return v ? fs::path(v) : fs::path{};
}

fs::path knownFolderDocuments() {
  const char* home = std::getenv("USERPROFILE");
  if (home) return fs::path(home) / "Documents";
  return fs::path("Documents");
}

fs::path expandEnv(const std::string& s) {
  std::string out;
  out.resize(32768);
  DWORD n = ExpandEnvironmentStringsA(s.c_str(), out.data(), static_cast<DWORD>(out.size()));
  if (n == 0 || n > out.size()) return fs::path(s);
  out.resize(n > 0 ? n - 1 : 0);
  return fs::path(out);
}

fs::path resolvePath(const fs::path& p) {
  if (p.is_absolute()) return fs::weakly_canonical(p);
  return fs::weakly_canonical(fs::absolute(p));
}

fs::path appDataDir() { return envPath("LOCALAPPDATA") / "ProjectPlatform"; }

fs::path installDir() { return appDataDir() / "bin"; }

fs::path configPath() { return appDataDir() / "config.json"; }

fs::path statePath() { return appDataDir() / "state.json"; }

fs::path hookScriptPath() { return appDataDir() / "pp-hook.ps1"; }

fs::path defaultProjectsDir() { return knownFolderDocuments() / "Projects"; }

fs::path defaultTemplatesDir() { return knownFolderDocuments() / "Templates"; }

fs::path globalScriptsDir() {
  const auto cfg = loadConfig();
  return cfg.projects_dir / ".scripts";
}

fs::path projectScriptsDir(const fs::path& project) { return project / ".scripts"; }

bool ensureDir(const fs::path& p) {
  if (p.empty()) return false;
  std::error_code ec;
  fs::create_directories(p, ec);
  return !ec;
}

std::string toDisplayPath(const fs::path& p) { return p.string(); }

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

static std::optional<std::string> jsonGetString(const std::string& json, const std::string& key) {
  const std::regex re("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
  std::smatch m;
  if (std::regex_search(json, m, re) && m.size() > 1) return m[1].str();
  return std::nullopt;
}

Config loadConfig() {
  Config cfg;
  cfg.projects_dir = defaultProjectsDir();
  cfg.templates_dir = defaultTemplatesDir();

  const auto path = configPath();
  if (!fs::exists(path)) return cfg;

  std::ifstream in(path);
  if (!in) return cfg;
  std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

  if (auto v = jsonGetString(json, "projects_dir")) cfg.projects_dir = expandEnv(*v);
  if (auto v = jsonGetString(json, "templates_dir")) cfg.templates_dir = expandEnv(*v);
  if (auto v = jsonGetString(json, "version")) cfg.version = *v;
  if (auto v = jsonGetString(json, "editor")) cfg.editor = *v;
  return cfg;
}

void saveConfig(const Config& cfg) {
  ensureDir(appDataDir());
  std::ofstream out(configPath());
  out << "{\n"
      << "  \"projects_dir\": \"" << jsonEscape(cfg.projects_dir.string()) << "\",\n"
      << "  \"templates_dir\": \"" << jsonEscape(cfg.templates_dir.string()) << "\",\n"
      << "  \"version\": \"" << jsonEscape(cfg.version) << "\",\n"
      << "  \"editor\": \"" << jsonEscape(cfg.editor) << "\"\n"
      << "}\n";
}

}  // namespace pp
