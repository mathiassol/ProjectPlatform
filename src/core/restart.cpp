#include "core/restart.hpp"

#include "core/projects.hpp"
#include "util/output.hpp"
#include "util/paths.hpp"

#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>
#include <windows.h>

namespace pp {
namespace fs = std::filesystem;

fs::path restartSessionPath() {
  const char* temp = std::getenv("TEMP");
  if (!temp || !*temp) temp = std::getenv("TMP");
  return fs::path(temp ? temp : ".") / "pp-restart-session.json";
}

static std::string jsonEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += c;
    }
  }
  return out;
}

bool saveRestartSession(const RestartSession& session) {
  std::ostringstream o;
  o << "{\n"
    << "  \"version\": 1,\n"
    << "  \"cwd\": \"" << jsonEscape(session.cwd) << "\",\n"
    << "  \"project\": \"" << jsonEscape(session.project) << "\",\n"
    << "  \"project_path\": \"" << jsonEscape(session.project_path) << "\",\n"
    << "  \"env_keys\": [";
  for (size_t i = 0; i < session.env_keys.size(); ++i) {
    if (i) o << ',';
    o << '"' << jsonEscape(session.env_keys[i]) << '"';
  }
  o << "],\n  \"env_vars\": {";
  size_t vi = 0;
  for (const auto& [k, v] : session.env_vars) {
    if (vi++) o << ',';
    o << '"' << jsonEscape(k) << "\":\"" << jsonEscape(v) << '"';
  }
  o << "}\n}\n";

  const auto path = restartSessionPath();
  ensureDir(path.parent_path());
  std::ofstream out(path);
  if (!out) return false;
  out << o.str();
  return static_cast<bool>(out);
}

static std::optional<std::string> jsonGetString(const std::string& json, const std::string& key) {
  const std::regex re("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
  std::smatch m;
  if (std::regex_search(json, m, re) && m.size() > 1) return m[1].str();
  return std::nullopt;
}

static std::vector<std::string> jsonGetStringArray(const std::string& json, const std::string& key) {
  std::vector<std::string> out;
  const std::regex block("\"" + key + "\"\\s*:\\s*\\[(.*?)\\]");
  std::smatch m;
  if (!std::regex_search(json, m, block) || m.size() < 2) return out;
  const std::regex item("\"([^\"]*)\"");
  auto begin = std::sregex_iterator(m[1].first, m[1].second, item);
  auto end = std::sregex_iterator();
  for (auto it = begin; it != end; ++it) out.push_back((*it)[1].str());
  return out;
}

static std::map<std::string, std::string> jsonGetStringMap(const std::string& json,
                                                           const std::string& key) {
  std::map<std::string, std::string> out;
  const std::regex block("\"" + key + "\"\\s*:\\s*\\{(.*?)\\}");
  std::smatch m;
  if (!std::regex_search(json, m, block) || m.size() < 2) return out;
  const std::regex item("\"([^\"]+)\"\\s*:\\s*\"([^\"]*)\"");
  auto begin = std::sregex_iterator(m[1].first, m[1].second, item);
  auto end = std::sregex_iterator();
  for (auto it = begin; it != end; ++it) out[(*it)[1].str()] = (*it)[2].str();
  return out;
}

std::optional<RestartSession> loadRestartSession(const fs::path& path) {
  if (!fs::exists(path)) return std::nullopt;
  std::ifstream in(path);
  if (!in) return std::nullopt;
  const std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

  RestartSession s;
  if (auto v = jsonGetString(json, "cwd")) s.cwd = *v;
  if (auto v = jsonGetString(json, "project")) s.project = *v;
  if (auto v = jsonGetString(json, "project_path")) s.project_path = *v;
  s.env_keys = jsonGetStringArray(json, "env_keys");
  s.env_vars = jsonGetStringMap(json, "env_vars");
  return s;
}

bool clearRestartSession(const fs::path& path) {
  std::error_code ec;
  fs::remove(path, ec);
  return !ec;
}

bool captureRestartSession(RestartSession& session) {
  char cwd[MAX_PATH] = {};
  if (GetCurrentDirectoryA(MAX_PATH, cwd)) session.cwd = cwd;

  if (auto detected = detectProjectFromCwd()) {
    session.project = detected->name;
    session.project_path = detected->path.string();
  } else if (auto cur = getCurrentProject()) {
    session.project = *cur;
    if (auto proj = findProject(*cur)) session.project_path = proj->path.string();
  }

  if (!session.project.empty()) setCurrentProject(session.project);
  return true;
}

static std::string psQuoteSingle(const std::string& s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') out += "''";
    else out.push_back(c);
  }
  out += "'";
  return out;
}

static fs::path findPwsh() {
  char buf[MAX_PATH] = {};
  if (SearchPathA(nullptr, "pwsh.exe", nullptr, MAX_PATH, buf, nullptr)) return fs::path(buf);
  return {};
}

bool spawnRestartTerminal(const fs::path& sessionPath) {
  const fs::path hook = hookScriptPath();
  if (!fs::exists(hook)) {
    out::error("hook script missing — run: pp hook install");
    return false;
  }

  const std::string init =
      ". " + psQuoteSingle(hook.string()) + "; pp-hook-restore " + psQuoteSingle(sessionPath.string());

  fs::path shell = findPwsh();
  std::string args;
  if (!shell.empty()) {
    args = "-NoExit -Command " + psQuoteSingle(init);
  } else {
    shell = fs::path(getenv("COMSPEC") ? getenv("COMSPEC") : "powershell.exe");
    args = "-NoExit -Command " + psQuoteSingle(init);
  }

  std::string cmdLine = "\"" + shell.string() + "\" " + args;
  STARTUPINFOA si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  std::vector<char> cmdBuf(cmdLine.begin(), cmdLine.end());
  cmdBuf.push_back('\0');
  if (!CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr,
                      nullptr, &si, &pi)) {
    out::error("could not start new terminal");
    return false;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
}

bool restoreRestartSession() {
  const auto path = restartSessionPath();
  const auto session = loadRestartSession(path);
  if (!session) return false;

  if (!session->project_path.empty()) SetCurrentDirectoryA(session->project_path.c_str());
  else if (!session->cwd.empty()) SetCurrentDirectoryA(session->cwd.c_str());

  if (!session->project.empty()) setCurrentProject(session->project);
  clearRestartSession(path);
  return true;
}

}  // namespace pp
