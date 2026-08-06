#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pp {

struct AutomationInfo {
  std::string id;
  std::string name;
  std::string version;
  std::string description;
  std::filesystem::path bundle_root;   // installed template (AppData)
  std::filesystem::path workspace_root;  // user workspace (Documents/Automations/<id>)
  bool setup_complete = false;
};

std::filesystem::path automationsEngineDir();
std::filesystem::path automationsBundleDir();
std::filesystem::path automationsWorkspaceRoot();
std::filesystem::path automationWorkspace(const std::string& id);

bool loadAutomationManifest(const std::filesystem::path& bundleRoot, AutomationInfo& out);
std::vector<AutomationInfo> discoverAutomations();
std::optional<AutomationInfo> findAutomation(const std::string& id);

bool installBundledAutomations();
bool runAutomationEngine(const std::string& id, const std::string& mode,
                         const std::vector<std::string>& args, bool force, bool dry_run,
                         int task_count, bool quiet, bool auto_no_agent, bool auto_prompt_setup);

void printAutomationHelp(const AutomationInfo& info);

}  // namespace pp
