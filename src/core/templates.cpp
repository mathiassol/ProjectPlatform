#include "core/templates.hpp"

#include "core/gitignore.hpp"
#include "core/projects.hpp"
#include "util/output.hpp"
#include "util/paths.hpp"
#include "util/progress.hpp"

#include <fstream>

namespace pp {
namespace fs = std::filesystem;

std::vector<TemplateInfo> listTemplates() {
  std::vector<TemplateInfo> result;
  const auto root = loadConfig().templates_dir;
  if (!fs::exists(root)) return result;

  for (const auto& entry : fs::directory_iterator(root)) {
    if (!entry.is_directory()) continue;
    TemplateInfo t;
    t.name = entry.path().filename().string();
    t.path = entry.path();
    t.has_gitignore = fs::exists(entry.path() / ".gitignore");
    const auto meta = entry.path() / ".pp-template.json";
    if (fs::exists(meta)) {
      std::ifstream in(meta);
      std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      const auto pos = json.find("\"description\"");
      if (pos != std::string::npos) {
        const auto q1 = json.find('"', pos + 13);
        const auto q2 = json.find('"', q1 + 1);
        if (q1 != std::string::npos && q2 != std::string::npos)
          t.description = json.substr(q1 + 1, q2 - q1 - 1);
      }
    }
    result.push_back(std::move(t));
  }
  std::sort(result.begin(), result.end(),
            [](const TemplateInfo& a, const TemplateInfo& b) { return a.name < b.name; });
  return result;
}

std::optional<TemplateInfo> findTemplate(const std::string& name) {
  for (const auto& t : listTemplates())
    if (t.name == name) return t;
  return std::nullopt;
}

bool addTemplateFromProject(const std::string& projectQuery, const std::string& templateName,
                            bool forceNoGitignore) {
  auto proj = findProject(projectQuery);
  if (!proj) {
    out::error("project not found: " + projectQuery);
    return false;
  }

  const auto cfg = loadConfig();
  ensureDir(cfg.templates_dir);
  const auto dest = cfg.templates_dir / templateName;
  if (fs::exists(dest)) {
    out::error("template already exists: " + templateName);
    return false;
  }

  Progress progress("template");
  progress.step("creating template from " + proj->name);

  int copied = 0, skipped = 0;
  if (!copyRespectingGitignore(proj->path, dest, true, forceNoGitignore, copied, skipped))
    return false;

  std::ofstream meta(dest / ".pp-template.json");
  meta << "{\n  \"source\": \"" << proj->name << "\",\n  \"description\": \"Template from "
       << proj->name << "\"\n}\n";

  out::success("template '" + templateName + "' saved to " + dest.string());
  return true;
}

bool createProjectFromTemplate(const std::string& templateName, const std::string& projectName,
                               bool forceNoGitignore) {
  auto tmpl = findTemplate(templateName);
  if (!tmpl) {
    out::error("template not found: " + templateName);
    return false;
  }

  if (!createProject(projectName)) return false;

  const auto dest = loadConfig().projects_dir / projectName;
  Progress progress("scaffold");
  progress.step("copying template " + templateName);

  int copied = 0, skipped = 0;
  for (const auto& entry : fs::directory_iterator(tmpl->path)) {
    const auto name = entry.path().filename().string();
    if (name == ".pp-template.json") continue;
    const auto target = dest / entry.path().filename();
    if (entry.is_directory()) {
      copyRespectingGitignore(entry.path(), target, tmpl->has_gitignore, forceNoGitignore, copied,
                              skipped);
    } else {
      fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing);
      ++copied;
    }
  }

  progress.done("project '" + projectName + "' created from template '" + templateName + "'");
  setCurrentProject(projectName);
  return true;
}

bool removeTemplate(const std::string& name, bool force) {
  auto tmpl = findTemplate(name);
  if (!tmpl) {
    out::error("template not found: " + name);
    return false;
  }
  if (!force) {
    out::warn("this will delete template " + name);
    out::info("re-run with --force to confirm");
    return false;
  }
  std::error_code ec;
  fs::remove_all(tmpl->path, ec);
  if (ec) {
    out::error("failed to remove template");
    return false;
  }
  out::success("removed template: " + name);
  return true;
}

}  // namespace pp
