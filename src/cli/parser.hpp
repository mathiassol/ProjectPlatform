#pragma once

#include <string>
#include <vector>

namespace pp {

struct Args {
  std::vector<std::string> positional;
  bool force = false;
  bool yes = false;
  bool quiet = false;
  bool json = false;
  bool global = false;
  bool project_scope = false;
  bool secret = false;
  bool show_secrets = false;
  bool remember = false;
  bool import_vars = false;
  bool shell_output = false;
  bool all = false;
  bool check_only = false;
  std::string type = "ps1";
};

Args parseArgs(int argc, char** argv);

}  // namespace pp
