#include "core/install.hpp"

#include "core/automations.hpp"
#include "core/envstore.hpp"
#include "core/plugins.hpp"
#include "platform/platform.hpp"
#include "util/output.hpp"
#include "util/paths.hpp"
#include "util/progress.hpp"
#include "util/version.hpp"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace pp {
namespace fs = std::filesystem;

std::string getExePath() { return platform::getExePath().string(); }

namespace {

constexpr const char* kPathBegin = "# >>> ProjectPlatform PATH >>>";
constexpr const char* kPathEnd = "# <<< ProjectPlatform PATH <<<";
constexpr const char* kHookBegin = "# ProjectPlatform hook";
constexpr const char* kHookEnd = "# End ProjectPlatform hook";

fs::path zprofilePath() {
  const auto home = platform::userHomeDir();
  if (home.empty()) return {};
  return home / ".zprofile";
}

fs::path zshrcPath() {
  const auto home = platform::userHomeDir();
  if (home.empty()) return {};
  return home / ".zshrc";
}

fs::path fishConfigPath() {
  const auto home = platform::userHomeDir();
  if (home.empty()) return {};
  return home / ".config" / "fish" / "config.fish";
}

fs::path fishHookScriptPath() { return appDataDir() / "pp-hook.fish"; }

std::string pathExportLine() {
  // Quote path for spaces in "Application Support"
  return "export PATH=\"" + installDir().string() + ":$PATH\"";
}

bool pathBlockPresent(const std::string& content) {
  return content.find(kPathBegin) != std::string::npos;
}

bool writeZprofilePathBlock() {
  const auto profile = zprofilePath();
  if (profile.empty()) return false;

  std::string content;
  if (fs::exists(profile)) {
    std::ifstream in(profile);
    content.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  }

  if (pathBlockPresent(content)) return true;

  if (!content.empty() && content.back() != '\n') content += '\n';
  content += "\n";
  content += kPathBegin;
  content += "\n";
  content += pathExportLine();
  content += "\n";
  content += kPathEnd;
  content += "\n";

  ensureDir(profile.parent_path());
  std::ofstream out(profile);
  if (!out) return false;
  out << content;
  return true;
}

bool removeZprofilePathBlock() {
  const auto profile = zprofilePath();
  if (profile.empty() || !fs::exists(profile)) return true;

  std::ifstream in(profile);
  std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const auto begin = content.find(kPathBegin);
  if (begin == std::string::npos) return true;
  auto end = content.find(kPathEnd, begin);
  if (end == std::string::npos) return false;
  end += std::char_traits<char>::length(kPathEnd);
  while (end < content.size() && (content[end] == '\n' || content[end] == '\r')) ++end;
  content.erase(begin, end - begin);
  std::ofstream out(profile);
  if (!out) return false;
  out << content;
  return true;
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

fs::path findBundledHookAsset() {
  const auto exe = platform::getExePath();
  if (exe.empty()) return {};
  fs::path p = exe.parent_path();
  for (int i = 0; i < 8; ++i) {
    const auto cand = p / "assets" / "pp-hook.zsh";
    if (fs::exists(cand)) return cand;
    if (p == p.parent_path()) break;
    p = p.parent_path();
  }
  return {};
}

// Fallback when assets/ are not next to the installed binary.
constexpr const char* kEmbeddedZshHook = R"PPHOOK(# Prefer the real binary (avoid recursion with the `pp` shell function).
_pp_bin() {
  if [[ -n "${PP_BIN:-}" && -x "$PP_BIN" ]]; then
    print -r -- "$PP_BIN"
    return
  fi
  if [[ -x "${PP_HOOK_PATH:h}/bin/pp" ]]; then
    print -r -- "${PP_HOOK_PATH:h}/bin/pp"
    return
  fi
  print -r -- "pp"
}

_pp_normalize_path() {
  local p="${1:-}"
  [[ -z "$p" ]] && { print -r -- ""; return; }
  p="${p//\\//}"
  p="${p%/}"
  if command -v python3 >/dev/null 2>&1; then
    python3 -c 'import os,sys; print(os.path.realpath(os.path.expanduser(sys.argv[1])))' "$p" 2>/dev/null && return
  fi
  print -r -- "$p"
}

_pp_sync_project() {
  local bin info name root
  bin="$(_pp_bin)"
  info="$("$bin" here --json 2>/dev/null)" || {
    if [[ -z "${PP_PROJECT:-}" ]]; then
      unset PP_PROJECT_PATH PP_PROJECT_ROOT
    fi
    return 1
  }
  name="${info//$'\n'/}"
  name="${name## }"
  name="${name%% }"
  [[ -z "$name" ]] && return 1
  export PP_PROJECT="$name"
  root="$("$bin" cd "$name" --quiet 2>/dev/null)" || true
  if [[ -n "$root" ]]; then
    export PP_PROJECT_ROOT="$(_pp_normalize_path "$root")"
  fi
  export PP_PROJECT_PATH="$PWD"
  print -r -- "$name"
  return 0
}

_pp_env_json() {
  local bin raw
  bin="$(_pp_bin)"
  raw="$("$bin" "$@" 2>/dev/null)" || return $?
  raw="${raw##[[:space:]]}"
  raw="${raw%%[[:space:]]}"
  [[ "$raw" == \{* ]] || return 1
  if ! command -v python3 >/dev/null 2>&1; then
    print -u2 -- "[pp] python3 required for env apply in zsh hook"
    return 1
  fi
  eval "$(print -r -- "$raw" | python3 -c '
import json, sys, shlex
data = json.loads(sys.stdin.read())
if data.get("clear"):
    for k in data.get("keys") or []:
        print(f"unset {shlex.quote(str(k))}")
    print("typeset -ga _PP_ENV_KEYS=()")
    print("unset PP_ENV_LOADED")
else:
    keys = [str(k) for k in (data.get("keys") or [])]
    print("typeset -ga _PP_ENV_KEYS=(" + " ".join(shlex.quote(k) for k in keys) + ")")
    for k, v in (data.get("vars") or {}).items():
        print(f"export {shlex.quote(str(k))}={shlex.quote(str(v))}")
    print("export PP_ENV_LOADED=1")
')"
}

_pp_env_apply() {
  _pp_env_json env apply --shell
}

_pp_env_shell() {
  if ! _pp_env_json env "$@" --shell; then
    "$(_pp_bin)" env "$@"
  fi
}

_pp_cd() {
  local name="$1"
  local quiet=0
  shift || true
  for a in "$@"; do
    [[ "$a" == "--quiet" || "$a" == "-q" ]] && quiet=1
  done
  local bin path
  bin="$(_pp_bin)"
  path="$("$bin" cd "$name" --quiet 2>/dev/null)" || {
    "$bin" cd "$name"
    return 1
  }
  path="${path//$'\n'/}"
  _pp_env_json env clear --shell >/dev/null 2>&1 || true
  cd -- "$path" || return 1
  export PP_PROJECT="$name"
  export PP_PROJECT_PATH="$path"
  export PP_PROJECT_ROOT="$(_pp_normalize_path "$path")"
  (( quiet )) || print -P "%F{cyan}-> $path%f"
  _pp_env_apply >/dev/null 2>&1 || true
  return 0
}

_pp_prompt_label() {
  local project="$1"
  local root cwd rel
  root="$(_pp_normalize_path "${PP_PROJECT_ROOT:-}")"
  if [[ -z "$root" ]]; then
    root="$("$(_pp_bin)" cd "$project" --quiet 2>/dev/null)" || { print -r -- "$project"; return; }
    root="$(_pp_normalize_path "$root")"
    export PP_PROJECT_ROOT="$root"
  fi
  cwd="$(_pp_normalize_path "$PWD")"
  if (( ${#cwd} <= ${#root} )); then
    print -r -- "$project"
    return
  fi
  if [[ "${cwd:l}" != "${root:l}"* ]]; then
    print -r -- "$project"
    return
  fi
  rel="${cwd#$root}"
  rel="${rel#/}"
  [[ -z "$rel" ]] && { print -r -- "$project"; return; }
  print -r -- "$project/$rel"
}

pp_hook_prompt_precmd() {
  [[ -n "${PP_HOOK_PATH:-}" && -f "$PP_HOOK_PATH" ]] && source "$PP_HOOK_PATH" 2>/dev/null

  local project
  project="$(_pp_sync_project)" || project=""
  if [[ -n "$project" ]]; then
    if [[ -z ${_PP_PROMPT_ACTIVE:-} ]]; then
      typeset -g _PP_SAVED_PROMPT="$PROMPT"
      typeset -g _PP_PROMPT_ACTIVE=1
    fi
    PROMPT="PP:$(_pp_prompt_label "$project")> "
  elif [[ -n ${_PP_PROMPT_ACTIVE:-} ]]; then
    PROMPT="${_PP_SAVED_PROMPT:-%n@%m %1~ %# }"
    unset _PP_PROMPT_ACTIVE
  fi
}

pp_hook_dispatch() {
  if (( $# >= 1 )) && [[ "$1" == "restart" ]]; then
    # Prefer assets/pp-hook.zsh body (copied on install). Fallback for older embeds:
    if typeset -f _pp_restart >/dev/null 2>&1; then
      _pp_restart
      return $?
    fi
    "$(_pp_bin)" restart
    return $?
  fi
  if (( $# >= 2 )); then
    case "$1" in
      cd|goto|go|enter)
        local name="$2"
        shift 2
        _pp_cd "$name" "$@"
        return $?
        ;;
      env)
        if [[ "$2" == "apply" || "$2" == "clear" || "$2" == "load" ]]; then
          shift
          _pp_env_shell "$@"
          return $?
        fi
        ;;
    esac
  fi
  "$(_pp_bin)" "$@"
}

ppgo() {
  _pp_cd "$1"
}

if [[ -z ${_PP_ZSH_HOOK_READY:-} ]]; then
  typeset -g _PP_ZSH_HOOK_READY=1
  autoload -Uz add-zsh-hook 2>/dev/null || true
  if typeset -f add-zsh-hook >/dev/null 2>&1; then
    add-zsh-hook precmd pp_hook_prompt_precmd
  else
    if typeset -f precmd >/dev/null 2>&1; then
      functions[_pp_user_precmd]=$functions[precmd]
      precmd() { _pp_user_precmd; pp_hook_prompt_precmd; }
    else
      precmd() { pp_hook_prompt_precmd; }
    fi
  fi
fi
)PPHOOK";

std::string hookBody() {
  const auto asset = findBundledHookAsset();
  if (!asset.empty()) {
    std::ifstream in(asset);
    if (in) {
      std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      // Skip leading comment banner from the reference file.
      const auto pos = content.find("_pp_bin()");
      if (pos != std::string::npos) {
        // Include from the blank line before _pp_bin, or from _pp_bin.
        auto start = content.rfind('\n', pos);
        if (start == std::string::npos) start = 0;
        else ++start;
        // Prefer content starting at "# Prefer" or "_pp_bin"
        const auto prefer = content.find("# Prefer the real binary");
        if (prefer != std::string::npos && prefer < pos) return content.substr(prefer);
        return content.substr(start);
      }
      return content;
    }
  }
  return kEmbeddedZshHook;
}

// Appended when the embedded base hook predates Phase 5 restart helpers.
constexpr const char* kRestartHelpersZsh = R"PPRESTART(
_pp_hook_restore() {
  local session="${1:-}"
  [[ -z "$session" || ! -f "$session" ]] && {
    print -P "%F{yellow}[pp] No restart session found%f"
    return 1
  }
  if ! command -v python3 >/dev/null 2>&1; then
    print -u2 -- "[pp] python3 required to restore restart session"
    return 1
  fi
  eval "$(python3 - "$session" <<'PY'
import json, sys, shlex
path = sys.argv[1]
with open(path, encoding="utf-8") as f:
    data = json.load(f)
cwd = data.get("project_path") or data.get("cwd") or ""
project = data.get("project") or ""
if cwd:
    print(f"cd -- {shlex.quote(cwd)} || true")
if project:
    print(f"export PP_PROJECT={shlex.quote(project)}")
    if cwd:
        print(f"export PP_PROJECT_PATH={shlex.quote(cwd)}")
        print(f"export PP_PROJECT_ROOT={shlex.quote(cwd)}")
vars = data.get("env_vars") or {}
keys = [str(k) for k in (data.get("keys") or data.get("env_keys") or list(vars.keys()))]
if vars:
    print("typeset -ga _PP_ENV_KEYS=(" + " ".join(shlex.quote(k) for k in keys) + ")")
    for k, v in vars.items():
        print(f"export {shlex.quote(str(k))}={shlex.quote(str(v))}")
    print("export PP_ENV_LOADED=1")
elif project:
    print("_pp_env_apply >/dev/null 2>&1 || true")
print(f"rm -f -- {shlex.quote(path)}")
print('print -P "%F{green}[pp] Session restored -> $PWD%f"')
PY
)"
}

_pp_restart() {
  local session="${TMPDIR:-/tmp}/pp-restart-session.json"
  local hook="${PP_HOOK_PATH:-}"
  local bin project cwd
  bin="$(_pp_bin)"
  project="${PP_PROJECT:-}"
  cwd="$PWD"
  [[ -z "$project" ]] && project="$("$bin" here --json 2>/dev/null)" || true
  project="${project//$'\n'/}"
  if ! command -v python3 >/dev/null 2>&1; then
    print -u2 -- "[pp] python3 required for pp restart"
    return 1
  fi
  PP_RESTART_SESSION="$session" \
  PP_RESTART_CWD="$cwd" \
  PP_RESTART_PROJECT="$project" \
  PP_RESTART_PROJECT_PATH="${PP_PROJECT_PATH:-$cwd}" \
  PP_RESTART_ENV_KEYS="${(j: :)_PP_ENV_KEYS}" \
  python3 - <<'PY'
import json, os
session = os.environ["PP_RESTART_SESSION"]
keys = [k for k in os.environ.get("PP_RESTART_ENV_KEYS", "").split() if k]
env_vars = {k: os.environ.get(k, "") for k in keys if k in os.environ}
data = {
    "version": 1,
    "cwd": os.environ.get("PP_RESTART_CWD", ""),
    "project": os.environ.get("PP_RESTART_PROJECT", ""),
    "project_path": os.environ.get("PP_RESTART_PROJECT_PATH", ""),
    "env_keys": keys,
    "env_vars": env_vars,
}
with open(session, "w", encoding="utf-8") as f:
    json.dump(data, f)
PY
  if [[ -z "$hook" || ! -f "$hook" ]]; then
    "$bin" restart
    return $?
  fi
  local initfile="${TMPDIR:-/tmp}/pp-restart-init.zsh"
  {
    print -r -- "source $(printf %q "$hook")"
    print -r -- "_pp_hook_restore $(printf %q "$session")"
  } >"$initfile"
  local boot="source $(printf %q "$initfile")"
  local term="${PP_TERMINAL:-}"
  if [[ -z "$term" ]]; then
    if osascript -e 'id of application "iTerm"' >/dev/null 2>&1; then term=iTerm
    else term=Terminal
    fi
  fi
  case "${term:l}" in
    iterm|iterm2)
      osascript <<OSA
tell application "iTerm"
  create window with default profile
  tell current session of current window
    write text "$boot"
  end tell
  activate
end tell
OSA
      ;;
    *)
      osascript <<OSA
tell application "Terminal"
  do script "$boot"
  activate
end tell
OSA
      ;;
  esac
  exit 0
}
)PPRESTART";

std::string hookProfileBlock() {
  std::ostringstream o;
  o << kHookBegin << "\n";
  o << "export PP_HOOK_PATH=" << shellSingleQuote(hookScriptPath().string()) << "\n";
  o << "export PP_BIN=" << shellSingleQuote((installDir() / "pp").string()) << "\n";
  o << "[[ -f \"$PP_HOOK_PATH\" ]] && source \"$PP_HOOK_PATH\"\n";
  o << "pp() {\n";
  o << "  source \"$PP_HOOK_PATH\"\n";
  o << "  pp_hook_dispatch \"$@\"\n";
  o << "}\n";
  o << kHookEnd << "\n";
  return o.str();
}

bool writeHookScript() {
  ensureDir(appDataDir());
  std::ofstream out(hookScriptPath());
  if (!out) return false;
  out << "# ProjectPlatform shell hook - auto-generated\n";
  out << "# PP_HOOK_VERSION=" << PP_APP_VERSION << "\n";
  out << "# Loaded via ~/.zshrc wrapper (re-sources this file each pp/precmd call).\n\n";
  const std::string body = hookBody();
  out << body;
  if (body.find("_pp_hook_restore") == std::string::npos) out << kRestartHelpersZsh;
  out << "\n";
  return static_cast<bool>(out);
}

bool stripHookFromZshrc() {
  const auto profile = zshrcPath();
  if (profile.empty() || !fs::exists(profile)) return true;
  std::ifstream in(profile);
  std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const auto pos = content.find(kHookBegin);
  if (pos == std::string::npos) return true;
  const auto end = content.find(kHookEnd, pos);
  if (end != std::string::npos) {
    size_t eraseEnd = end + std::char_traits<char>::length(kHookEnd);
    while (eraseEnd < content.size() && (content[eraseEnd] == '\r' || content[eraseEnd] == '\n'))
      ++eraseEnd;
    content.erase(pos, eraseEnd - pos);
  } else {
    content.erase(pos);
  }
  std::ofstream out(profile);
  if (!out) return false;
  out << content;
  return true;
}

bool appendHookToZshrc() {
  const auto profile = zshrcPath();
  if (profile.empty()) return false;

  std::string content;
  if (fs::exists(profile)) {
    std::ifstream in(profile);
    content.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  }
  if (content.find(kHookBegin) != std::string::npos) {
    // Refresh block in place.
    stripHookFromZshrc();
    content.clear();
    if (fs::exists(profile)) {
      std::ifstream in(profile);
      content.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
  }

  if (!content.empty() && content.back() != '\n') content += '\n';
  content += "\n";
  content += hookProfileBlock();

  ensureDir(profile.parent_path());
  std::ofstream out(profile);
  if (!out) return false;
  out << content;
  return true;
}

fs::path findBundledFishHookAsset() {
  const auto exe = platform::getExePath();
  if (exe.empty()) return {};
  fs::path p = exe.parent_path();
  for (int i = 0; i < 8; ++i) {
    const auto cand = p / "assets" / "pp-hook.fish";
    if (fs::exists(cand)) return cand;
    if (p == p.parent_path()) break;
    p = p.parent_path();
  }
  return {};
}

bool writeFishHookScript() {
  const auto asset = findBundledFishHookAsset();
  if (asset.empty()) return false;
  ensureDir(appDataDir());
  std::error_code ec;
  fs::copy_file(asset, fishHookScriptPath(), fs::copy_options::overwrite_existing, ec);
  return !ec;
}

std::string fishHookProfileBlock() {
  std::ostringstream o;
  o << kHookBegin << "\n";
  o << "set -gx PP_HOOK_PATH " << shellSingleQuote(fishHookScriptPath().string()) << "\n";
  o << "set -gx PP_BIN " << shellSingleQuote((installDir() / "pp").string()) << "\n";
  o << "test -f $PP_HOOK_PATH; and source $PP_HOOK_PATH\n";
  o << kHookEnd << "\n";
  return o.str();
}

bool stripHookFromFishConfig() {
  const auto profile = fishConfigPath();
  if (profile.empty() || !fs::exists(profile)) return true;
  std::ifstream in(profile);
  std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const auto pos = content.find(kHookBegin);
  if (pos == std::string::npos) return true;
  const auto end = content.find(kHookEnd, pos);
  if (end != std::string::npos) {
    size_t eraseEnd = end + std::char_traits<char>::length(kHookEnd);
    while (eraseEnd < content.size() && (content[eraseEnd] == '\r' || content[eraseEnd] == '\n'))
      ++eraseEnd;
    content.erase(pos, eraseEnd - pos);
  } else {
    content.erase(pos);
  }
  std::ofstream out(profile);
  if (!out) return false;
  out << content;
  return true;
}

bool appendHookToFishConfig() {
  if (!writeFishHookScript()) return false;
  const auto profile = fishConfigPath();
  if (profile.empty()) return false;

  std::string content;
  if (fs::exists(profile)) {
    std::ifstream in(profile);
    content.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  }
  if (content.find(kHookBegin) != std::string::npos) {
    stripHookFromFishConfig();
    content.clear();
    if (fs::exists(profile)) {
      std::ifstream in(profile);
      content.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
  }
  if (!content.empty() && content.back() != '\n') content += '\n';
  content += "\n";
  content += fishHookProfileBlock();
  ensureDir(profile.parent_path());
  std::ofstream out(profile);
  if (!out) return false;
  out << content;
  return true;
}

}  // namespace

bool installBinaryToPath(const fs::path& src, bool updatePath) {
  const auto destDir = installDir();
  ensureDir(destDir);
  const auto dest = destDir / "pp";

  std::error_code ec;
  if (fs::exists(dest, ec)) {
    const auto staging = destDir / "pp.staging";
    fs::copy_file(src, staging, fs::copy_options::overwrite_existing, ec);
    if (ec) {
      out::dim("copy failed: " + ec.message());
      return false;
    }
    fs::remove(dest, ec);
    fs::rename(staging, dest, ec);
    if (ec) {
      out::dim("replace failed: " + ec.message());
      return false;
    }
  } else {
    fs::copy_file(src, dest, fs::copy_options::overwrite_existing, ec);
    if (ec) {
      out::dim("copy failed: " + ec.message());
      return false;
    }
  }

  ::chmod(dest.c_str(), S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);

  if (updatePath) return addInstallDirToPath();
  return true;
}

bool replaceInstalledBinary(const fs::path& src, std::string& errorOut) {
  if (!installBinaryToPath(src, false)) {
    errorOut = "failed to copy binary";
    return false;
  }
  return true;
}

bool addInstallDirToPath() { return writeZprofilePathBlock(); }

bool installSelf() {
  Progress progress("install");
  const auto src = platform::getExePath();
  if (src.empty()) {
    out::error("could not locate executable");
    return false;
  }

  progress.step("copying to " + (installDir() / "pp").string());
  if (!installBinaryToPath(src, true)) {
    out::error("failed to install");
    return false;
  }

  installBundledPlugins();
  installBundledAutomations();
  installEnvTemplates();
  writeHookScript();

  progress.done(std::string("ProjectPlatform ") + PP_APP_VERSION + " installed to " +
                installDir().string());
  out::blank();
  out::info("Reload your shell, then run:  pp list");
  out::dim("  source ~/.zprofile");
  out::dim("  # or open a new terminal");
  out::blank();
  out::dim("Optional shell hook (pp cd / prompt):");
  out::dim("  pp hook install");
  return true;
}

bool uninstallSelf() {
  Progress progress("uninstall");

  progress.step("removing shell hook from ~/.zshrc");
  stripHookFromZshrc();

  progress.step("removing PATH from ~/.zprofile");
  removeZprofilePathBlock();

  progress.step("removing install directory");
  const auto data = appDataDir();
  std::error_code ec;
  if (fs::exists(data, ec)) fs::remove_all(data, ec);

  progress.done("ProjectPlatform uninstalled");
  out::dim("Open a new terminal (or: source ~/.zprofile && source ~/.zshrc)");
  return true;
}

bool refreshHookScript() {
  writeHookScript();
  return true;
}

bool ensureHookScriptFresh() {
  const auto path = hookScriptPath();
  const std::string verLine = std::string("# PP_HOOK_VERSION=") + PP_APP_VERSION;
  bool hookUpdated = false;

  if (!fs::exists(path)) {
    hookUpdated = writeHookScript();
  } else {
    std::ifstream in(path);
    const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (content.find("pp_hook_dispatch") == std::string::npos ||
        content.find("_pp_prompt_label") == std::string::npos ||
        content.find("_pp_normalize_path") == std::string::npos ||
        content.find("_pp_hook_restore") == std::string::npos ||
        content.find(verLine) == std::string::npos) {
      hookUpdated = writeHookScript();
    }
  }

  if (hookUpdated) {
    out::dim("[pp] Shell hook updated — reload this session: source ~/.zshrc");
  }
  return true;
}

bool installHook() {
  if (!writeHookScript()) {
    out::error("failed to write hook script");
    return false;
  }
  if (!appendHookToZshrc()) {
    out::error("failed to update ~/.zshrc");
    return false;
  }
  out::success("hook installed (zsh)");
  out::dim("Reload this session:");
  out::dim("  source ~/.zshrc");

  // Optional fish support when config dir already exists.
  if (!fishConfigPath().empty() && fs::exists(fishConfigPath().parent_path())) {
    if (appendHookToFishConfig()) {
      out::success("fish hook installed (~/.config/fish/config.fish)");
      out::dim("  source ~/.config/fish/config.fish");
    }
  }

  out::dim("Or open a new terminal.");
  return true;
}

bool uninstallHook() {
  stripHookFromZshrc();
  stripHookFromFishConfig();
  out::success("hook removed from shell configs");
  return true;
}

}  // namespace pp
