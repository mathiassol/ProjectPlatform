#include "core/plugins.hpp"

#include "core/install.hpp"
#include "core/projects.hpp"
#include "platform/platform.hpp"
#include "util/output.hpp"
#include "util/paths.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

namespace pp {
namespace fs = std::filesystem;

fs::path globalPluginsDir() { return appDataDir() / "plugins"; }

fs::path projectsGlobalPluginsDir() {
  return loadConfig().projects_dir / ".plugins";
}

fs::path projectPluginsDir(const fs::path& project) { return project / ".pp" / "plugins"; }

namespace {

std::string toLower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::optional<std::string> jsonGetString(const std::string& json, const std::string& key) {
  const std::regex re("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
  std::smatch m;
  if (std::regex_search(json, m, re) && m.size() > 1) return m[1].str();
  return std::nullopt;
}

std::map<std::string, std::string> jsonGetStringMap(const std::string& json, const std::string& key) {
  std::map<std::string, std::string> out;
  const std::regex block("\"" + key + "\"\\s*:\\s*\\{([\\s\\S]*?)\\}");
  std::smatch m;
  if (!std::regex_search(json, m, block) || m.size() < 2) return out;
  const std::regex item("\"([^\"]+)\"\\s*:\\s*\"([^\"]*)\"");
  auto begin = std::sregex_iterator(m[1].first, m[1].second, item);
  auto end = std::sregex_iterator();
  for (auto it = begin; it != end; ++it) out[(*it)[1].str()] = (*it)[2].str();
  return out;
}

std::vector<std::string> jsonGetStringArray(const std::string& json, const std::string& key) {
  std::vector<std::string> out;
  const std::regex block("\"" + key + "\"\\s*:\\s*\\[([^\\]]*)\\]");
  std::smatch m;
  if (!std::regex_search(json, m, block) || m.size() < 2) return out;
  const std::regex item("\"([^\"]+)\"");
  auto begin = std::sregex_iterator(m[1].first, m[1].second, item);
  auto end = std::sregex_iterator();
  for (auto it = begin; it != end; ++it) out.push_back((*it)[1].str());
  return out;
}

bool platformMatches(const std::string& token) {
  const auto t = toLower(token);
#if defined(_WIN32)
  return t == "win" || t == "windows" || t == "win32" || t == "win64";
#elif defined(__APPLE__)
  return t == "darwin" || t == "macos" || t == "mac" || t == "osx";
#else
  return t == "linux" || t == "unix";
#endif
}

bool pluginSupportsCurrentOs(const PluginInfo& info) {
  if (info.platforms.empty()) return true;
  for (const auto& p : info.platforms) {
    if (platformMatches(p)) return true;
  }
  return false;
}

void loadCommandsFromMap(const std::map<std::string, std::string>& cmds, PluginInfo& out) {
  out.commands.clear();
  for (const auto& [name, script] : cmds) {
    PluginCommand c;
    c.name = name;
    c.script = script;
    c.description = script;
    out.commands.push_back(std::move(c));
  }
}

bool commandOnPath(const char* name) {
#if defined(_WIN32)
  const std::string cmd = std::string("where ") + name + " >nul 2>&1";
#else
  const std::string cmd = std::string("command -v ") + name + " >/dev/null 2>&1";
#endif
  return std::system(cmd.c_str()) == 0;
}

std::string shellQuote(const std::string& s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') out.push_back('\\');
    out.push_back(c);
  }
  out.push_back('"');
  return out;
}

fs::path resolvePluginScript(const PluginInfo& plugin, const PluginCommand& cmd) {
  fs::path script = plugin.root / cmd.script;
#if !defined(_WIN32)
  if (script.extension() == ".ps1") {
    fs::path sh = script;
    sh.replace_extension(".sh");
    if (fs::exists(sh)) return sh;
  }
#endif
  return script;
}

std::string joinArgsQuoted(const std::vector<std::string>& args) {
  std::string out;
  for (const auto& a : args) {
    out += " ";
    out += shellQuote(a);
  }
  return out;
}

}  // namespace

bool loadPluginManifest(const fs::path& pluginRoot, PluginInfo& out) {
  const auto manifest = pluginRoot / "plugin.json";
  std::ifstream in(manifest);
  if (!in) return false;
  const std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

  out = {};
  out.root = pluginRoot;
  if (auto v = jsonGetString(json, "name")) out.name = *v;
  if (auto v = jsonGetString(json, "version")) out.version = *v;
  if (auto v = jsonGetString(json, "description")) out.description = *v;
  if (auto v = jsonGetString(json, "prefix")) out.prefix = *v;
  if (auto v = jsonGetString(json, "scope")) {
    if (*v == "project") out.scope = PluginScope::Project;
    else out.scope = PluginScope::Global;
  }
  out.vars = jsonGetStringMap(json, "vars");
  out.platforms = jsonGetStringArray(json, "platforms");

  auto cmds = jsonGetStringMap(json, "commands");
#if defined(__APPLE__)
  auto darwinCmds = jsonGetStringMap(json, "commands_darwin");
  if (darwinCmds.empty()) darwinCmds = jsonGetStringMap(json, "commands_macos");
  if (!darwinCmds.empty()) cmds = std::move(darwinCmds);
#endif
  loadCommandsFromMap(cmds, out);

  return !out.name.empty() && !out.prefix.empty() && !out.commands.empty();
}

static void scanPluginDir(const fs::path& dir, PluginScope scope, std::vector<PluginInfo>& out) {
  if (!fs::exists(dir)) return;
  for (const auto& entry : fs::directory_iterator(dir)) {
    if (!entry.is_directory()) continue;
    const auto manifest = entry.path() / "plugin.json";
    if (!fs::exists(manifest)) continue;
    PluginInfo info;
    if (!loadPluginManifest(entry.path(), info)) continue;
    if (!pluginSupportsCurrentOs(info)) continue;
    info.scope = scope;
    info.root = entry.path();
    out.push_back(std::move(info));
  }
}

std::vector<PluginInfo> discoverPlugins(const fs::path& project) {
  std::vector<PluginInfo> plugins;
  if (!project.empty()) scanPluginDir(projectPluginsDir(project), PluginScope::Project, plugins);
  scanPluginDir(globalPluginsDir(), PluginScope::Global, plugins);
  scanPluginDir(projectsGlobalPluginsDir(), PluginScope::Global, plugins);
  return plugins;
}

std::optional<PluginInfo> findPluginByPrefix(const std::string& prefix, const fs::path& project) {
  fs::path proj = project;
  if (proj.empty()) {
    if (auto cur = getCurrentProject()) {
      if (auto p = findProject(*cur)) proj = p->path;
    }
    if (proj.empty()) {
      if (auto d = detectProjectFromCwd()) proj = d->path;
    }
  }

  std::optional<PluginInfo> found;
  for (const auto& p : discoverPlugins(proj)) {
    if (p.prefix != prefix) continue;
    if (p.scope == PluginScope::Project) {
      if (proj.empty()) continue;
      const auto expected = projectPluginsDir(proj);
      if (p.root.parent_path() != expected && p.root.string().find(expected.string()) == std::string::npos)
        continue;
    }
    found = p;
    if (p.scope == PluginScope::Project) return found;
  }
  return found;
}

std::optional<PluginCommand> findPluginCommand(const PluginInfo& plugin, const std::string& name) {
  for (const auto& c : plugin.commands)
    if (c.name == name) return c;
  return std::nullopt;
}

void printPluginHelp(const PluginInfo& plugin) {
  out::info("plugin: " + plugin.name + " (pp " + plugin.prefix + ")");
  if (!plugin.description.empty()) out::dim(plugin.description);
  if (!plugin.platforms.empty()) {
    std::ostringstream oss;
    oss << "platforms: ";
    for (size_t i = 0; i < plugin.platforms.size(); ++i) {
      if (i) oss << ", ";
      oss << plugin.platforms[i];
    }
    out::dim(oss.str());
  }
  out::blank();
  for (const auto& c : plugin.commands) {
    std::cout << "  pp " << plugin.prefix << " " << c.name;
    if (!c.description.empty() && c.description != c.script) std::cout << " — " << c.description;
    std::cout << "\n";
  }
}

static fs::path resolveProjectForPlugin(const fs::path& project) {
  if (!project.empty()) return project;
  if (auto cur = getCurrentProject()) {
    if (auto p = findProject(*cur)) return p->path;
  }
  if (auto d = detectProjectFromCwd()) return d->path;
  return {};
}

bool runPluginCommand(const PluginInfo& plugin, const std::string& commandName,
                      const std::vector<std::string>& args, const fs::path& project) {
  const auto cmd = findPluginCommand(plugin, commandName);
  if (!cmd) {
    out::error("unknown command: pp " + plugin.prefix + " " + commandName);
    printPluginHelp(plugin);
    return false;
  }

  const auto script = resolvePluginScript(plugin, *cmd);
  if (!fs::exists(script)) {
    out::error("plugin script missing: " + script.string());
    return false;
  }

  const auto proj = resolveProjectForPlugin(project);
  if (plugin.scope == PluginScope::Project && proj.empty()) {
    out::error("plugin requires project context");
    return false;
  }

  for (const auto& [k, v] : plugin.vars) platform::setEnv(k.c_str(), v.c_str());
  platform::setEnv("PP_APP_DATA", appDataDir().string().c_str());
  platform::setEnv("PP_PLUGIN_ROOT", plugin.root.string().c_str());

  if (!proj.empty()) platform::setCwd(proj);

  out::dim("plugin " + plugin.name + ": " + commandName);

  const auto ext = toLower(script.extension().string());
  std::string full;
  if (ext == ".sh" || ext == ".bash") {
#if !defined(_WIN32)
    ::chmod(script.c_str(), S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
#endif
    full = "bash " + shellQuote(script.string()) + joinArgsQuoted(args);
  } else if (ext == ".ps1") {
#if defined(_WIN32)
    full = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File " + shellQuote(script.string()) +
           joinArgsQuoted(args);
#else
    if (!commandOnPath("pwsh")) {
      out::error("pwsh not found — required to run PowerShell plugins on macOS");
      out::dim("  brew install --cask powershell");
      out::dim("Or add a .sh sibling next to the plugin .ps1 script");
      return false;
    }
    full = "pwsh -NoProfile -ExecutionPolicy Bypass -File " + shellQuote(script.string()) +
           joinArgsQuoted(args);
#endif
  } else if (ext == ".bat" || ext == ".cmd") {
#if defined(_WIN32)
    full = "cmd /c " + shellQuote(script.string()) + joinArgsQuoted(args);
#else
    out::error("batch plugin scripts are Windows-only: " + script.filename().string());
    return false;
#endif
  } else {
    full = shellQuote(script.string()) + joinArgsQuoted(args);
  }

  const int code = std::system(full.c_str());
  if (code != 0) {
    out::error("plugin command failed (exit " + std::to_string(code) + ")");
    return false;
  }
  return true;
}

static fs::path sourceBundledPluginsDir() {
  const auto exe = getExePath();
  if (exe.empty()) return {};
  fs::path p = exe;
  p = p.parent_path();
  for (int i = 0; i < 8; ++i) {
    const auto cand = p / "assets" / "plugins";
    if (fs::exists(cand)) return cand;
    if (p == p.parent_path()) break;
    p = p.parent_path();
  }
  return {};
}

static void copyDirRecursive(const fs::path& from, const fs::path& to) {
  ensureDir(to);
  for (const auto& entry : fs::recursive_directory_iterator(from)) {
    const auto rel = fs::relative(entry.path(), from);
    const auto dest = to / rel;
    if (entry.is_directory()) {
      ensureDir(dest);
    } else {
      ensureDir(dest.parent_path());
      std::error_code ec;
      fs::copy_file(entry.path(), dest, fs::copy_options::overwrite_existing, ec);
    }
  }
}

bool installBundledPlugins() {
  const auto src = sourceBundledPluginsDir();
  if (src.empty() || !fs::exists(src)) return true;
  const auto dest = globalPluginsDir();
  ensureDir(dest);
  for (const auto& entry : fs::directory_iterator(src)) {
    if (!entry.is_directory()) continue;
    copyDirRecursive(entry.path(), dest / entry.path().filename());
  }
  return true;
}

}  // namespace pp
