# Where to go next

**Audience:** anyone deciding what to do after the macOS Phases 0–5 work on `mac-demo`.  
**Status today:** Windows = primary. macOS = **CI-proven beta** (GHA `macos-e2e` green). Not merged to `main`, and no macOS release zip is published yet (latest GitHub release is still **v1.3.2 win64-only**).

## Done (don’t rebuild)

Phases **0–5** are implemented on `mac-demo`: platform seam, Darwin install/Keychain, zsh(+optional fish) hook, `pp update` client, plugins/autos via `pwsh`, restart/editor, universal release packaging, and `macos-e2e` on GitHub Actions.

## Reality check

| Layer | State |
|-------|--------|
| Code on `mac-demo` | Complete for the planned phases |
| GHA macOS VM | Core path verified |
| Your Mac | Not dogfooded yet |
| `main` + GitHub Releases | Still Windows-first; no `macos-universal` asset |

Treat macOS as shippable **after** a hardware pass + a tagged release that includes the universal zip.

## Recommended order

### 1. Hardware dogfood (when you have a Mac)

Run the same checklist CI covers, then the gaps CI can’t:

```bash
./install.sh && source ~/.zprofile
pp hook install && source ~/.zshrc
pp new demo && pp cd demo
pp env set TEST secret --global --secret && pp env get TEST --global --show-secrets
pp editor setup
pp restart                    # confirm Terminal/iTerm restore
brew install --cask powershell
pp ai path
pp auto doctor
```

Also try a **downloaded** zip (Gatekeeper quarantine) once a release exists.

### 2. Ship the branch

1. Open/merge PR: `mac-demo` → `main` (only when you’re ready to own macOS on the default branch).
2. Bump version if needed; tag `vX.Y.Z`.
3. Let `.github/workflows/release.yml` publish **win64 + macos-universal**.
4. On Mac: `pp update` must find and install that universal asset.

Until step 3, `pp update` on Mac can check GitHub but **cannot** self-install a Darwin binary.

### 3. Harden what users will hit first

Priority after dogfood bugs:

1. **`pp update` install** from a real release (download/extract/replace).
2. **zsh hook** edge cases (prompt/subfolder, env with weird characters, multi-shell).
3. **`pp ai` / `pp auto`** on Mac with Cursor Agent + Notion (document `pwsh` as required, or add thin `.sh` entrypoints).
4. **`pp restart`** Terminal vs iTerm reliability.

Keep Windows behavior unchanged; Darwin-only fixes stay under `src/platform/darwin/` and shell assets.

### 4. Later (not blocking a Mac beta)

- Native `.sh` plugins where `pwsh` is painful  
- Fish prompt parity with zsh  
- Linux via the same unix seam  
- Universal binary already in release CI — only revisit if `lipo`/signing becomes an issue  

## Decision guide

| Goal | Do this |
|------|---------|
| Keep experimenting safely | Stay on `mac-demo`; use Actions → **macos-e2e** |
| Use PP daily on a Mac | Dogfood → merge → cut release with universal zip |
| Fix a CI-only failure | Iterate on `scripts/ci/macos-e2e.sh` + push `mac-demo` |
| Fix a laptop-only failure | Capture repro; prefer Darwin-local changes + extend e2e if automatable |

## Edge cases

**GHA green ≠ laptop green** — Keychain prompts, Gatekeeper, Homebrew paths, and GUI Terminal/`osascript` differ from the runner.

**No macOS asset yet** — `pp update --check` can succeed while `pp update` still errors with “no macOS asset”; that means publish the universal zip, not rewrite the updater.

**Don’t merge half-shipped** — Merging `mac-demo` to `main` without a Darwin release leaves Mac users on source-build/`install.sh` only.

## Links

- Platform details: [platforms/README.md](platforms/README.md)  
- Automations: [automations/README.md](automations/README.md)  
- Branch: `mac-demo` · Workflow: `macos-e2e`
