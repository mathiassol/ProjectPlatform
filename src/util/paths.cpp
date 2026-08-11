#include "util/paths.hpp"

#include "platform/platform.hpp"

#include <cstdlib>
#include <fstream>
#include <regex>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace pp {

fs::path knownFolderDocuments() { return platform::userDocumentsDir(); }

fs::path expandEnv(const std::string& s) {
#if defined(_WIN32)
  std::string out;
  out.resize(32768);
  DWORD n = ExpandEnvironmentStringsA(s.c_str(), out.data(), static_cast<DWORD>(out.size()));
  if (n == 0 || n > out.size()) return fs::path(s);
  out.resize(n > 0 ? n - 1 : 0);
  return fs::path(out);
#else
  // Minimal: expand $HOME / ${HOME} and leave the rest as-is (Phase 1 can deepen).
  std::string out = s;
  if (auto home = platform::getEnv("HOME")) {
    const std::string h = *home;
    size_t pos = 0;
    while ((pos = out.find("${HOME}", pos)) != std::string::npos) {
      out.replace(pos, 7, h);
      pos += h.size();
    }
    pos = 0;
    while ((pos = out.find("$HOME", pos)) != std::string::npos) {
      out.replace(pos, 5, h);
      pos += h.size();
    }
  }
  return fs::path(out);
#endif
}

fs::path resolvePath(const fs::path& p) {
  if (p.is_absolute()) return fs::weakly_canonical(p);
  return fs::weakly_canonical(fs::absolute(p));
}

fs::path appDataDir() { return platform::appDataRoot(); }

fs::path installDir() { return appDataDir() / "bin"; }

fs::path configPath() { return appDataDir() / "config.json"; }

fs::path statePath() { return appDataDir() / "state.json"; }

fs::path hookScriptPath() {
#if defined(_WIN32)
  return appDataDir() / "pp-hook.ps1";
#else
  return appDataDir() / "pp-hook.zsh";
#endif
}

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

static std::string jsonUnescape(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      switch (s[i + 1]) {
        case '\\': out += '\\'; ++i; break;
        case '"': out += '"'; ++i; break;
        case 'n': out += '\n'; ++i; break;
        case 'r': out += '\r'; ++i; break;
        case 't': out += '\t'; ++i; break;
        default: out += s[i]; break;
      }
    } else {
      out += s[i];
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

  if (auto v = jsonGetString(json, "projects_dir"))
    cfg.projects_dir = resolvePath(expandEnv(jsonUnescape(*v)));
  if (auto v = jsonGetString(json, "templates_dir"))
    cfg.templates_dir = resolvePath(expandEnv(jsonUnescape(*v)));
  if (auto v = jsonGetString(json, "version")) cfg.version = jsonUnescape(*v);
  if (auto v = jsonGetString(json, "editor")) cfg.editor = jsonUnescape(*v);
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
