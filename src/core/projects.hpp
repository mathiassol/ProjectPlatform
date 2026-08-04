#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pp {

struct ProjectInfo {
  std::string name;
  std::filesystem::path path;
  bool has_git = false;
  bool has_scripts = false;
  std::filesystem::file_time_type modified{};
};

struct AppState {
  std::string current_project;
  std::vector<std::string> recent;
};

AppState loadState();
void saveState(const AppState& state);
void touchRecent(AppState& state, const std::string& name);

std::vector<ProjectInfo> listProjects();
std::optional<ProjectInfo> findProject(const std::string& query);
std::optional<ProjectInfo> projectAtPath(const std::filesystem::path& p);
std::optional<ProjectInfo> detectProjectFromCwd();

bool createProject(const std::string& name);
bool removeProject(const std::string& name, bool force);
bool renameProject(const std::string& from, const std::string& to);

void setCurrentProject(const std::string& name);
std::optional<std::string> getCurrentProject();

}  // namespace pp
