#include "cli/env_commands.hpp"

#include "core/envstore.hpp"
#include "core/projects.hpp"
#include "util/output.hpp"
#include "util/paths.hpp"

#include <iostream>

namespace pp {
namespace fs = std::filesystem;

static EnvScope resolveScope(const Args& args, fs::path& project) {
  if (args.global) return EnvScope::Global;
  if (args.project_scope) {
    if (auto cur = getCurrentProject()) {
      if (auto p = findProject(*cur)) project = p->path;
    }
    if (project.empty()) {
      if (auto d = detectProjectFromCwd()) project = d->path;
    }
    return EnvScope::Project;
  }
  if (auto d = detectProjectFromCwd()) {
    project = d->path;
    return EnvScope::Project;
  }
  if (auto cur = getCurrentProject()) {
    if (auto p = findProject(*cur)) {
      project = p->path;
      return EnvScope::Project;
    }
  }
  return EnvScope::Global;
}

static void printEnvHelp() {
  out::info("Environment & secrets");
  out::dim("  pp env list [--global|-g|--project|-p] [--show-secrets]");
  out::dim("  pp env set KEY VALUE [--global|-g|--secret]");
  out::dim("  pp env get KEY [--global|-g]");
  out::dim("  pp env unset KEY [--global|-g]");
  out::dim("  pp env load [file] [--remember] [--global|-g] [--import]");
  out::dim("  pp env files                     Find .env / .envrc in project");
  out::dim("  pp env remembered                List auto-loaded files");
  out::dim("  pp env forget <file>             Stop auto-loading a file");
  out::dim("  pp env apply [--global|-g]       Load vars into terminal");
  out::dim("  pp env clear [--all]             Remove PP-loaded vars from terminal");
  out::dim("  pp env reset [--global|-g]       Delete stored vars/secrets");
  out::blank();
  out::dim("Secrets use Windows DPAPI (user-local). Hook required for apply/clear in shell.");
  out::dim("Enable hook: pp hook install");
}

static int cmdEnvList(const Args& args) {
  fs::path project;
  const auto scope = resolveScope(args, project);
  const auto vars = listVars(scope, project, args.show_secrets);
  if (vars.empty()) {
    out::dim("no stored vars");
    return 0;
  }
  const auto label = scope == EnvScope::Global ? "global" : "project";
  out::info(std::string(label) + " environment:");
  for (const auto& v : vars) {
    std::cout << "  " << v.key << "=" << v.value;
    if (v.secret) std::cout << " [secret]";
    std::cout << "\n";
  }
  return 0;
}

static int cmdEnvSet(const Args& args) {
  if (args.positional.size() < 4) {
    out::error("usage: pp env set KEY VALUE [--global|-g] [--secret]");
    return 1;
  }
  fs::path project;
  const auto scope = resolveScope(args, project);
  if (scope == EnvScope::Project && project.empty()) {
    out::error("no project context; use --global or cd into a project");
    return 1;
  }
  setVar(scope, project, args.positional[2], args.positional[3], args.secret);
  out::success("set " + args.positional[2] + (args.secret ? " (secret)" : ""));
  return 0;
}

static int cmdEnvGet(const Args& args) {
  if (args.positional.size() < 3) {
    out::error("usage: pp env get KEY");
    return 1;
  }
  fs::path project;
  const auto scope = resolveScope(args, project);
  auto v = getVar(scope, project, args.positional[2]);
  if (!v) {
    out::error("not found");
    return 1;
  }
  if (v->secret && !args.show_secrets) {
    out::info(args.positional[2] + "=******** [secret]");
  } else {
    std::cout << v->key << "=" << v->value << "\n";
  }
  return 0;
}

static int cmdEnvUnset(const Args& args) {
  if (args.positional.size() < 3) {
    out::error("usage: pp env unset KEY");
    return 1;
  }
  fs::path project;
  const auto scope = resolveScope(args, project);
  unsetVar(scope, project, args.positional[2]);
  out::success("removed " + args.positional[2]);
  return 0;
}

static fs::path resolveEnvFile(const Args& args, const fs::path& project) {
  if (args.positional.size() >= 3 && args.positional[2] != "--remember" && args.positional[2][0] != '-')
    return fs::path(args.positional[2]);
  if (!project.empty()) {
    for (const auto& f : discoverEnvFiles(project))
      return f;
  }
  return project / ".env";
}

static int cmdEnvLoad(const Args& args) {
  fs::path project;
  const auto scope = resolveScope(args, project);
  const auto file = resolveEnvFile(args, project);
  if (!fs::exists(file)) {
    out::error("file not found: " + file.string());
    return 1;
  }

  std::vector<EnvVar> vars;
  if (!parseDotEnvFile(file, vars, false)) {
    out::error("could not parse " + file.string());
    return 1;
  }

  if (args.import_vars) {
    for (const auto& v : vars) setVar(scope, project, v.key, v.value, false);
    out::info("imported " + std::to_string(vars.size()) + " vars into store");
  }

  if (args.remember && scope == EnvScope::Project && !project.empty()) {
    const auto rel = fs::relative(file, project).generic_string();
    rememberFile(scope, project, rel);
    out::info("remembered " + rel + " for this project");
  } else if (args.remember && scope == EnvScope::Global) {
    rememberFile(scope, {}, file.generic_string());
    out::info("remembered " + file.filename().string());
  }

  if (args.shell_output) {
    EnvBundle bundle;
    for (const auto& v : vars) {
      bundle.vars.push_back(v);
      bundle.keys.push_back(v.key);
    }
    std::cout << formatPowerShellApply(bundle);
    return 0;
  }

  out::success("loaded " + std::to_string(vars.size()) + " vars from " + file.filename().string());
  if (!args.quiet) {
    out::dim("Apply to terminal: pp env apply");
    out::dim("Or with hook: pp env load " + file.filename().string() + " --remember");
  }
  return 0;
}

static int cmdEnvFiles(const Args&) {
  auto proj = detectProjectFromCwd();
  if (!proj) {
    if (auto cur = getCurrentProject()) proj = findProject(*cur);
  }
  if (!proj) {
    out::error("not in a project");
    return 1;
  }
  const auto files = discoverEnvFiles(proj->path);
  if (files.empty()) {
    out::dim("no .env / .envrc files found");
    return 0;
  }
  for (const auto& f : files) std::cout << fs::relative(f, proj->path).generic_string() << "\n";
  return 0;
}

static int cmdEnvRemembered(const Args& args) {
  fs::path project;
  const auto scope = resolveScope(args, project);
  const auto files = listRememberedFiles(scope, project);
  if (files.empty()) {
    out::dim("no remembered env files");
    return 0;
  }
  for (const auto& f : files) std::cout << f.generic_string() << "\n";
  return 0;
}

static int cmdEnvForget(const Args& args) {
  if (args.positional.size() < 3) {
    out::error("usage: pp env forget <file>");
    return 1;
  }
  fs::path project;
  const auto scope = resolveScope(args, project);
  forgetFile(scope, project, args.positional[2]);
  out::success("forgot " + args.positional[2]);
  return 0;
}

static int cmdEnvApply(const Args& args) {
  fs::path project;
  auto scope = resolveScope(args, project);
  if (args.global) scope = EnvScope::Global;

  EnvBundle bundle;
  if (scope == EnvScope::Global) {
    bundle = collectEnv(EnvScope::Global, {}, false);
  } else {
    if (project.empty()) {
      out::error("no project context");
      return 1;
    }
    bundle = collectEnv(EnvScope::Project, project, true);
  }

  if (bundle.vars.empty()) {
    out::warn("nothing to apply");
    return 0;
  }

  const auto paths = scope == EnvScope::Global ? envPaths(EnvScope::Global, {}) : envPaths(EnvScope::Project, project);
  saveSessionKeys(paths.session_file, bundle.keys);

  if (args.shell_output) {
    std::cout << formatPowerShellApply(bundle);
    return 0;
  }

  out::success("ready to apply " + std::to_string(bundle.vars.size()) + " vars");
  out::dim("Run in PowerShell: pp env apply --shell ps | Invoke-Expression");
  out::dim("Or enable hook: pp hook install  (then pp env apply works directly)");
  return 0;
}

static int cmdEnvClear(const Args& args) {
  fs::path project;
  const auto scope = args.all ? EnvScope::Project : resolveScope(args, project);

  std::vector<std::string> keys;
  if (args.all) {
    keys = loadSessionKeys(envPaths(EnvScope::Global, {}).session_file);
    const auto pk = loadSessionKeys(envPaths(EnvScope::Project, project).session_file);
    keys.insert(keys.end(), pk.begin(), pk.end());
  } else {
    const auto paths = scope == EnvScope::Global ? envPaths(EnvScope::Global, {}) : envPaths(EnvScope::Project, project);
    keys = loadSessionKeys(paths.session_file);
  }

  if (args.shell_output) {
    std::cout << formatPowerShellClear(keys);
    return 0;
  }

  out::success("clear script ready for " + std::to_string(keys.size()) + " vars");
  out::dim("Run: pp env clear --shell ps | Invoke-Expression");
  return 0;
}

static int cmdEnvReset(const Args& args) {
  if (!args.force) {
    out::warn("this deletes stored vars and secrets");
    out::info("re-run with --force");
    return 1;
  }
  fs::path project;
  const auto scope = resolveScope(args, project);
  const auto paths = envPaths(scope, project);
  std::error_code ec;
  fs::remove(paths.vars_file, ec);
  fs::remove(paths.secrets_file, ec);
  fs::remove(paths.remembered_file, ec);
  fs::remove(paths.session_file, ec);
  out::success("store reset");
  return 0;
}

int runEnvCommand(const Args& args) {
  if (args.positional.size() < 2) {
    printEnvHelp();
    return 0;
  }

  const auto& sub = args.positional[1];
  if (sub == "help" || sub == "-h") {
    printEnvHelp();
    return 0;
  }
  if (sub == "list" || sub == "ls") return cmdEnvList(args);
  if (sub == "set") return cmdEnvSet(args);
  if (sub == "get") return cmdEnvGet(args);
  if (sub == "unset" || sub == "rm") return cmdEnvUnset(args);
  if (sub == "load") return cmdEnvLoad(args);
  if (sub == "files") return cmdEnvFiles(args);
  if (sub == "remembered") return cmdEnvRemembered(args);
  if (sub == "forget") return cmdEnvForget(args);
  if (sub == "apply") return cmdEnvApply(args);
  if (sub == "clear") return cmdEnvClear(args);
  if (sub == "reset") return cmdEnvReset(args);

  out::error("unknown env subcommand: " + sub);
  printEnvHelp();
  return 1;
}

}  // namespace pp
