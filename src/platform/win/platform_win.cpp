#include "platform/platform.hpp"

#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
#include <wincrypt.h>

#include <cstdlib>
#include <vector>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")

namespace pp {
namespace platform {
namespace {

std::string base64Encode(const BYTE* data, DWORD len) {
  static const char* k =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((len + 2) / 3 * 4);
  for (DWORD i = 0; i < len; i += 3) {
    const unsigned a = data[i];
    const unsigned b = i + 1 < len ? data[i + 1] : 0;
    const unsigned c = i + 2 < len ? data[i + 2] : 0;
    out.push_back(k[a >> 2]);
    out.push_back(k[((a & 3) << 4) | (b >> 4)]);
    out.push_back(i + 1 < len ? k[((b & 15) << 2) | (c >> 6)] : '=');
    out.push_back(i + 2 < len ? k[c & 63] : '=');
  }
  return out;
}

bool base64Decode(const std::string& in, std::vector<BYTE>& out) {
  auto val = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  std::string clean;
  for (char c : in) {
    if (c == '=' || val(c) >= 0) clean.push_back(c);
  }
  if (clean.size() % 4 != 0) return false;
  out.clear();
  for (size_t i = 0; i < clean.size(); i += 4) {
    const int a = val(clean[i]);
    const int b = val(clean[i + 1]);
    const int c = clean[i + 2] == '=' ? 0 : val(clean[i + 2]);
    const int d = clean[i + 3] == '=' ? 0 : val(clean[i + 3]);
    if (a < 0 || b < 0) return false;
    out.push_back(static_cast<BYTE>((a << 2) | (b >> 4)));
    if (clean[i + 2] != '=') out.push_back(static_cast<BYTE>(((b & 15) << 4) | (c >> 2)));
    if (clean[i + 3] != '=') out.push_back(static_cast<BYTE>(((c & 3) << 6) | d));
  }
  return true;
}

std::wstring utf8ToWide(const std::string& s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
  if (n <= 0) return {};
  std::wstring out(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), n);
  return out;
}

}  // namespace

const char* osName() { return "windows"; }

std::filesystem::path getExePath() {
  char buf[MAX_PATH];
  const DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
  return n ? std::filesystem::path(std::string(buf, n)) : std::filesystem::path{};
}

std::filesystem::path getCwd() {
  char buf[MAX_PATH];
  if (!GetCurrentDirectoryA(MAX_PATH, buf)) return {};
  return std::filesystem::path(buf);
}

bool setCwd(const std::filesystem::path& path) {
  return SetCurrentDirectoryA(path.string().c_str()) != 0;
}

std::filesystem::path userHomeDir() {
  if (const char* v = std::getenv("USERPROFILE")) return std::filesystem::path(v);
  return {};
}

std::filesystem::path userDocumentsDir() {
  const auto home = userHomeDir();
  if (home.empty()) return std::filesystem::path("Documents");
  return home / "Documents";
}

std::filesystem::path appDataRoot() {
  if (const char* v = std::getenv("LOCALAPPDATA")) return std::filesystem::path(v) / "ProjectPlatform";
  return userHomeDir() / "AppData" / "Local" / "ProjectPlatform";
}

std::optional<std::string> getEnv(const char* name) {
  const char* v = std::getenv(name);
  if (!v) return std::nullopt;
  return std::string(v);
}

bool setEnv(const char* name, const char* value) {
  return SetEnvironmentVariableA(name, value) != 0;
}

bool unsetEnv(const char* name) { return SetEnvironmentVariableA(name, nullptr) != 0; }

bool encryptSecret(const std::string& plain, std::string& out) {
  DATA_BLOB in{};
  in.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()));
  in.cbData = static_cast<DWORD>(plain.size());
  DATA_BLOB blob{};
  if (!CryptProtectData(&in, L"ProjectPlatform", nullptr, nullptr, nullptr, 0, &blob)) return false;
  out = base64Encode(blob.pbData, blob.cbData);
  LocalFree(blob.pbData);
  return true;
}

bool decryptSecret(const std::string& encrypted, std::string& plainOut) {
  std::vector<BYTE> raw;
  if (!base64Decode(encrypted, raw)) return false;
  DATA_BLOB in{};
  in.pbData = raw.data();
  in.cbData = static_cast<DWORD>(raw.size());
  DATA_BLOB blob{};
  if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &blob)) return false;
  plainOut.assign(reinterpret_cast<char*>(blob.pbData), blob.cbData);
  LocalFree(blob.pbData);
  return true;
}

std::string httpGet(const std::string& url) {
  // Minimal URL parse: https://host/path
  std::string rest = url;
  bool https = true;
  if (rest.rfind("https://", 0) == 0) {
    https = true;
    rest = rest.substr(8);
  } else if (rest.rfind("http://", 0) == 0) {
    https = false;
    rest = rest.substr(7);
  } else {
    return {};
  }
  const auto slash = rest.find('/');
  const std::string host = slash == std::string::npos ? rest : rest.substr(0, slash);
  const std::string path = slash == std::string::npos ? "/" : rest.substr(slash);
  const auto whost = utf8ToWide(host);
  const auto wpath = utf8ToWide(path);

  HINTERNET session = WinHttpOpen(L"ProjectPlatform", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) return {};
  HINTERNET connect =
      WinHttpConnect(session, whost.c_str(), https ? INTERNET_DEFAULT_HTTPS_PORT : 80, 0);
  if (!connect) {
    WinHttpCloseHandle(session);
    return {};
  }
  DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
  HINTERNET request = WinHttpOpenRequest(connect, L"GET", wpath.c_str(), nullptr, WINHTTP_NO_REFERER,
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
  if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0,
                         0) &&
      WinHttpReceiveResponse(request, nullptr)) {
    DWORD avail = 0;
    do {
      if (!WinHttpQueryDataAvailable(request, &avail) || avail == 0) break;
      std::string chunk(avail, '\0');
      DWORD read = 0;
      if (!WinHttpReadData(request, chunk.data(), avail, &read)) break;
      body.append(chunk.data(), read);
    } while (avail > 0);
  }
  WinHttpCloseHandle(request);
  WinHttpCloseHandle(connect);
  WinHttpCloseHandle(session);
  return body;
}

void initConsole() {
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  if (hOut == INVALID_HANDLE_VALUE) return;
  DWORD mode = 0;
  if (!GetConsoleMode(hOut, &mode)) return;
  SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}

bool runCommand(const std::string& command, const std::filesystem::path& cwd) {
  std::filesystem::path prev;
  if (!cwd.empty()) {
    prev = getCwd();
    if (!setCwd(cwd)) return false;
  }
  const int code = std::system(command.c_str());
  if (!cwd.empty() && !prev.empty()) setCwd(prev);
  return code == 0;
}

bool openPath(const std::filesystem::path& path) {
  HINSTANCE r =
      ShellExecuteA(nullptr, "open", path.string().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  return reinterpret_cast<intptr_t>(r) > 32;
}

}  // namespace platform
}  // namespace pp
