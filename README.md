# ProjectPlatform (PP)

Cross-platform project manager CLI for `Documents/Projects` (Windows primary; macOS supported through Phase 3).

See [docs/platforms/README.md](docs/platforms/README.md) for the Mac status matrix, and [docs/platforms/WHERE-TO-GO.md](docs/platforms/WHERE-TO-GO.md) for the next-step roadmap.

## Quick install

### Windows

1. Download **ProjectPlatform-v\*-win64.zip** from [Releases](https://github.com/mathiassol/ProjectPlatform/releases)
2. Extract the zip
3. **Double-click `Install.bat`**
4. Open a **new** terminal and run `pp list`

Shell hook is **off by default**. To enable in-terminal `pp cd`:

```powershell
pp hook install
```

### macOS (universal)

1. Download **ProjectPlatform-v\*-macos-universal.zip** from [Releases](https://github.com/mathiassol/ProjectPlatform/releases)
2. Extract and run `./install.sh`
3. `source ~/.zprofile` then `pp list`
4. Optional: `pp hook install && source ~/.zshrc`
5. Optional: `pp editor setup` (Zed)

## Build from source

### Windows

Requires [CPM](https://github.com/mathiassol/CPM) and MSVC.

```powershell
cpm build
.\build\Release\pp.exe install
```

### macOS

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
./build/pp install
```

## Commands

Run `pp help` for the full list.

```
pp update          # check GitHub and install latest release
pp update --check  # check only, don't install
```

| Command | Description |
|---------|-------------|
| `pp install` | Install to user AppData / Application Support and PATH |
| `pp uninstall` | Remove pp from PATH and AppData (projects/templates kept) |
| `pp update` | Install latest release from GitHub |
| `pp list` | List projects |
| `pp cd <name>` | Jump to project (needs `pp hook install`) |
| `pp template add/create` | Manage templates in `Documents/Templates` |
| `pp script run <name>` | Run global or project scripts |
| `pp hook install` | Enable shell integration (optional) |

## Paths

| | Windows | macOS |
|--|---------|-------|
| Projects | `%USERPROFILE%\Documents\Projects` | `~/Documents/Projects` |
| Binary | `%LOCALAPPDATA%\ProjectPlatform\bin\pp.exe` | `~/Library/Application Support/ProjectPlatform/bin/pp` |
