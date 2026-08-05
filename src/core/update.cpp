#include "core/install.hpp"
#include "core/update.hpp"
#include "util/output.hpp"
#include "util/paths.hpp"
#include "util/progress.hpp"
#include "util/version.hpp"

#include <windows.h>
#include <winhttp.h>

#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>

#pragma comment(lib, "winhttp.lib")

namespace pp {
namespace fs = std::filesystem;

static std::string httpGet(const std::wstring& host, const std::wstring& path, bool useHttps) {
  HINTERNET session = WinHttpOpen(L"ProjectPlatform", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) return {};

  HINTERNET connect = WinHttpConnect(session, host.c_str(), useHttps ? INTERNET_DEFAULT_HTTPS_PORT : 80, 0);
  if (!connect) {
    WinHttpCloseHandle(session);
    return {};
  }

  DWORD flags = useHttps ? WINHTTP_FLAG_SECURE : 0;
  HINTERNET request =
      WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                         WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
  if (!request) {
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return {};
  }

  std::wstring headers = L"User-Agent: ProjectPlatform\r\n";
  WinHttpAddRequestHeaders(request, headers.c_str(), static_cast<DWORD>(headers.size()),
                           WINHTTP_ADDREQ_FLAG_ADD);

  std::string body;
  if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
      WinHttpReceiveResponse(request, nullptr)) {
    DWORD avail = 0;
    do {
      if (!WinHttpQueryDataAvailable(request, &avail) || avail == 0) break;
      std::string chunk(avail, '\0');
      DWORD read = 0;
      if (!WinHttpReadData(request, chunk.data(), avail, &read)) break;
      chunk.resize(read);
      body += chunk;
    } while (avail > 0);
  }

  WinHttpCloseHandle(request);
  WinHttpCloseHandle(connect);
  WinHttpCloseHandle(session);
  return body;
}

static bool downloadUrl(const std::string& url, const fs::path& dest) {
  const auto schemePos = url.find("://");
  if (schemePos == std::string::npos) return false;
  const bool https = url.rfind("https", 0) == 0;
  const auto hostStart = schemePos + 3;
  const auto pathStart = url.find('/', hostStart);
  if (pathStart == std::string::npos) return false;

  const std::string hostStr = url.substr(hostStart, pathStart - hostStart);
  const std::string pathStr = url.substr(pathStart);
  const std::wstring whost(hostStr.begin(), hostStr.end());
  const std::wstring wpath(pathStr.begin(), pathStr.end());

  const std::string data = httpGet(whost, wpath, https);
  if (data.empty()) return false;

  ensureDir(dest.parent_path());
  std::ofstream out(dest, std::ios::binary);
  out.write(data.data(), static_cast<std::streamsize>(data.size()));
  return static_cast<bool>(out);
}

static std::string jsonStringField(const std::string& json, const std::string& key) {
  const std::regex re("\"" + key + "\"\\s*:\\s*\"([^\"]+)\"");
  std::smatch m;
  if (std::regex_search(json, m, re) && m.size() > 1) return m[1].str();
  return {};
}

std::optional<ReleaseInfo> fetchLatestRelease() {
  const std::string json = httpGet(L"api.github.com", L"/repos/" PP_GITHUB_REPO "/releases/latest", true);
  if (json.empty()) return std::nullopt;

  ReleaseInfo info;
  info.tag = jsonStringField(json, "tag_name");
  info.html_url = jsonStringField(json, "html_url");
  if (info.tag.empty()) return std::nullopt;

  if (!info.tag.empty() && info.tag[0] == 'v') info.version = info.tag.substr(1);
  else info.version = info.tag;

  const std::regex assetRe(
      "\\{[^{}]*\"name\"\\s*:\\s*\"ProjectPlatform-[^\"]*-win64\\.zip\"[^{}]*"
      "\"browser_download_url\"\\s*:\\s*\"([^\"]+)\"[^{}]*\\}");
  std::smatch m;
  if (std::regex_search(json, m, assetRe) && m.size() > 1) {
    info.zip_url = m[1].str();
    return info;
  }

  const std::regex altRe("\"browser_download_url\"\\s*:\\s*\"([^\"]*ProjectPlatform[^\"]*win64[^\"]*\\.zip)\"");
  if (std::regex_search(json, m, altRe) && m.size() > 1) {
    info.zip_url = m[1].str();
    return info;
  }
  return std::nullopt;
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
    out::dim("run: pp update");
  }
  return true;
}

static bool extractZip(const fs::path& zip, const fs::path& dest) {
  ensureDir(dest);
  const std::string cmd =
      "powershell -NoProfile -Command \"Expand-Archive -LiteralPath '" + zip.string() +
      "' -DestinationPath '" + dest.string() + "' -Force\"";
  return std::system(cmd.c_str()) == 0;
}

static void cleanupDir(const fs::path& dir) {
  std::error_code ec;
  fs::remove_all(dir, ec);
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

  progress.step("downloading " + release->version);
  const auto tempRoot = fs::temp_directory_path() / "ProjectPlatform-update";
  cleanupDir(tempRoot);
  ensureDir(tempRoot);

  const auto zipPath = tempRoot / "update.zip";
  if (!downloadUrl(release->zip_url, zipPath)) {
    out::error("download failed");
    cleanupDir(tempRoot);
    return false;
  }

  progress.step("extracting");
  const auto extractDir = tempRoot / "extract";
  if (!extractZip(zipPath, extractDir)) {
    out::error("extract failed");
    cleanupDir(tempRoot);
    return false;
  }

  fs::path newExe = extractDir / "pp.exe";
  if (!fs::exists(newExe)) {
    for (const auto& entry : fs::recursive_directory_iterator(extractDir)) {
      if (entry.path().filename() == "pp.exe") {
        newExe = entry.path();
        break;
      }
    }
  }
  if (!fs::exists(newExe)) {
    out::error("pp.exe not found in release zip");
    cleanupDir(tempRoot);
    return false;
  }

  progress.step("installing to " + installDir().string());
  if (!installBinaryToPath(newExe, true)) {
    out::error("install failed");
    cleanupDir(tempRoot);
    return false;
  }

  refreshHookScript();
  cleanupDir(tempRoot);
  progress.done("updated to " + release->version);
  out::dim("Restart your terminal, then run: pp version");
  return true;
}

}  // namespace pp
