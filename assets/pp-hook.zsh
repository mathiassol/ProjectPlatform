# ProjectPlatform zsh hook (reference copy)
# Installed to ~/Library/Application Support/ProjectPlatform/pp-hook.zsh
# Loaded via ~/.zshrc wrapper — re-sources this file on each pp/precmd call.

# Prefer the real binary (avoid recursion with the `pp` shell function).
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

  # Capture current PP env keys into the session JSON.
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
    if osascript -e 'id of application "iTerm"' >/dev/null 2>&1; then
      term=iTerm
    else
      term=Terminal
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
    _pp_restart
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

# Register prompt hook once per shell.
if [[ -z ${_PP_ZSH_HOOK_READY:-} ]]; then
  typeset -g _PP_ZSH_HOOK_READY=1
  autoload -Uz add-zsh-hook 2>/dev/null || true
  if typeset -f add-zsh-hook >/dev/null 2>&1; then
    add-zsh-hook precmd pp_hook_prompt_precmd
  else
    # Fallback: chain into existing precmd
    if typeset -f precmd >/dev/null 2>&1; then
      functions[_pp_user_precmd]=$functions[precmd]
      precmd() { _pp_user_precmd; pp_hook_prompt_precmd; }
    else
      precmd() { pp_hook_prompt_precmd; }
    fi
  fi
fi
