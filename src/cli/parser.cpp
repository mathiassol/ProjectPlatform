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
