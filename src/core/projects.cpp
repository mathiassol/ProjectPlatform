#include "core/projects.hpp"

#include "util/output.hpp"
#include "util/paths.hpp"

#include <windows.h>

#include <algorithm>
#include <fstream>
#include <regex>

namespace pp {
namespace fs = std::filesystem;

static std::optional<std::string> jsonGetStr(const std::string& json, const std::string& key) {
  const std::regex re("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
  std::smatch m;
  if (std::regex_search(json, m, re) && m.size() > 1) return m[1].str();
  return std::nullopt;
}

static std::vector<std::string> jsonGetArray(const std::string& json, const std::string& key) {
  std::vector<std::string> result;
  const std::regex block("\"" + key + "\"\\s*:\\s*\\[([^\\]]*)\\]");
  std::smatch m;
  if (!std::regex_search(json, m, block) || m.size() < 2) return result;
  const std::regex item("\"([^\"]+)\"");
  auto begin = std::sregex_iterator(m[1].first, m[1].second, item);
  auto end = std::sregex_iterator();
  for (auto it = begin; it != end; ++it) result.push_back((*it)[1].str());
  return result;
}

AppState loadState() {
  AppState s;
  const auto path = statePath();
  if (!fs::exists(path)) return s;
  std::ifstream in(path);
  if (!in) return s;
  std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (auto v = jsonGetStr(json, "current_project")) s.current_project = *v;
  s.recent = jsonGetArray(json, "recent");
  return s;
}

void saveState(const AppState& state) {
  ensureDir(appDataDir());
  std::ofstream out(statePath());
  out << "{\n  \"current_project\": \"" << state.current_project << "\",\n  \"recent\": [";
  for (size_t i = 0; i < state.recent.size(); ++i) {
    if (i) out << ", ";
    out << "\"" << state.recent[i] << "\"";
  }
  out << "]\n}\n";
}

void touchRecent(AppState& state, const std::string& name) {
  auto& r = state.recent;
  r.erase(std::remove(r.begin(), r.end(), name), r.end());
  r.insert(r.begin(), name);
  if (r.size() > 10) r.resize(10);
}

static bool isValidName(const std::string& name) {
  if (name.empty() || name.find_first_of("<>:\"/\\|?*") != std::string::npos) return false;
  if (name == "." || name == "..") return false;
  return true;
}

std::vector<ProjectInfo> listProjects() {
  std::vector<ProjectInfo> projects;
  const auto root = loadConfig().projects_dir;
  if (!fs::exists(root)) return projects;

  for (const auto& entry : fs::directory_iterator(root)) {
    if (!entry.is_directory()) continue;
    const auto name = entry.path().filename().string();
    if (name.empty() || name[0] == '.') continue;

    ProjectInfo info;
    info.name = name;
    info.path = entry.path();
    info.has_git = fs::exists(entry.path() / ".git");
    info.has_scripts = fs::exists(entry.path() / ".scripts");
    std::error_code ec;
    info.modified = fs::last_write_time(entry.path(), ec);
    projects.push_back(std::move(info));
  }

  std::sort(projects.begin(), projects.end(),
            [](const ProjectInfo& a, const ProjectInfo& b) { return a.name < b.name; });
  return projects;
}

std::optional<ProjectInfo> findProject(const std::string& query) {
  auto all = listProjects();
  for (const auto& p : all)
    if (p.name == query) return p;

  std::vector<ProjectInfo> partial;
  for (const auto& p : all) {
    if (p.name.find(query) != std::string::npos) partial.push_back(p);
  }
  if (partial.size() == 1) return partial[0];
  if (partial.empty()) return std::nullopt;
  return std::nullopt;
}

std::optional<ProjectInfo> projectAtPath(const fs::path& p) {
  const auto root = fs::weakly_canonical(loadConfig().projects_dir);
  const auto canonical = fs::weakly_canonical(p);
  std::error_code ec;
  if (!fs::exists(canonical, ec)) return std::nullopt;

  fs::path cur = canonical;
  while (!cur.empty()) {
    if (fs::equivalent(cur.parent_path(), root, ec)) {
      ProjectInfo info;
      info.name = cur.filename().string();
      info.path = cur;
      info.has_git = fs::exists(cur / ".git");
      info.has_scripts = fs::exists(cur / ".scripts");
      return info;
    }
    if (cur == cur.parent_path()) break;
    cur = cur.parent_path();
  }
  return std::nullopt;
}

std::optional<ProjectInfo> detectProjectFromCwd() {
  char buf[MAX_PATH];
  if (!GetCurrentDirectoryA(MAX_PATH, buf)) return std::nullopt;
  return projectAtPath(fs::path(buf));
}

bool createProject(const std::string& name) {
  if (!isValidName(name)) {
    out::error("invalid project name");
    return false;
  }
  const auto cfg = loadConfig();
  ensureDir(cfg.projects_dir);
  const auto dest = cfg.projects_dir / name;
  if (fs::exists(dest)) {
    out::error("project already exists: " + name);
    return false;
  }
  fs::create_directories(dest);
  fs::create_directories(projectScriptsDir(dest));
  fs::create_directories(dest / ".pp");
  std::ofstream marker(dest / ".pp-project");
  marker << "managed-by=ProjectPlatform\n";
  out::success("created project: " + name);
  return true;
}

bool removeProject(const std::string& name, bool force) {
  auto proj = findProject(name);
  if (!proj) {
    out::error("project not found: " + name);
    return false;
  }
  if (!force) {
    out::warn("this will delete " + proj->path.string());
    out::info("re-run with --force to confirm");
    return false;
  }
  std::error_code ec;
  fs::remove_all(proj->path, ec);
  if (ec) {
    out::error("failed to remove project");
    return false;
  }
  auto state = loadState();
  if (state.current_project == name) state.current_project.clear();
  touchRecent(state, name);
  state.recent.erase(std::remove(state.recent.begin(), state.recent.end(), name), state.recent.end());
  saveState(state);
  out::success("removed project: " + name);
  return true;
}

bool renameProject(const std::string& from, const std::string& to) {
  if (!isValidName(to)) {
    out::error("invalid project name");
    return false;
  }
  auto proj = findProject(from);
  if (!proj) {
    out::error("project not found: " + from);
    return false;
  }
  const auto dest = loadConfig().projects_dir / to;
  if (fs::exists(dest)) {
    out::error("project already exists: " + to);
    return false;
  }
  std::error_code ec;
  fs::rename(proj->path, dest, ec);
  if (ec) {
    out::error("rename failed");
    return false;
  }
  auto state = loadState();
  if (state.current_project == from) state.current_project = to;
  for (auto& r : state.recent)
    if (r == from) r = to;
  saveState(state);
  out::success("renamed " + from + " -> " + to);
  return true;
}

void setCurrentProject(const std::string& name) {
  auto state = loadState();
  state.current_project = name;
  touchRecent(state, name);
  saveState(state);
}

std::optional<std::string> getCurrentProject() {
  const auto s = loadState();
  if (s.current_project.empty()) return std::nullopt;
  return s.current_project;
}

}  // namespace pp
