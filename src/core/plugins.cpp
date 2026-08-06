#include "core/plugins.hpp"

#include "core/install.hpp"
#include "core/projects.hpp"
#include "util/output.hpp"
#include "util/paths.hpp"

#include <cstdlib>
#include <fstream>
#include <regex>
#include <windows.h>

namespace pp {
namespace fs = std::filesystem;

fs::path globalPluginsDir() { return appDataDir() / "plugins"; }

fs::path projectsGlobalPluginsDir() {
  return loadConfig().projects_dir / ".plugins";
}

fs::path projectPluginsDir(const fs::path& project) { return project / ".pp" / "plugins"; }

static std::optional<std::string> jsonGetString(const std::string& json, const std::string& key) {
  const std::regex re("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
  std::smatch m;
  if (std::regex_search(json, m, re) && m.size() > 1) return m[1].str();
  return std::nullopt;
}

static std::map<std::string, std::string> jsonGetStringMap(const std::string& json,
                                                           const std::string& key) {
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

static std::map<std::string, std::string> jsonGetCommandsMap(const std::string& json) {
  return jsonGetStringMap(json, "commands");
}

static void scanPluginDir(const fs::path& dir, PluginScope scope, std::vector<PluginInfo>& out) {
  if (!fs::exists(dir)) return;
  for (const auto& entry : fs::directory_iterator(dir)) {
    if (!entry.is_directory()) continue;
    const auto manifest = entry.path() / "plugin.json";
    if (!fs::exists(manifest)) continue;
    PluginInfo info;
    if (!loadPluginManifest(entry.path(), info)) continue;
    info.scope = scope;
    info.root = entry.path();
    out.push_back(std::move(info));
  }
}

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

  const auto cmds = jsonGetCommandsMap(json);
  for (const auto& [name, script] : cmds) {
    PluginCommand c;
    c.name = name;
    c.script = script;
    c.description = script;
    out.commands.push_back(std::move(c));
  }

  return !out.name.empty() && !out.prefix.empty() && !out.commands.empty();
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

  const auto script = plugin.root / cmd->script;
  if (!fs::exists(script)) {
    out::error("plugin script missing: " + script.string());
    return false;
  }

  const auto proj = resolveProjectForPlugin(project);
  if (plugin.scope == PluginScope::Project && proj.empty()) {
    out::error("plugin requires project context");
    return false;
  }

  for (const auto& [k, v] : plugin.vars) SetEnvironmentVariableA(k.c_str(), v.c_str());

  std::string psArgs = "-NoProfile -ExecutionPolicy Bypass -File \"" + script.string() + "\"";
  for (const auto& a : args) psArgs += " \"" + a + "\"";

  if (!proj.empty()) SetCurrentDirectoryA(proj.string().c_str());

  out::dim("plugin " + plugin.name + ": " + commandName);
  const std::string full = "powershell.exe " + psArgs;
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
