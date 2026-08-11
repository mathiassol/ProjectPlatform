# Platforms

ProjectPlatform is cross-platform. **Windows remains the primary, fully supported OS.**
macOS Phase 5 adds restart, editor setup, optional fish hook, and universal binaries.

## Status

| Platform | Status | Notes |
|----------|--------|--------|
| Windows | Primary | Full feature set |
| macOS | Phase 5 polish | Core + hook + update + plugins/auto + restart/editor/universal |
| Linux | Not planned yet | Same unix seam as Darwin later |

## Architecture

```
src/platform/platform.hpp     # compile-time OS seam (no virtuals on hot paths)
src/platform/win/             # DPAPI, WinHTTP, ShellExecute, …
src/platform/darwin/          # Keychain, install, zsh/fish hook, update, restart, editor
```

## macOS quick start

```bash
# From release (universal arm64+x86_64)
./install.sh && source ~/.zprofile
pp hook install && source ~/.zshrc
pp editor setup                 # Zed; optional: brew install duti
brew install --cask powershell  # for pp ai / pp auto
```

```bash
pp restart                      # new Terminal/iTerm + restore cwd/project/env (zsh)
# PP_TERMINAL=Terminal|iTerm to force terminal app
```

## Feature matrix

| Feature | macOS |
|---------|--------|
| Core CLI / env / `.sh` scripts | Yes |
| zsh hook (`pp cd`, prompt, env) | Yes |
| fish hook (optional) | Yes (if `~/.config/fish` exists) |
| `pp update` | Yes (prefers `macos-universal` zip) |
| `pp ai` / `pp auto` | Yes via `pwsh` |
| `pp restart` | Yes (Terminal.app / iTerm) |
| `pp editor setup` | Yes (Zed + optional duti) |
| Universal binary | Yes (release CI) |

## Release assets

| Asset | Contents |
|-------|----------|
| `ProjectPlatform-vX.Y.Z-win64.zip` | Windows |
| `ProjectPlatform-vX.Y.Z-macos-universal.zip` | macOS arm64 + x86_64 |

## Plugin manifest

```json
{
  "platforms": ["win", "darwin"],
  "commands": { "dev": "dev.ps1" },
  "commands_darwin": { "dev": "dev.sh" }
}
```

## Roadmap

Phases **0–5** are implemented on `mac-demo` (CI-proven beta). **What to do next:** [WHERE-TO-GO.md](WHERE-TO-GO.md).

## Windows performance rules

- No runtime `if (mac)` on hot paths
- Windows keeps DPAPI / WinHTTP / Registry PATH / PowerShell hook
- Platform calls are free functions selected at compile time
