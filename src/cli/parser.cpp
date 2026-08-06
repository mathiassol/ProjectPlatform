#include "cli/parser.hpp"

namespace pp {

Args parseArgs(int argc, char** argv) {
  Args a;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--force" || arg == "-f") a.force = true;
    else if (arg == "--yes" || arg == "-y") a.yes = true;
    else if (arg == "--quiet" || arg == "-q") a.quiet = true;
    else if (arg == "--json") a.json = true;
    else if (arg == "--global" || arg == "-g") a.global = true;
    else if (arg == "--project" || arg == "-p") a.project_scope = true;
    else if (arg == "--secret") a.secret = true;
    else if (arg == "--show-secrets") a.show_secrets = true;
    else if (arg == "--remember") a.remember = true;
    else if (arg == "--import") a.import_vars = true;
    else if (arg == "--shell" || arg == "ps") a.shell_output = true;
    else if (arg == "--all") a.all = true;
    else if (arg == "--check") a.check_only = true;
    else if (arg == "--dry-run") a.dry_run = true;
    else if (arg == "--no-agent") a.auto_no_agent = true;
    else if (arg == "--setup") a.auto_prompt_setup = true;
    else if (arg == "--tasks" && i + 1 < argc) a.task_count = std::atoi(argv[++i]);
    else if (arg == "--profile" && i + 1 < argc) a.profile = argv[++i];
    else if (arg == "--from" && i + 1 < argc) a.from_path = argv[++i];
    else if (arg == "--as" && i + 1 < argc) a.as_name = argv[++i];
    else if (arg == "--template" && i + 1 < argc) a.template_name = argv[++i];
    else if (arg == "--type" && i + 1 < argc) a.type = argv[++i];
    else if (arg.rfind("--", 0) == 0) {
      /* ignore unknown flags for forward compat */
    } else
      a.positional.push_back(arg);
  }
  if (a.yes) a.force = true;
  return a;
}

}  // namespace pp
