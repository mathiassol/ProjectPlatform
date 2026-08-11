#include "core/update.hpp"

#include "core/install.hpp"
#include "platform/platform.hpp"
#include "util/output.hpp"
#include "util/paths.hpp"
#include "util/progress.hpp"
#include "util/version.hpp"

#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>

namespace pp {
namespace fs = std::filesystem;

namespace {

std::string shellSingleQuote(const std::string& s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'')
      out += "'\\''";
    else
      out += c;
  }
  out += "'";
  return out;
}

std::string jsonStringField(const std::string& json, const std::string& key) {
  const std::regex re("\"" + key + "\"\\s*:\\s*\"([^\"]+)\"");
  std::smatch m;
  if (std::regex_search(json, m, re) && m.size() > 1) return m[1].str();
  return {};
}

bool downloadUrl(const std::string& url, const fs::path& dest) {
  ensureDir(dest.parent_path());
  // Binary-safe download via curl (httpGet is text-oriented).
  const std::string cmd = "curl -fsSL --max-time 180 -A ProjectPlatform -o " +
                          shellSingleQuote(dest.string()) + " " + shellSingleQuote(url);
  if (std::system(cmd.c_str()) != 0) return false;
  std::error_code ec;
  return fs::exists(dest, ec) && fs::file_size(dest, ec) > 0 && !ec;
}

bool extractArchive(const fs::path& archive, const fs::path& dest) {
  ensureDir(dest);
  const auto name = archive.filename().string();
  std::string cmd;
  if (name.size() >= 7 && name.substr(name.size() - 7) == ".tar.gz") {
    cmd = "tar -xzf " + shellSingleQuote(archive.string()) + " -C " +
          shellSingleQuote(dest.string());
  } else {
    cmd = "unzip -qo " + shellSingleQuote(archive.string()) + " -d " +
          shellSingleQuote(dest.string());
  }
  return std::system(cmd.c_str()) == 0;
}

void cleanupDir(const fs::path& dir) {
  std::error_code ec;
  fs::remove_all(dir, ec);
}

fs::path findBinaryInExtract(const fs::path& extractDir) {
  const auto direct = extractDir / "pp";
  if (fs::exists(direct)) return direct;
  for (const auto& entry : fs::recursive_directory_iterator(extractDir)) {
    if (!entry.is_regular_file()) continue;
    if (entry.path().filename() == "pp") return entry.path();
  }
  return {};
}

}  // namespace

std::optional<ReleaseInfo> fetchLatestRelease() {
  const std::string body =
      platform::httpGet("https://api.github.com/repos/" + std::string(PP_GITHUB_REPO) +
                        "/releases/latest");
  if (body.empty()) return std::nullopt;

  ReleaseInfo info;
  info.tag = jsonStringField(body, "tag_name");
  info.html_url = jsonStringField(body, "html_url");
  if (info.tag.empty()) return std::nullopt;

  if (!info.tag.empty() && (info.tag[0] == 'v' || info.tag[0] == 'V'))
    info.version = info.tag.substr(1);
  else
    info.version = info.tag;

  // Prefer universal > arm64 > any ProjectPlatform macos asset.
  const std::regex universalRe(
      "\"browser_download_url\"\\s*:\\s*\"([^\"]*ProjectPlatform[^\"]*macos-universal[^\"]*\\.(?:zip|tar\\.gz))\"");
  std::smatch m;
  if (std::regex_search(body, m, universalRe) && m.size() > 1) {
    info.zip_url = m[1].str();
    return info;
  }
  const std::regex arm64Re(
      "\"browser_download_url\"\\s*:\\s*\"([^\"]*ProjectPlatform[^\"]*macos-arm64[^\"]*\\.(?:zip|tar\\.gz))\"");
  if (std::regex_search(body, m, arm64Re) && m.size() > 1) {
    info.zip_url = m[1].str();
    return info;
  }
  const std::regex preferred(
      "\"browser_download_url\"\\s*:\\s*\"([^\"]*ProjectPlatform[^\"]*"
      "(?:macos|darwin|osx)[^\"]*\\.(?:zip|tar\\.gz))\"");
  if (std::regex_search(body, m, preferred) && m.size() > 1) {
    info.zip_url = m[1].str();
    return info;
  }

  const std::regex fallback(
      "\"browser_download_url\"\\s*:\\s*\"([^\"]*(?:macos|darwin|osx)[^\"]*\\.(?:zip|tar\\.gz))\"");
  if (std::regex_search(body, m, fallback) && m.size() > 1) {
    info.zip_url = m[1].str();
    return info;
  }

  // Release exists but no macOS asset yet.
  return info;
}

bool checkUpdate(bool quiet) {
  const auto release = fetchLatestRelease();
  if (!release) {
    if (!quiet) out::error("could not reach GitHub releases");
    return false;
  }

  const int cmp = compareVersions(PP_APP_VERSION, release->version);
  if (cmp >= 0) {
    if (!quiet) out::success(std::string("ProjectPlatform ") + PP_APP_VERSION + " is up to date");
    return true;
  }

  if (!quiet) {
    out::info("update available: " + release->version + " (current: " + PP_APP_VERSION + ")");
    out::dim(release->html_url);
    if (release->zip_url.empty())
      out::dim("no macOS asset on this release yet — build from source or wait for the zip");
    else
      out::dim("run: pp update");
  }
  return true;
}

bool performUpdate(bool force) {
  Progress progress("update");
  progress.step("checking GitHub releases");

  const auto release = fetchLatestRelease();
  if (!release) {
    out::error("could not fetch latest release");
    return false;
  }

  const int cmp = compareVersions(PP_APP_VERSION, release->version);
  if (!force && cmp >= 0) {
    out::success(std::string("already on latest (") + PP_APP_VERSION + ")");
    return true;
  }

  if (release->zip_url.empty()) {
    out::error("latest release has no macOS asset");
    out::dim(release->html_url.empty()
                 ? ("https://github.com/" + std::string(PP_GITHUB_REPO) + "/releases")
                 : release->html_url);
    out::dim("Expected: ProjectPlatform-vX.Y.Z-macos-universal.zip");
    return false;
  }

  progress.step("downloading " + release->version);
  const auto tempRoot = fs::temp_directory_path() / "ProjectPlatform-update";
  cleanupDir(tempRoot);
  ensureDir(tempRoot);

  const bool isTar = release->zip_url.find(".tar.gz") != std::string::npos;
  const auto archivePath = tempRoot / (isTar ? "update.tar.gz" : "update.zip");
  if (!downloadUrl(release->zip_url, archivePath)) {
    out::error("download failed");
    cleanupDir(tempRoot);
    return false;
  }

  progress.step("extracting");
  const auto extractDir = tempRoot / "extract";
  if (!extractArchive(archivePath, extractDir)) {
    out::error("extract failed");
    cleanupDir(tempRoot);
    return false;
  }

  const auto newExe = findBinaryInExtract(extractDir);
  if (newExe.empty()) {
    out::error("pp binary not found in release archive");
    cleanupDir(tempRoot);
    return false;
  }

  progress.step("installing to " + installDir().string());
  std::string installError;
  if (!replaceInstalledBinary(newExe, installError)) {
    out::error("install failed: " + installError);
    cleanupDir(tempRoot);
    return false;
  }
  addInstallDirToPath();
  refreshHookScript();
  cleanupDir(tempRoot);
  progress.done("updated to " + release->version);
  out::info("Update applied. Restart your terminal, then run: pp version");
  out::dim("If the hook is installed: source ~/.zshrc");
  return true;
}

}  // namespace pp
