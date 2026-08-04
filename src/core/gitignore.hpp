#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace pp {

class GitignoreMatcher {
 public:
  void addPattern(std::string pattern);
  bool loadFile(const std::filesystem::path& gitignorePath);
  bool isIgnored(const std::filesystem::path& root, const std::filesystem::path& relative,
                 bool isDir) const;

 private:
  struct Pattern {
    std::string raw;
    bool negated = false;
    bool dirOnly = false;
  };
  std::vector<Pattern> patterns_;

  static std::string normalizePattern(std::string p, bool& dirOnly);
  static bool matchPattern(const std::string& pattern, const std::string& path, bool isDir);
};

bool copyRespectingGitignore(const std::filesystem::path& src, const std::filesystem::path& dst,
                             bool warnNoGitignore, bool forceNoGitignore, int& copied,
                             int& skipped);

}  // namespace pp
