# PP Automations



Native ProjectPlatform automations run AI tasks via the **Cursor Agent CLI** (`agent`), with isolated workspaces, safety gates, and QoL commands.



**Fully independent of the pp ai plugin** — works offline with `pp auto init` / `pp auto prompt`.



## Layout



```

docs/automations/           # documentation per automation type

  ai-data-backlog-bot/

assets/automations/         # bundled templates (installed by pp install)

  engine/                   # shared PowerShell engine

  <automation-id>/          # manifest + prompts + templates

```



Workspaces live at `Documents/Automations/<id>/` (not in the repo).



## Commands



```powershell

pp auto list

pp auto doctor

pp auto init <id>               # bootstrap workspace (no agent, offline)

pp auto setup <id> [--no-agent] # optional agent README pass

pp auto run <id> [--tasks N]    # run task via Cursor agent

pp auto prompt <id> [--setup]   # write prompt file (offline)

pp auto status [id]

pp auto upload <id>             # interactive branch push (type confirm)

pp auto goto <id>               # print workspace path

pp auto explore <id>            # open in Explorer

pp auto logs [id]

pp auto open <id>

pp auto reset <id> [--force]    # clear state.json only

```



## Safety



- Dry-run: `--dry-run` writes prompt to `runs/` without invoking agent

- Single confirm before agent (use `--force` / `-y` to skip)

- Never auto-push `main`/`master`

- Upload script requires typing **confirm**

- `max_tasks_per_run` cap per automation manifest



## Adding an automation



1. Create `assets/automations/<id>/automation.json`

2. Add `setup.prompt.md` and `task.prompt.md`

3. Run `pp install` to copy bundled files

4. Add docs under `docs/automations/<id>/`

