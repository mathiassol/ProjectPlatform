#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pp {

struct TemplateInfo {
  std::string name;
  std::filesystem::path path;
  bool has_gitignore = false;
  std::string description;
};

std::vector<TemplateInfo> listTemplates();
std::optional<TemplateInfo> findTemplate(const std::string& name);

bool addTemplateFromProject(const std::string& projectQuery, const std::string& templateName,
                            bool forceNoGitignore);
bool createProjectFromTemplate(const std::string& templateName, const std::string& projectName,
                               bool forceNoGitignore);
bool removeTemplate(const std::string& name, bool force);

}  // namespace pp
