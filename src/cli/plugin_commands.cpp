#include "cli/plugin_commands.hpp"

#include "cli/parser.hpp"
#include "core/plugins.hpp"
#include "core/projects.hpp"
#include "util/output.hpp"

#include <windows.h>

namespace pp {
namespace fs = std::filesystem;

static fs::path resolveProjectContext() {
  if (auto cur = getCurrentProject()) {
    if (auto p = findProject(*cur)) return p->path;
  }
  if (auto d = detectProjectFromCwd()) return d->path;
  return {};
}

static int cmdPluginList(const Args&) {
  const auto project = resolveProjectContext();
  const auto plugins = discoverPlugins(project);
  if (plugins.empty()) {
    out::dim("no plugins installed");
    out::dim("global: " + globalPluginsDir().string());
    return 0;
  }
  for (const auto& p : plugins) {
    const auto scopeLabel = p.scope == PluginScope::Global ? "global" : "project";
    std::cout << "  " << p.name << " (pp " << p.prefix << ") [" << scopeLabel << "]\n";
    if (!p.description.empty()) out::dim("    " + p.description);
  }
  return 0;
}

static int cmdPluginInfo(const Args& args) {
  if (args.positional.size() < 3) {
    out::error("usage: pp plugin info <name|prefix>");
    return 1;
  }
  const auto query = args.positional[2];
  const auto project = resolveProjectContext();
  for (const auto& p : discoverPlugins(project)) {
    if (p.name == query || p.prefix == query) {
      printPluginHelp(p);
      out::dim("path: " + p.root.string());
      return 0;
    }
  }
  out::error("plugin not found: " + query);
  return 1;
}

int runPluginCommandCli(const Args& args) {
  if (args.positional.size() < 2) {
    out::error("usage: pp plugin list|info|help");
    return 1;
  }
  const auto& sub = args.positional[1];
  if (sub == "list" || sub == "ls") return cmdPluginList(args);
  if (sub == "info") return cmdPluginInfo(args);
  if (sub == "help") {
    out::info("pp plugin list              List installed plugins");
    out::dim("pp plugin info <name>       Show plugin commands");
    return 0;
  }
  out::error("unknown plugin subcommand: " + sub);
  return 1;
}

std::optional<int> tryDispatchPlugin(const Args& args) {
  if (args.positional.empty()) return std::nullopt;
  const auto project = resolveProjectContext();
  const auto plugin = findPluginByPrefix(args.positional[0], project);
  if (!plugin) return std::nullopt;

  if (args.positional.size() < 2) {
    printPluginHelp(*plugin);
    return 0;
  }

  const auto& sub = args.positional[1];
  std::vector<std::string> rest(args.positional.begin() + 2, args.positional.end());

  if (args.force) SetEnvironmentVariableA("PP_FORCE", "1");
  else SetEnvironmentVariableA("PP_FORCE", nullptr);
  if (args.all) SetEnvironmentVariableA("PP_ALL", "1");
  else SetEnvironmentVariableA("PP_ALL", nullptr);
  if (args.dry_run) SetEnvironmentVariableA("PP_DRY_RUN", "1");
  else SetEnvironmentVariableA("PP_DRY_RUN", nullptr);

  return runPluginCommand(*plugin, sub, rest, project) ? 0 : 1;
}

}  // namespace pp
