#include "core/gitignore.hpp"

#include "util/output.hpp"
#include "util/progress.hpp"

#include <algorithm>
#include <fstream>
#include <functional>

namespace pp {
namespace fs = std::filesystem;

std::string GitignoreMatcher::normalizePattern(std::string p, bool& dirOnly) {
  dirOnly = false;
  while (!p.empty() && (p.back() == ' ' || p.back() == '\t')) p.pop_back();
  while (!p.empty() && (p.front() == ' ' || p.front() == '\t')) p.erase(p.begin());
  if (!p.empty() && p.back() == '/') {
    dirOnly = true;
    p.pop_back();
  }
  if (!p.empty() && p.front() == '/') p.erase(p.begin());
  std::replace(p.begin(), p.end(), '\\', '/');
  return p;
}

void GitignoreMatcher::addPattern(std::string pattern) {
  if (pattern.empty() || pattern[0] == '#') return;
  Pattern p;
  if (pattern[0] == '!') {
    p.negated = true;
    pattern.erase(pattern.begin());
  }
  p.raw = normalizePattern(pattern, p.dirOnly);
  if (p.raw.empty()) return;
  patterns_.push_back(std::move(p));
}

bool GitignoreMatcher::loadFile(const fs::path& gitignorePath) {
  std::ifstream in(gitignorePath);
  if (!in) return false;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const auto hash = line.find('#');
    if (hash != std::string::npos) line = line.substr(0, hash);
    addPattern(line);
  }
  return true;
}

static bool wildcardMatch(const std::string& text, const std::string& pattern) {
  size_t ti = 0, pi = 0;
  size_t starPi = std::string::npos, starTi = 0;
  while (ti <= text.size()) {
    if (pi < pattern.size() && (pattern[pi] == '?' || pattern[pi] == text[ti])) {
      ++ti;
      ++pi;
    } else if (pi < pattern.size() && pattern[pi] == '*') {
      starPi = pi++;
      starTi = ti;
    } else if (starPi != std::string::npos) {
      pi = starPi + 1;
      ti = ++starTi;
    } else {
      return false;
    }
  }
  while (pi < pattern.size() && pattern[pi] == '*') ++pi;
  return pi == pattern.size();
}

bool GitignoreMatcher::matchPattern(const std::string& pattern, const std::string& path,
                                    bool isDir) {
  if (pattern.empty()) return false;
  if (wildcardMatch(path, pattern)) return true;

  const auto slash = path.find_last_of('/');
  const std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
  if (wildcardMatch(base, pattern)) return true;

  if (pattern.find('/') == std::string::npos) {
    if (path.find(pattern) != std::string::npos) return true;
  }
  return false;
}

bool GitignoreMatcher::isIgnored(const fs::path& root, const fs::path& relative,
                                 bool isDir) const {
  const std::string rel = relative.generic_string();
  if (rel == ".git" || rel.find(".git/") == 0) return true;

  bool ignored = false;
  for (const auto& p : patterns_) {
    if (p.dirOnly && !isDir) continue;
    if (matchPattern(p.raw, rel, isDir)) ignored = !p.negated;
  }
  return ignored;
}

static void copyTree(const fs::path& src, const fs::path& dst, const GitignoreMatcher& matcher,
                     int& copied, int& skipped, Progress& progress) {
  std::function<void(const fs::path&, const fs::path&)> walk;
  walk = [&](const fs::path& curSrc, const fs::path& rel) {
    for (const auto& entry : fs::directory_iterator(curSrc, fs::directory_options::skip_permission_denied)) {
      const auto name = entry.path().filename();
      const auto childRel = rel.empty() ? name : rel / name;
      const bool isDir = entry.is_directory();

      if (matcher.isIgnored(src, childRel, isDir)) {
        ++skipped;
        continue;
      }

      const auto target = dst / childRel;
      if (isDir) {
        fs::create_directories(target);
        walk(entry.path(), childRel);
      } else {
        fs::create_directories(target.parent_path());
        std::error_code ec;
        fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing, ec);
        if (!ec) {
          ++copied;
          if (copied % 25 == 0) progress.step("copied " + std::to_string(copied) + " files...");
        }
      }
    }
  };
  walk(src, {});
}

bool copyRespectingGitignore(const fs::path& src, const fs::path& dst, bool warnNoGitignore,
                             bool forceNoGitignore, int& copied, int& skipped) {
  if (!fs::exists(src) || !fs::is_directory(src)) return false;

  GitignoreMatcher matcher;
  const auto gi = src / ".gitignore";
  const bool hasGi = fs::exists(gi);

  if (!hasGi) {
    if (warnNoGitignore) {
      out::warn("no .gitignore found in " + src.string());
      out::warn("all files will be copied unless you use --yes to continue");
      if (!forceNoGitignore) return false;
    }
    matcher.addPattern(".git");
    matcher.addPattern("build");
    matcher.addPattern("node_modules");
    matcher.addPattern(".vs");
    matcher.addPattern("x64");
    matcher.addPattern("Debug");
    matcher.addPattern("Release");
  } else {
    matcher.loadFile(gi);
    matcher.addPattern(".git");
  }

  Progress progress("copy");
  progress.step("scanning " + src.filename().string());
  fs::create_directories(dst);
  copyTree(src, dst, matcher, copied, skipped, progress);
  progress.done("copied " + std::to_string(copied) + " files, skipped " +
                std::to_string(skipped));
  return true;
}

}  // namespace pp
