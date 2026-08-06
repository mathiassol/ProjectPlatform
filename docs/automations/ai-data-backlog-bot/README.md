# AI-DATA backlog BOT

Notion backlog → isolated git clones → Cursor agent implements safe codebase-only tasks.

## Prerequisites

- Notion connected in Cursor (MCP)
- Cursor Agent CLI (`agent`) on PATH — [Cursor CLI docs](https://cursor.com/docs/cli)
- `git`, GitHub access to `Enterprise-Plus/ai-data-monorepo`
- Optional: `pp env edit ai-data --global` for repo credentials

## Quick start

```powershell
pp install
pp auto doctor
pp auto setup ai-data-backlog-bot
pp auto run ai-data-backlog-bot --tasks 3
pp auto status ai-data-backlog-bot
pp auto upload ai-data-backlog-bot   # pick task, type confirm to push
```

## Workspace layout

After setup (`Documents/Automations/ai-data-backlog-bot/`):

| Path | Purpose |
|------|---------|
| `repos/` | One fresh clone per backlog task |
| `completed/` | JSON metadata per task (branch, notion id, status) |
| `runs/` | Agent prompts + logs per run |
| `upload-task.ps1` | Pick completed task, push branch after typing `confirm` |

## Task flow

1. Agent reads Notion **AI & Data / backlog issues**
2. Filters code-related / codebase-only issues
3. Sorts by criticalness, picks easiest **N** safe tasks (`--tasks` or default 3)
4. Per task: fresh clone, feature branch, implement, commit, write `completed/*.json`
5. **No push** during run — human uploads via `pp auto upload`

## Safety

- Skips tasks that are not 100% safe for AI (auth changes, prod deploys, etc.)
- Max 5 tasks per run (manifest cap)
- Upload refuses `main`/`master`
