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
  bool dry_run = false;
  bool auto_no_agent = false;
  bool auto_prompt_setup = false;
  int task_count = 0;
#if defined(_WIN32)
  std::string type = "ps1";
#else
  std::string type = "sh";
#endif
  std::string profile;
  std::string from_path;
  std::string as_name;
  std::string template_name;
};

Args parseArgs(int argc, char** argv);

}  // namespace pp
