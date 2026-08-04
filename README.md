# ProjectPlatform (PP)

Windows project manager CLI for `Documents/Projects`.

## Quick install

1. Download **ProjectPlatform-v1.0.0-win64.zip** from [Releases](https://github.com/mathiassol/ProjectPlatform/releases)
2. Extract the zip
3. **Double-click `Install.bat`**
4. Open a **new** terminal and run `pp list`

Shell hook is **off by default**. To enable in-terminal `pp cd`:

```powershell
pp hook install
```

Restart PowerShell after enabling the hook.

## Build from source

Requires [CPM](https://github.com/mathiassol/CPM) and MSVC.

```powershell
cpm build
.\build\Release\Release\pp.exe install
```

## Commands

Run `pp help` for the full list.

| Command | Description |
|---------|-------------|
| `pp list` | List projects |
| `pp cd <name>` | Jump to project (needs `pp hook install`) |
| `pp template add/create` | Manage templates in `Documents/Templates` |
| `pp script run <name>` | Run global or project scripts (`.ps1`, `.bat`) |
| `pp hook install` | Enable shell integration (optional) |

## Paths

- Projects: `%USERPROFILE%\Documents\Projects`
- Templates: `%USERPROFILE%\Documents\Templates`
- Binary: `%LOCALAPPDATA%\ProjectPlatform\bin\pp.exe`
