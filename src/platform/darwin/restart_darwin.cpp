#include "core/restart.hpp"

#include "core/projects.hpp"
#include "platform/platform.hpp"
#include "util/output.hpp"
#include "util/paths.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>

namespace pp {
namespace fs = std::filesystem;

namespace {

std::string jsonEscape(const std::string& s) {
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

std::string appleScriptString(const std::string& s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '\\' || c == '"') out.push_back('\\');
    out.push_back(c);
  }
  out += "\"";
  return out;
}

std::optional<std::string> jsonGetString(const std::string& json, const std::string& key) {
  const std::regex re("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
  std::smatch m;
  if (std::regex_search(json, m, re) && m.size() > 1) return m[1].str();
  return std::nullopt;
}

std::vector<std::string> jsonGetStringArray(const std::string& json, const std::string& key) {
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

std::map<std::string, std::string> jsonGetStringMap(const std::string& json, const std::string& key) {
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

bool appExists(const char* name) {
  const std::string cmd =
      std::string("osascript -e 'id of application \"") + name + "\"' >/dev/null 2>&1";
  return std::system(cmd.c_str()) == 0;
}

bool preferITerm() {
  if (const char* pref = std::getenv("PP_TERMINAL")) {
    std::string p = pref;
    for (char& c : p) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (p == "iterm" || p == "iterm2") return true;
    if (p == "terminal") return false;
  }
  return appExists("iTerm");
}

}  // namespace

fs::path restartSessionPath() {
  const char* tmp = std::getenv("TMPDIR");
  if (tmp && *tmp) return fs::path(tmp) / "pp-restart-session.json";
  return fs::path("/tmp") / "pp-restart-session.json";
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
  session.cwd = platform::getCwd().string();

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

bool spawnRestartTerminal(const fs::path& sessionPath) {
  const fs::path hook = hookScriptPath();
  std::ostringstream init;
  if (fs::exists(hook)) {
    init << "source " << shellSingleQuote(hook.string()) << "; _pp_hook_restore "
         << shellSingleQuote(sessionPath.string());
  } else {
    init << "cd " << shellSingleQuote(sessionPath.parent_path().string())
         << "; echo '[pp] hook missing — run: pp hook install'; "
         << "session at " << shellSingleQuote(sessionPath.string());
  }

  const std::string script = init.str();
  std::ostringstream osa;
  if (preferITerm()) {
    osa << "tell application \"iTerm\"\n"
        << "  create window with default profile\n"
        << "  tell current session of current window\n"
        << "    write text " << appleScriptString(script) << "\n"
        << "  end tell\n"
        << "  activate\n"
        << "end tell\n";
  } else {
    osa << "tell application \"Terminal\"\n"
        << "  do script " << appleScriptString(script) << "\n"
        << "  activate\n"
        << "end tell\n";
  }

  const auto scriptFile = restartSessionPath().parent_path() / "pp-restart-spawn.scpt.txt";
  {
    std::ofstream out(scriptFile);
    if (!out) {
      out::error("could not write Terminal spawn script");
      return false;
    }
    out << osa.str();
  }

  const std::string cmd = "osascript " + shellSingleQuote(scriptFile.string());
  const int code = std::system(cmd.c_str());
  std::error_code ec;
  fs::remove(scriptFile, ec);
  if (code != 0) {
    out::error("could not open Terminal/iTerm — is Terminal.app available?");
    out::dim("Override with: PP_TERMINAL=Terminal or PP_TERMINAL=iTerm");
    return false;
  }
  return true;
}

bool restoreRestartSession() {
  const auto path = restartSessionPath();
  const auto session = loadRestartSession(path);
  if (!session) return false;

  if (!session->project_path.empty())
    platform::setCwd(session->project_path);
  else if (!session->cwd.empty())
    platform::setCwd(session->cwd);

  if (!session->project.empty()) setCurrentProject(session->project);
  clearRestartSession(path);
  return true;
}

}  // namespace pp
