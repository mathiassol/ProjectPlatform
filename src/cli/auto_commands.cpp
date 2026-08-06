#include "cli/auto_commands.hpp"

#include "core/automations.hpp"
#include "util/output.hpp"

namespace pp {

static void printAutoHelp() {
  out::info("AI automations (Cursor CLI + isolated workspaces)");
  out::dim("  pp auto list|ls                       List automations");
  out::dim("  pp auto info <id>                     Show automation help");
  out::dim("  pp auto doctor [id]                   Check prerequisites");
  out::dim("  pp auto init <id>                     Bootstrap workspace (no agent)");
  out::dim("  pp auto setup <id> [--no-agent]       Optional agent README pass");
  out::dim("  pp auto run <id> [--tasks N]          Run task via Cursor agent");
  out::dim("  pp auto prompt <id> [--setup]         Write prompt file (offline)");
  out::dim("  pp auto status [id]                   Workspace + task status");
  out::dim("  pp auto upload <id>                   Upload completed branch");
  out::dim("  pp auto goto <id>                     Print workspace path");
  out::dim("  pp auto explore <id>                  Open workspace in Explorer");
  out::dim("  pp auto open <id>                     Open workspace in Cursor");
  out::dim("  pp auto logs [id]                     Recent run logs");
  out::dim("  pp auto reset <id> [--force]          Clear state.json only");
  out::blank();
  out::dim("Workspaces: Documents/Automations/<id>/  (independent of pp ai plugin)");
  out::dim("Flags: --force/-y, --dry-run, --quiet/-q, --tasks N, --no-agent");
}

static int runEngine(const Args& args, const std::string& id, const std::string& mode,
                     const std::vector<std::string>& rest = {}) {
  return runAutomationEngine(id, mode, rest, args.force, args.dry_run, args.task_count, args.quiet,
                             args.auto_no_agent, args.auto_prompt_setup)
             ? 0
             : 1;
}

static int cmdAutoList(const Args&) {
  const auto list = discoverAutomations();
  if (list.empty()) {
    out::dim("no automations installed");
    out::dim("bundled: " + automationsBundleDir().string());
    out::dim("run: pp install");
    return 0;
  }
  out::info("automations:");
  for (const auto& a : list) {
    std::cout << "  " << a.id;
    if (!a.name.empty() && a.name != a.id) std::cout << " (" << a.name << ")";
    std::cout << " [" << (a.setup_complete ? "ready" : "needs init") << "]\n";
    if (!a.description.empty()) out::dim("    " + a.description);
  }
  return 0;
}

static int cmdAutoInfo(const Args& args) {
  if (args.positional.size() < 3) {
    out::error("usage: pp auto info <id>");
    return 1;
  }
  const auto a = findAutomation(args.positional[2]);
  if (!a) {
    out::error("automation not found: " + args.positional[2]);
    return 1;
  }
  printAutomationHelp(*a);
  return 0;
}

static int cmdAutoDispatch(const Args& args, const std::string& mode, size_t idIndex) {
  if (args.positional.size() <= idIndex) {
    out::error("usage: pp auto " + mode + " <id>");
    return 1;
  }
  const auto& id = args.positional[idIndex];
  std::vector<std::string> rest;
  for (size_t i = idIndex + 1; i < args.positional.size(); ++i) rest.push_back(args.positional[i]);
  return runEngine(args, id, mode, rest);
}

int runAutoCommand(const Args& args) {
  if (args.positional.size() < 2) {
    printAutoHelp();
    return 0;
  }

  const auto& sub = args.positional[1];
  if (sub == "help" || sub == "-h") {
    printAutoHelp();
    return 0;
  }
  if (sub == "list" || sub == "ls") return cmdAutoList(args);
  if (sub == "info") return cmdAutoInfo(args);
  if (sub == "doctor") {
    if (args.positional.size() >= 3) return cmdAutoDispatch(args, "doctor", 2);
    return runEngine(args, "", "doctor");
  }
  if (sub == "status") {
    if (args.positional.size() >= 3) return cmdAutoDispatch(args, "status", 2);
    return runEngine(args, "", "status-all");
  }
  if (sub == "init") return cmdAutoDispatch(args, "init", 2);
  if (sub == "setup") return cmdAutoDispatch(args, "setup", 2);
  if (sub == "run") return cmdAutoDispatch(args, "run", 2);
  if (sub == "prompt") return cmdAutoDispatch(args, "prompt", 2);
  if (sub == "upload") return cmdAutoDispatch(args, "upload", 2);
  if (sub == "goto" || sub == "cd") return cmdAutoDispatch(args, "goto", 2);
  if (sub == "explore" || sub == "open-folder") return cmdAutoDispatch(args, "explore", 2);
  if (sub == "reset") return cmdAutoDispatch(args, "reset", 2);
  if (sub == "logs") {
    if (args.positional.size() >= 3) return cmdAutoDispatch(args, "logs", 2);
    return runEngine(args, "", "logs-all");
  }
  if (sub == "open") return cmdAutoDispatch(args, "open", 2);

  out::error("unknown auto subcommand: " + sub);
  printAutoHelp();
  return 1;
}

}  // namespace pp
