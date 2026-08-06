#include "core/automations.hpp"

#include "core/install.hpp"
#include "util/output.hpp"
#include "util/paths.hpp"

#include <cstdlib>
#include <fstream>
#include <regex>
#include <windows.h>

namespace pp {
namespace fs = std::filesystem;

fs::path automationsEngineDir() { return appDataDir() / "automations" / "engine"; }

fs::path automationsBundleDir() { return appDataDir() / "automations" / "bundled"; }

fs::path automationsWorkspaceRoot() {
  const auto docs = knownFolderDocuments();
  if (!docs.empty()) return docs / "Automations";
  return defaultProjectsDir().parent_path() / "Automations";
}

fs::path automationWorkspace(const std::string& id) {
  return automationsWorkspaceRoot() / id;
}

static std::optional<std::string> jsonGetString(const std::string& json, const std::string& key) {
  const std::regex re("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
  std::smatch m;
  if (std::regex_search(json, m, re) && m.size() > 1) return m[1].str();
  return std::nullopt;
}

static bool jsonGetBool(const std::string& json, const std::string& key, bool defaultVal) {
  const std::regex re("\"" + key + "\"\\s*:\\s*(true|false)");
  std::smatch m;
  if (std::regex_search(json, m, re) && m.size() > 1) return m[1].str() == "true";
  return defaultVal;
}

static fs::path sourceBundledAutomationsDir() {
  const auto exe = getExePath();
  if (exe.empty()) return {};
  fs::path p = exe;
  p = p.parent_path();
  for (int i = 0; i < 8; ++i) {
    const auto cand = p / "assets" / "automations";
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

bool installBundledAutomations() {
  const auto src = sourceBundledAutomationsDir();
  if (src.empty() || !fs::exists(src)) {
    out::dim("automations: bundled source not found (dev build? run from repo or pp install)");
    return true;
  }

  const auto engineSrc = src / "engine";
  if (!fs::exists(engineSrc / "engine.ps1")) {
    out::error("automations engine.ps1 missing in bundle - reinstall from source");
    return false;
  }
  copyDirRecursive(engineSrc, automationsEngineDir());

  const auto bundledDest = automationsBundleDir();
  ensureDir(bundledDest);
  for (const auto& entry : fs::directory_iterator(src)) {
    if (!entry.is_directory()) continue;
    if (entry.path().filename() == "engine") continue;
    copyDirRecursive(entry.path(), bundledDest / entry.path().filename());
  }
  return true;
}

bool loadAutomationManifest(const fs::path& bundleRoot, AutomationInfo& out) {
  const auto manifest = bundleRoot / "automation.json";
  std::ifstream in(manifest);
  if (!in) return false;
  const std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

  out = {};
  out.bundle_root = bundleRoot;
  if (auto v = jsonGetString(json, "id")) out.id = *v;
  if (auto v = jsonGetString(json, "name")) out.name = *v;
  if (auto v = jsonGetString(json, "version")) out.version = *v;
  if (auto v = jsonGetString(json, "description")) out.description = *v;

  if (out.id.empty()) return false;

  out.workspace_root = automationWorkspace(out.id);
  const auto stateFile = out.workspace_root / "state.json";
  if (fs::exists(stateFile)) {
    std::ifstream st(stateFile);
    const std::string stJson((std::istreambuf_iterator<char>(st)), std::istreambuf_iterator<char>());
    out.setup_complete = jsonGetBool(stJson, "setup_complete", false);
  }
  if (!out.setup_complete) {
    const auto upload = out.workspace_root / "upload-task.ps1";
    const auto repos = out.workspace_root / "repos";
    if (fs::exists(upload) && fs::exists(repos)) out.setup_complete = true;
  }

  return true;
}

std::vector<AutomationInfo> discoverAutomations() {
  std::vector<AutomationInfo> out;
  const auto dir = automationsBundleDir();
  if (!fs::exists(dir)) return out;
  for (const auto& entry : fs::directory_iterator(dir)) {
    if (!entry.is_directory()) continue;
    AutomationInfo info;
    if (loadAutomationManifest(entry.path(), info)) out.push_back(std::move(info));
  }
  return out;
}

std::optional<AutomationInfo> findAutomation(const std::string& id) {
  for (const auto& a : discoverAutomations()) {
    if (a.id == id || a.name == id) return a;
  }
  return std::nullopt;
}

void printAutomationHelp(const AutomationInfo& info) {
  out::info("automation: " + info.name + " (" + info.id + ")");
  if (!info.description.empty()) out::dim(info.description);
  out::blank();
  out::dim("  pp auto init " + info.id + "       Bootstrap workspace (no agent)");
  out::dim("  pp auto setup " + info.id + "     Optional agent README enrichment");
  out::dim("  pp auto run " + info.id + "       Run task via Cursor CLI");
  out::dim("  pp auto prompt " + info.id + "    Write prompt file (--setup for setup prompt)");
  out::dim("  pp auto status " + info.id + "    Workspace + task status");
  out::dim("  pp auto upload " + info.id + "     Upload completed task branch");
  out::dim("  pp auto goto " + info.id + "       Print workspace path (for cd)");
  out::dim("  pp auto explore " + info.id + "    Open workspace in Explorer");
  out::dim("  pp auto logs " + info.id + "       Recent run logs");
  out::dim("  pp auto open " + info.id + "       Open workspace in Cursor");
  out::dim("  pp auto doctor " + info.id + "     Check prerequisites");
  out::blank();
  out::dim("workspace: " + info.workspace_root.string());
  out::dim("setup: " + std::string(info.setup_complete ? "complete" : "not run"));
}

bool runAutomationEngine(const std::string& id, const std::string& mode,
                         const std::vector<std::string>& args, bool force, bool dry_run,
                         int task_count, bool quiet, bool auto_no_agent, bool auto_prompt_setup) {
  const bool globalMode = (mode == "doctor" || mode == "status-all" || mode == "logs-all");
  std::optional<AutomationInfo> autoInfo;
  if (!id.empty()) {
    autoInfo = findAutomation(id);
    if (!autoInfo) {
      out::error("automation not found: " + id);
      out::dim("run: pp auto list");
      return false;
    }
  } else if (!globalMode) {
    out::error("automation id required");
    return false;
  }

  const auto engine = automationsEngineDir() / "engine.ps1";
  if (!fs::exists(engine)) {
    out::error("automation engine missing - run: pp install");
    return false;
  }

  if (autoInfo) ensureDir(autoInfo->workspace_root);

  if (force) SetEnvironmentVariableA("PP_FORCE", "1");
  else SetEnvironmentVariableA("PP_FORCE", nullptr);
  if (dry_run) SetEnvironmentVariableA("PP_DRY_RUN", "1");
  else SetEnvironmentVariableA("PP_DRY_RUN", nullptr);
  if (quiet) SetEnvironmentVariableA("PP_QUIET", "1");
  else SetEnvironmentVariableA("PP_QUIET", nullptr);
  if (auto_no_agent) SetEnvironmentVariableA("PP_AUTO_AGENT", "0");
  else SetEnvironmentVariableA("PP_AUTO_AGENT", nullptr);
  if (auto_prompt_setup) SetEnvironmentVariableA("PP_AUTO_PROMPT_KIND", "setup");
  else SetEnvironmentVariableA("PP_AUTO_PROMPT_KIND", nullptr);
  if (task_count > 0) {
    SetEnvironmentVariableA("PP_AUTO_TASK_COUNT", std::to_string(task_count).c_str());
  } else {
    SetEnvironmentVariableA("PP_AUTO_TASK_COUNT", nullptr);
  }

  if (autoInfo) {
    SetEnvironmentVariableA("PP_AUTO_ID", id.c_str());
    SetEnvironmentVariableA("PP_AUTO_BUNDLE", autoInfo->bundle_root.string().c_str());
    SetEnvironmentVariableA("PP_AUTO_WORKSPACE", autoInfo->workspace_root.string().c_str());
  } else {
    SetEnvironmentVariableA("PP_AUTO_ID", nullptr);
    SetEnvironmentVariableA("PP_AUTO_BUNDLE", nullptr);
    SetEnvironmentVariableA("PP_AUTO_WORKSPACE", nullptr);
  }

  std::string psArgs = "-NoProfile -ExecutionPolicy Bypass -File \"" + engine.string() + "\"";
  psArgs += " -Mode \"" + mode + "\"";
  psArgs += " -Id \"" + id + "\"";
  for (const auto& a : args) psArgs += " \"" + a + "\"";

  out::dim(id.empty() ? std::string("automation: ") + mode : "automation " + id + ": " + mode);
  const std::string full = "powershell.exe " + psArgs;
  const int code = std::system(full.c_str());
  if (code != 0) {
    out::error("automation failed (exit " + std::to_string(code) + ")");
    return false;
  }
  return true;
}

}  // namespace pp
