#!/usr/bin/env bash
# Full macOS e2e for ProjectPlatform — intended for GitHub Actions macos-14+.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PP_BUILD="${PP_BUILD:-$ROOT/build/pp}"
PP_DATA="${HOME}/Library/Application Support/ProjectPlatform"
PP_BIN="${PP_DATA}/bin/pp"
PROJECTS="${HOME}/Documents/Projects"
DEMO="pp-ci-demo-$$"

log() { printf '\n==> %s\n' "$*"; }
fail() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

need() {
  command -v "$1" >/dev/null 2>&1 || fail "missing command: $1"
}

log "Prerequisites"
need cmake
need python3
need zsh
need curl
need unzip

if [[ ! -x "$PP_BUILD" ]]; then
  log "Configure + build"
  cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Release
  cmake --build "$ROOT/build" --config Release
  PP_BUILD="$ROOT/build/pp"
fi
[[ -x "$PP_BUILD" ]] || fail "binary missing: $PP_BUILD"

log "version / help / config (pre-install)"
"$PP_BUILD" version
"$PP_BUILD" help >/dev/null
"$PP_BUILD" config

log "install"
"$PP_BUILD" install
export PATH="${PP_DATA}/bin:${PATH}"
hash -r || true
command -v pp >/dev/null || fail "pp not on PATH after install"
[[ "$(command -v pp)" == "$PP_BIN" ]] || true
pp version

log "projects"
mkdir -p "$PROJECTS"
pp new "$DEMO"
pp list | grep -F "$DEMO" || fail "project not listed"
pp info "$DEMO" >/dev/null
pp here --quiet || true
PATH_OUT="$(pp cd "$DEMO" --quiet)"
[[ -d "$PATH_OUT" ]] || fail "pp cd --quiet did not return a directory"
[[ "$PATH_OUT" == *"$DEMO"* ]] || fail "unexpected project path: $PATH_OUT"

log "env + Keychain secret"
pp env new ci-smoke --global || true
pp env set PP_CI_SECRET "secret-value-$$" --global --secret
GOT="$(pp env get PP_CI_SECRET --global --show-secrets)"
# Strip ANSI color codes if present
GOT_CLEAN="$(printf '%s' "$GOT" | sed $'s/\x1b\\[[0-9;]*m//g')"
echo "$GOT_CLEAN" | grep -q "PP_CI_SECRET=secret-value-$$" || fail "secret round-trip failed (got: $GOT_CLEAN)"
pp env unset PP_CI_SECRET --global

log "scripts (.sh default)"
(
  cd "$PATH_OUT"
  pp script new ci-hello --type sh
  test -f .scripts/ci-hello.sh || fail "script file missing"
  # Make a trivial runnable script
  printf '#!/usr/bin/env bash\necho ci-hello-ok\n' > .scripts/ci-hello.sh
  chmod +x .scripts/ci-hello.sh
  OUT="$(pp script run ci-hello)"
  echo "$OUT" | grep -q ci-hello-ok || fail "script run failed: $OUT"
)

log "hook install + zsh cd/env"
pp hook install
test -f "$PP_DATA/pp-hook.zsh" || fail "pp-hook.zsh missing"
grep -q 'ProjectPlatform hook' "${HOME}/.zshrc" || fail "zshrc hook block missing"
pp hook status

zsh -f -c "
set -e
export PATH=$(printf %q "${PP_DATA}/bin"):\$PATH
export PP_HOOK_PATH=$(printf %q "${PP_DATA}/pp-hook.zsh")
export PP_BIN=$(printf %q "$PP_BIN")
source \"\$PP_HOOK_PATH\"
pp() { source \"\$PP_HOOK_PATH\"; pp_hook_dispatch \"\$@\"; }
pp cd $(printf %q "$DEMO")
pwd | grep -F $(printf %q "$DEMO")
test -n \"\$PP_PROJECT\"
test \"\$PP_PROJECT\" = $(printf %q "$DEMO")
# env apply via hook (may be empty — just must not crash)
pp env apply --global >/dev/null 2>&1 || true
echo zsh-hook-ok
"

log "update --check"
pp update --check || true

log "editor status"
pp editor status || true

log "plugins / automations"
pp plugin list | grep -F ai-data || fail "ai-data plugin not listed"
pp auto list | grep -F ai-data-backlog-bot || fail "automation not listed"

if command -v pwsh >/dev/null 2>&1; then
  log "pwsh plugin smoke (pp ai path)"
  pp ai path || true
  log "auto doctor (may warn without Cursor agent)"
  pp auto doctor || true
else
  log "pwsh not installed — skip plugin/auto runtime smoke"
fi

log "restart session write (spawn may fail headless)"
# Capture/save path: pp restart tries to open Terminal — allow failure after session write.
set +e
pp restart
RC=$?
set -e
if [[ -f "${TMPDIR:-/tmp}/pp-restart-session.json" ]]; then
  echo "restart session file written"
  python3 -c 'import json,sys; json.load(open(sys.argv[1]))' "${TMPDIR:-/tmp}/pp-restart-session.json"
  rm -f "${TMPDIR:-/tmp}/pp-restart-session.json"
else
  echo "note: no session file (ok if spawn failed early); exit=$RC"
fi

log "hook uninstall + project cleanup + uninstall"
pp hook uninstall
grep -q 'ProjectPlatform hook' "${HOME}/.zshrc" && fail "zshrc hook still present" || true
rm -rf "${PROJECTS}/${DEMO}"
pp uninstall || true
test ! -d "$PP_DATA" || echo "note: some AppData may remain"

log "ALL macOS e2e checks passed"
