#include "core/scripts.hpp"

#include "core/projects.hpp"
#include "util/editor.hpp"
#include "util/output.hpp"
#include "util/paths.hpp"

#include <windows.h>

#include <cstdlib>
#include <fstream>

namespace pp {
namespace fs = std::filesystem;

static fs::path scriptsRoot(ScriptScope scope, const fs::path& project) {
  if (scope == ScriptScope::Global) return globalScriptsDir();
  return projectScriptsDir(project);
}

static bool isScriptExt(const std::string& ext) { return ext == ".ps1" || ext == ".bat"; }

static std::string normalizeScriptName(std::string name) {
  const fs::path p(name);
  const auto ext = p.extension().string();
  if (isScriptExt(ext)) return p.stem().string();
  return name;
}

std::vector<ScriptInfo> listScripts(ScriptScope scope, const fs::path& project) {
  std::vector<ScriptInfo> scripts;
  const auto root = scriptsRoot(scope, project);
  if (!fs::exists(root)) return scripts;

  for (const auto& entry : fs::directory_iterator(root)) {
    if (!entry.is_regular_file()) continue;
    const auto ext = entry.path().extension().string();
    if (!isScriptExt(ext)) continue;
    ScriptInfo s;
    s.name = entry.path().stem().string();
    s.ext = ext;
    s.path = entry.path();
    s.scope = scope;
    scripts.push_back(std::move(s));
  }
  std::sort(scripts.begin(), scripts.end(),
            [](const ScriptInfo& a, const ScriptInfo& b) { return a.name < b.name; });
  return scripts;
}

static std::optional<ScriptInfo> findScript(const std::string& name, ScriptScope scope,
                                            const fs::path& project) {
  const auto normalized = normalizeScriptName(name);
  for (const auto& s : listScripts(scope, project))
    if (s.name == normalized) return s;
  return std::nullopt;
}

std::optional<ScriptInfo> resolveScript(const std::string& name, ScriptScope scope,
                                        const fs::path& project, bool explicitScope) {
  if (auto script = findScript(name, scope, project)) return script;
  if (explicitScope) return std::nullopt;
  if (scope == ScriptScope::Project)
    return findScript(name, ScriptScope::Global, {});
  return findScript(name, ScriptScope::Project, project);
}

bool runScript(const std::string& name, ScriptScope scope, const fs::path& project,
               const std::vector<std::string>& args, bool explicitScope) {
  auto script = resolveScript(name, scope, project, explicitScope);
  if (!script) {
    out::error("script not found: " + name);
    return false;
  }

  std::string cmd;
  if (script->ext == ".ps1") {
    cmd = "powershell -NoProfile -ExecutionPolicy Bypass -File \"" + script->path.string() + "\"";
  } else {
    cmd = "cmd /c \"" + script->path.string() + "\"";
  }
  for (const auto& a : args) cmd += " \"" + a + "\"";

  if (scope == ScriptScope::Project && !project.empty()) {
    out::info("running in " + project.filename().string());
    SetCurrentDirectoryA(project.string().c_str());
  } else if (script->scope == ScriptScope::Global) {
    out::dim("global script");
  }

  out::dim("exec: " + cmd);
  const int code = std::system(cmd.c_str());
  if (code != 0) {
    out::error("script exited with code " + std::to_string(code));
    return false;
  }
  out::success("script finished: " + name);
  return true;
}

bool createScript(const std::string& name, const std::string& ext, ScriptScope scope,
                  const fs::path& project) {
  if (ext != ".ps1" && ext != ".bat") {
    out::error("extension must be .ps1 or .bat");
    return false;
  }
  const auto root = scriptsRoot(scope, project);
  ensureDir(root);
  const auto path = root / (name + ext);
  if (fs::exists(path)) {
    out::error("script already exists: " + name);
    return false;
  }

  std::ofstream file(path);
  if (ext == ".ps1") {
    file << "# PP script: " << name << "\n";
    file << "Write-Host \"Running " << name << "...\"\n";
    file << "# Add commands below\n";
  } else {
    file << "@echo off\n";
    file << "REM PP script: " << name << "\n";
    file << "echo Running " << name << "...\n";
  }
  out::success("created script: " + path.string());
  return true;
}

bool deleteScript(const std::string& name, ScriptScope scope, const fs::path& project,
                  bool force, bool explicitScope) {
  auto script = resolveScript(name, scope, project, explicitScope);
  if (!script) {
    out::error("script not found: " + name);
    return false;
  }
  if (!force) {
    out::warn("this will delete " + script->path.string());
    out::info("re-run with --force to confirm");
    return false;
  }
  std::error_code ec;
  fs::remove(script->path, ec);
  if (ec) {
    out::error("failed to delete script");
    return false;
  }
  out::success("deleted script: " + name);
  return true;
}

bool editScript(const std::string& name, ScriptScope scope, const fs::path& project,
                bool explicitScope) {
  auto script = resolveScript(name, scope, project, explicitScope);
  if (!script) {
    out::error("script not found: " + name);
    return false;
  }
  return openInEditor(script->path);
}

}  // namespace pp
