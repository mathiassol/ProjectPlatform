# ProjectPlatform fish hook (optional)
# Enabled by pp hook install when ~/.config/fish exists.

function __pp_bin
  if set -q PP_BIN; and test -x "$PP_BIN"
    printf '%s\n' $PP_BIN
    return
  end
  if set -q PP_HOOK_PATH
    set -l cand (dirname $PP_HOOK_PATH)/bin/pp
    if test -x $cand
      printf '%s\n' $cand
      return
    end
  end
  printf '%s\n' pp
end

function __pp_cd --argument-names name
  set -l bin (__pp_bin)
  set -l path ($bin cd $name --quiet 2>/dev/null)
  if test $status -ne 0
    $bin cd $name
    return 1
  end
  cd -- $path
  or return 1
  set -gx PP_PROJECT $name
  set -gx PP_PROJECT_PATH $path
  set -gx PP_PROJECT_ROOT $path
  echo "-> $path"
end

function __pp_env_from_json --argument-names mode
  set -l bin (__pp_bin)
  if not command -q python3
    echo "[pp] python3 required for env $mode in fish hook" >&2
    return 1
  end
  set -l raw ($bin env $argv --shell 2>/dev/null)
  or return 1
  echo $raw | python3 -c '
import json, sys, shlex
data = json.loads(sys.stdin.read())
if data.get("clear"):
    for k in data.get("keys") or []:
        print("set -e " + shlex.quote(str(k)))
else:
    for k, v in (data.get("vars") or {}).items():
        print("set -gx " + shlex.quote(str(k)) + " " + shlex.quote(str(v)))
' | source
end

function pp
  set -l bin (__pp_bin)
  if test (count $argv) -ge 1; and test "$argv[1]" = restart
    echo "[pp] Prefer zsh for full session restart; falling back to pp binary" >&2
    $bin $argv
    return $status
  end
  if test (count $argv) -ge 2
    switch $argv[1]
      case cd goto go enter
        __pp_cd $argv[2]
        return $status
      case env
        if contains -- $argv[2] apply clear load
          __pp_env_from_json $argv[2..]
          return $status
        end
    end
  end
  $bin $argv
end

function fish_prompt
  set -l bin (__pp_bin)
  set -l project ($bin here --json 2>/dev/null)
  if test -n "$project"
    printf 'PP:%s> ' $project
  else
    printf '%s> ' (prompt_pwd)
  end
end
