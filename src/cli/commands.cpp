#include "cli/commands.hpp"
#include "cli/auto_commands.hpp"
#include "cli/env_commands.hpp"
#include "cli/plugin_commands.hpp"

#include "core/install.hpp"
#include "core/restart.hpp"
#include "core/update.hpp"
#include "util/version.hpp"
#include "core/projects.hpp"
#include "core/scripts.hpp"
#include "core/templates.hpp"
#include "platform/platform.hpp"
#include "util/output.hpp"
#include "util/paths.hpp"
#include "util/editor.hpp"

#include <fstream>

namespace pp {
namespace fs = std::filesystem;

static void printHelp() {
  out::title("ProjectPlatform (PP) - project manager");
  out::blank();
  out::info("Projects");
  out::dim("  pp list|ls                 List projects");
  out::dim("  pp new <name>              Create empty project");
  out::dim("  pp open <name>             Open project in Explorer");
  out::dim("  pp cd|goto|enter <name>    Jump to project (needs: pp hook install)");
  out::dim("  pp here                    Detect project from current directory");
  out::dim("  pp current                 Show active project");
  out::dim("  pp info [name]             Show project details");
  out::dim("  pp find <query>            Search projects by name");
  out::dim("  pp recent                  Recently used projects");
  out::dim("  pp rename <from> <to>      Rename project");
  out::dim("  pp remove <name> [--force] Remove a project folder (not pp itself)");
  out::blank();
  out::info("Templates  (stored in Documents/Templates)");
  out::dim("  pp template list");
  out::dim("  pp template add <project> [name]   Save project as template (.gitignore aware)");
  out::dim("  pp template create <tpl> <project> Create project from template");
  out::dim("  pp template remove <name> [--force]");
  out::blank();
  out::info("Scripts  (global: Projects/.scripts, project: <project>/.scripts)");
  out::dim("  pp script list [--global|-g|--project|-p]");
  out::dim("  pp script run <name> [--global|-g] [args...]");
  out::dim("  pp script edit <name> [--global|-g]     Open script in editor");
  out::dim("  pp script new <name> [--global|-g] [--type ps1|bat|sh]");
  out::dim("  pp script delete <name> [--global|-g] [--force]");
  out::blank();
  out::info("Environment & secrets");
  out::dim("  pp env set/get/list/load/apply/clear   Manage env vars & .env files");
  out::dim("  pp env help                            Full env command reference");
  out::blank();
  out::info("Plugins");
  out::dim("  pp plugin list                    List installed plugins");
  out::dim("  pp plugin info <name>             Show plugin commands");
  out::dim("  pp <prefix> <cmd>                 Run a plugin command (e.g. pp ai setup)");
  out::blank();
  out::info("Automations  (AI tasks via Cursor CLI — independent of pp ai)");
  out::dim("  pp auto list|init|run|upload        Manage AI automations");
  out::dim("  pp auto help                        Full automation reference");
  out::blank();
  out::info("Shell & setup");
  out::dim("  pp update [--check] [--force]   Check/install latest GitHub release");
  out::dim("  pp version                      Show version");
  out::dim("  pp install                      Copy pp to AppData and add to PATH");
  out::dim("  pp uninstall                    Remove pp from PATH and AppData");
  out::dim("  pp hook install            Enable pp cd/goto in terminal (+ project prompt)");
  out::dim("  pp hook uninstall");
  out::dim("  pp hook status             Show whether shell integration is active");
  out::dim("  pp restart                 Restart terminal and restore session");
  out::dim("  pp editor setup            Set Zed as default for scripts/env files");
  out::dim("  pp editor status           Show editor config and associations");
  out::dim("  pp config                  Show paths/config");
  out::dim("  pp code [name]             Open project in VS Code");
}

static bool openExplorer(const fs::path& p) { return platform::openPath(p); }

static bool openVsCode(const fs::path& p) {
  const std::string cmd = "code \"" + p.string() + "\"";
  return std::system(cmd.c_str()) == 0;
}

static fs::path resolveProjectPath(const Args& args, size_t index) {
  if (args.positional.size() <= index) return {};
  auto proj = findProject(args.positional[index]);
  return proj ? proj->path : fs::path{};
}

static int cmdList(const Args& args) {
  const auto projects = listProjects();
  if (projects.empty()) {
    if (!args.quiet) out::warn("no projects in " + loadConfig().projects_dir.string());
    return 0;
  }
  const auto current = getCurrentProject();
  for (const auto& p : projects) {
    const bool isCurrent = current && *current == p.name;
    out::stream(isCurrent ? out::Color::Green : out::Color::Reset);
    std::cout << (isCurrent ? "* " : "  ") << p.name;
    if (p.has_git) std::cout << " [git]";
    if (p.has_scripts) std::cout << " [scripts]";
    std::cout << "\n";
    if (!args.quiet) out::dim("    " + p.path.string());
  }
  return 0;
}

static int cmdCd(const Args& args) {
  if (args.positional.size() < 2) {
    out::error("usage: pp cd <name>");
    return 1;
  }
  auto proj = findProject(args.positional[1]);
  if (!proj) {
    out::error("project not found: " + args.positional[1]);
    return 1;
  }
  setCurrentProject(proj->name);
  std::error_code ec;
  const auto outPath = fs::weakly_canonical(proj->path, ec);
  std::cout << (ec ? proj->path.string() : outPath.string()) << "\n";
  if (!args.quiet) {
    out::blank();
    out::warn("pp.exe cannot change your shell directory by itself");
    out::dim("  PowerShell (one-off):  Set-Location (pp cd " + proj->name + " --quiet)");
    out::dim("  PowerShell (permanent):  pp hook install   then pp cd " + proj->name + " works");
  }
  return 0;
}

static int cmdGoto(const Args& args) { return cmdCd(args); }

static int cmdHere(const Args& args) {
  auto proj = detectProjectFromCwd();
  if (!proj) {
    if (!args.quiet) out::dim("not inside a managed project");
    return 1;
  }
  setCurrentProject(proj->name);
  if (args.json) {
    std::cout << proj->name << "\n";
    return 0;
  }
  out::info("project: " + proj->name);
  out::dim(proj->path.string());
  return 0;
}

static int cmdInfo(const Args& args) {
  std::optional<ProjectInfo> proj;
  if (args.positional.size() >= 2)
    proj = findProject(args.positional[1]);
  else if (auto cur = getCurrentProject())
    proj = findProject(*cur);
  else
    proj = detectProjectFromCwd();

  if (!proj) {
    out::error("no project specified or detected");
    return 1;
  }
  out::title(proj->name);
  out::dim("path:     " + proj->path.string());
  out::dim("git:      " + std::string(proj->has_git ? "yes" : "no"));
  out::dim("scripts:  " + std::string(proj->has_scripts ? "yes" : "no"));
  return 0;
}

static int cmdFind(const Args& args) {
  if (args.positional.size() < 2) {
    out::error("usage: pp find <query>");
    return 1;
  }
  const auto q = args.positional[1];
  for (const auto& p : listProjects()) {
    if (p.name.find(q) != std::string::npos) std::cout << p.name << "\n";
  }
  return 0;
}

static int cmdRecent(const Args&) {
  const auto state = loadState();
  if (state.recent.empty()) {
    out::dim("no recent projects");
    return 0;
  }
  for (const auto& name : state.recent) std::cout << name << "\n";
  return 0;
}

static int cmdTemplate(const Args& args) {
  if (args.positional.size() < 2) {
    out::error("usage: pp template <list|add|create|remove> ...");
    return 1;
  }
  const auto& sub = args.positional[1];
  if (sub == "list" || sub == "ls") {
    const auto all = listTemplates();
    if (all.empty()) {
      out::warn("no templates in " + loadConfig().templates_dir.string());
      return 0;
    }
    for (const auto& t : all) {
      std::cout << t.name;
      if (t.has_gitignore) std::cout << " [gitignore]";
      if (!t.description.empty()) std::cout << " - " << t.description;
      std::cout << "\n";
    }
    return 0;
  }
  if (sub == "add") {
    if (args.positional.size() < 3) {
      out::error("usage: pp template add <project> [template-name] [--yes]");
      return 1;
    }
    const auto& project = args.positional[2];
    const std::string tplName =
        args.positional.size() >= 4 ? args.positional[3] : project;
    return addTemplateFromProject(project, tplName, args.force) ? 0 : 1;
  }
  if (sub == "create" || sub == "new") {
    if (args.positional.size() < 4) {
      out::error("usage: pp template create <template> <project>");
      return 1;
    }
    return createProjectFromTemplate(args.positional[2], args.positional[3], args.force) ? 0 : 1;
  }
  if (sub == "remove" || sub == "rm") {
    if (args.positional.size() < 3) {
      out::error("usage: pp template remove <name> [--force]");
      return 1;
    }
    return removeTemplate(args.positional[2], args.force) ? 0 : 1;
  }
  out::error("unknown template subcommand: " + sub);
  return 1;
}

static int cmdScript(const Args& args) {
  if (args.positional.size() < 2) {
    out::error("usage: pp script <list|run|edit|new|delete> ...");
    return 1;
  }

  ScriptScope scope = ScriptScope::Project;
  if (args.global) scope = ScriptScope::Global;
  if (args.project_scope) scope = ScriptScope::Project;

  const bool explicitScope = args.global || args.project_scope;

  fs::path project;
  if (scope == ScriptScope::Project) {
    if (auto cur = getCurrentProject()) {
      if (auto p = findProject(*cur)) project = p->path;
    }
    if (project.empty()) {
      if (auto detected = detectProjectFromCwd()) project = detected->path;
    }
    if (project.empty() && args.positional[1] != "list") {
      out::error("no current project; use --global or cd into a project");
      return 1;
    }
  }

  const auto& sub = args.positional[1];
  if (sub == "list" || sub == "ls") {
    auto print = [&](ScriptScope sc, const fs::path& proj) {
      const auto label = sc == ScriptScope::Global ? "global" : "project";
      const auto scripts = listScripts(sc, proj);
      if (scripts.empty()) return;
      out::info(std::string(label) + " scripts:");
      for (const auto& s : scripts) std::cout << "  " << s.name << s.ext << "\n";
    };
    if (args.global || args.project_scope) {
      print(scope, project);
    } else {
      print(ScriptScope::Global, {});
      if (!project.empty()) print(ScriptScope::Project, project);
      else {
        auto detected = detectProjectFromCwd();
        if (detected) print(ScriptScope::Project, detected->path);
      }
    }
    return 0;
  }
  if (sub == "run") {
    if (args.positional.size() < 3) {
      out::error("usage: pp script run <name> [args...]");
      return 1;
    }
    std::vector<std::string> scriptArgs(args.positional.begin() + 3, args.positional.end());
    return runScript(args.positional[2], scope, project, scriptArgs, explicitScope) ? 0 : 1;
  }
  if (sub == "edit" || sub == "open") {
    if (args.positional.size() < 3) {
      out::error("usage: pp script edit <name> [--global|-g]");
      return 1;
    }
    return editScript(args.positional[2], scope, project, explicitScope) ? 0 : 1;
  }
  if (sub == "new" || sub == "create") {
    if (args.positional.size() < 3) {
      out::error("usage: pp script new <name> [--type ps1|bat|sh]");
      return 1;
    }
    std::string ext = args.type;
    if (ext[0] != '.') ext = "." + ext;
    return createScript(args.positional[2], ext, scope, project) ? 0 : 1;
  }
  if (sub == "delete" || sub == "rm") {
    if (args.positional.size() < 3) {
      out::error("usage: pp script delete <name> [--force]");
      return 1;
    }
    return deleteScript(args.positional[2], scope, project, args.force, explicitScope) ? 0 : 1;
  }
  out::error("unknown script subcommand: " + sub);
  return 1;
}

static int cmdHook(const Args& args) {
  if (args.positional.size() < 2) {
    out::error("usage: pp hook install|reload|uninstall|status");
    return 1;
  }
  const auto& sub = args.positional[1];
  if (sub == "install") return installHook() ? 0 : 1;
  if (sub == "reload" || sub == "refresh") {
    refreshHookScript();
    out::success("hook script updated");
    out::dim("Reload this session:");
#if defined(_WIN32)
    out::dim("  . $PROFILE");
#else
    out::dim("  source ~/.zshrc");
#endif
    return 0;
  }
  if (sub == "uninstall" || sub == "remove") return uninstallHook() ? 0 : 1;
  if (sub == "status") {
    const auto marker = "# ProjectPlatform hook";
#if defined(_WIN32)
    bool pwsh7 = false, winps = false;
    for (const bool pwsh : {true, false}) {
      const char* home = std::getenv("USERPROFILE");
      if (!home) continue;
      const fs::path profile = pwsh
          ? fs::path(home) / "Documents" / "PowerShell" / "Microsoft.PowerShell_profile.ps1"
          : fs::path(home) / "Documents" / "WindowsPowerShell" /
                "Microsoft.PowerShell_profile.ps1";
      if (!fs::exists(profile)) continue;
      std::ifstream in(profile);
      const std::string content((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
      if (content.find(marker) != std::string::npos) {
        if (pwsh) pwsh7 = true;
        else winps = true;
      }
    }
    if (pwsh7 || winps) {
      out::success("hook active");
      if (pwsh7) out::dim("  PowerShell 7+ profile");
      if (winps) out::dim("  Windows PowerShell profile");
      out::blank();
      out::info("With hook on:");
      out::dim("  pp cd/goto/enter <name>  actually changes directory");
      out::dim("  $env:PP_PROJECT          set for scripts and pp script");
      out::dim("  prompt                   PP:project> or PP:project/subfolder> when nested");
    } else {
      out::warn("hook not installed");
      out::dim("  pp cd only prints a path — run 'pp hook install' to jump in-terminal");
    }
#else
    bool zsh = false, fish = false;
    const auto home = platform::userHomeDir();
    if (!home.empty()) {
      const fs::path profile = home / ".zshrc";
      if (fs::exists(profile)) {
        std::ifstream in(profile);
        const std::string content((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());
        zsh = content.find(marker) != std::string::npos;
      }
      const fs::path fishProfile = home / ".config" / "fish" / "config.fish";
      if (fs::exists(fishProfile)) {
        std::ifstream in(fishProfile);
        const std::string content((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());
        fish = content.find(marker) != std::string::npos;
      }
    }
    if (zsh || fish) {
      out::success("hook active");
      if (zsh) out::dim("  ~/.zshrc");
      if (fish) out::dim("  ~/.config/fish/config.fish");
      out::blank();
      out::info("With hook on:");
      out::dim("  pp cd/goto/enter <name>  actually changes directory");
      out::dim("  $PP_PROJECT              set for scripts and pp script");
      out::dim("  prompt                   PP:project> or PP:project/subfolder> when nested");
      out::dim("  pp restart               new Terminal/iTerm + session restore (zsh)");
    } else {
      out::warn("hook not installed");
      out::dim("  pp cd only prints a path — run 'pp hook install' to jump in-terminal");
    }
#endif
    return 0;
  }
  out::error("unknown hook subcommand");
  return 1;
}

static int cmdEditor(const Args& args) {
  if (args.positional.size() < 2) {
    out::error("usage: pp editor setup|status");
    return 1;
  }
  const auto& sub = args.positional[1];
  if (sub == "setup" || sub == "install" || sub == "zed") return configureZedAsDefaultEditor() ? 0 : 1;
  if (sub == "status") {
    showEditorStatus();
    return 0;
  }
  out::error("unknown editor subcommand: " + sub);
  out::dim("usage: pp editor setup|status");
  return 1;
}

static int cmdRestart(const Args&) {
  RestartSession session;
  captureRestartSession(session);
  if (!saveRestartSession(session)) {
    out::error("could not save restart session");
    return 1;
  }
  if (!spawnRestartTerminal(restartSessionPath())) {
    return 1;
  }
  out::success("new terminal started — session saved to " + restartSessionPath().string());
  out::dim("With hook: this window closes automatically");
  out::dim("Without hook: close this window manually");
  return 0;
}

static int cmdConfig(const Args&) {
  const auto cfg = loadConfig();
  out::title("ProjectPlatform config");
  out::dim("projects:  " + cfg.projects_dir.string());
  out::dim("templates: " + cfg.templates_dir.string());
  out::dim("editor:    " + (cfg.editor.empty() ? std::string("(system default)") : cfg.editor));
  out::dim("install:   " + installDir().string());
  out::dim("config:    " + configPath().string());
  if (auto cur = getCurrentProject()) out::dim("current:   " + *cur);
  return 0;
}

int runCommand(const Args& args) {
  if (args.positional.empty()) {
    printHelp();
    return 0;
  }

  const auto& cmd = args.positional[0];

  if (cmd == "help" || cmd == "-h" || cmd == "--help") {
    printHelp();
    return 0;
  }
  if (cmd == "version" || cmd == "-v") {
    std::cout << "ProjectPlatform " << PP_APP_VERSION << "\n";
    return 0;
  }
  if (cmd == "update" || cmd == "upgrade") {
    if (args.check_only) return checkUpdate(false) ? 0 : 1;
    return performUpdate(args.force) ? 0 : 1;
  }
  if (cmd == "install") return installSelf() ? 0 : 1;
  if (cmd == "uninstall") return uninstallSelf() ? 0 : 1;
  if (cmd == "config") return cmdConfig(args);
  if (cmd == "hook") return cmdHook(args);
  if (cmd == "editor") return cmdEditor(args);
  if (cmd == "restart") return cmdRestart(args);

  if (cmd == "list" || cmd == "ls") return cmdList(args);
  if (cmd == "new" || cmd == "init") {
    if (args.positional.size() < 2) {
      out::error("usage: pp new <name>");
      return 1;
    }
    return createProject(args.positional[1]) ? 0 : 1;
  }
  if (cmd == "open" || cmd == "explorer") {
    if (args.positional.size() < 2) {
      out::error("usage: pp open <name>");
      return 1;
    }
    auto proj = findProject(args.positional[1]);
    if (!proj) return 1;
    setCurrentProject(proj->name);
    return openExplorer(proj->path) ? 0 : 1;
  }
  if (cmd == "cd") return cmdCd(args);
  if (cmd == "goto" || cmd == "go" || cmd == "enter") return cmdGoto(args);
  if (cmd == "here") return cmdHere(args);
  if (cmd == "current" || cmd == "pwd") {
    if (auto cur = getCurrentProject()) {
      if (auto p = findProject(*cur)) {
        std::cout << p->path.string() << "\n";
        return 0;
      }
    }
    out::error("no current project");
    return 1;
  }
  if (cmd == "info") return cmdInfo(args);
  if (cmd == "find") return cmdFind(args);
  if (cmd == "recent") return cmdRecent(args);
  if (cmd == "rename") {
    if (args.positional.size() < 3) {
      out::error("usage: pp rename <from> <to>");
      return 1;
    }
    return renameProject(args.positional[1], args.positional[2]) ? 0 : 1;
  }
  if (cmd == "remove" || cmd == "rm") {
    if (args.positional.size() < 2) {
      out::error("usage: pp remove <name> [--force]");
      return 1;
    }
    return removeProject(args.positional[1], args.force) ? 0 : 1;
  }
  if (cmd == "code") {
    std::optional<ProjectInfo> proj;
    if (args.positional.size() >= 2) proj = findProject(args.positional[1]);
    else if (auto cur = getCurrentProject()) proj = findProject(*cur);
    else proj = detectProjectFromCwd();
    if (!proj) {
      out::error("no project found");
      return 1;
    }
    setCurrentProject(proj->name);
    return openVsCode(proj->path) ? 0 : 1;
  }
  if (cmd == "template" || cmd == "tpl") return cmdTemplate(args);
  if (cmd == "script" || cmd == "scripts") return cmdScript(args);
  if (cmd == "env" || cmd == "secrets") return runEnvCommand(args);
  if (cmd == "plugin" || cmd == "plugins") return runPluginCommandCli(args);
  if (cmd == "auto" || cmd == "automation" || cmd == "automations") return runAutoCommand(args);

  if (auto pluginResult = tryDispatchPlugin(args)) return *pluginResult;

  out::error("unknown command: " + cmd);
  out::dim("run 'pp help' for usage");
  return 1;
}

}  // namespace pp
