# Setup: AI-DATA backlog BOT workspace

You are setting up a **ProjectPlatform automation workspace** for the first time.
The **task automation** (what runs on `pp auto run`) is described in the task context above - read it fully so setup matches runtime needs.

## Create this folder structure under the workspace root

```
repos/                 # one git clone per backlog task (isolated)
completed/             # JSON metadata per finished task
runs/                  # (already used by PP engine for logs)
README.md              # human + AI orientation (update/expand)
upload-task.ps1        # interactive upload script (see below)
.gitignore             # ignore repos/*/.env* secrets, node_modules, etc.
```

## upload-task.ps1 (required)

Create `upload-task.ps1` in the workspace root that:

1. Scans `repos/` for task folders that have a completed feature branch with commits
2. Also reads `completed/*.json` for task metadata (title, branch, repo path, notion id)
3. Lists numbered completed tasks for the user to pick ONE
4. Shows branch name + changed files summary
5. Asks user to type **confirm** (exact word) before any git push
6. **Never** pushes to `main` or `master` - refuse protected branch names
7. Runs `git push -u origin <branch>` only after confirm
8. Prints GitHub compare URL for manual PR creation
9. On abort: no push, no destructive git operations

Use clear `[auto]` prefixed Write-Host messages and Read-Host prompts.

## README.md

Document:
- What this automation does (Notion backlog -> isolated repos -> Cursor tasks)
- Folder layout
- PP commands: `pp auto run ai-data-backlog-bot`, `pp auto upload ai-data-backlog-bot`, `pp auto status`
- Safety: no auto-push to main, upload requires typing confirm
- Notion MCP must be connected in Cursor for run mode

## .gitignore

Ignore:
- repos/**/node_modules
- repos/**/.env*
- repos/**/build
- repos/**/.gradle
- secrets/

## Do NOT

- Push anything to GitHub during setup
- Store Notion tokens or credentials in this workspace
- Modify files outside the workspace root

When finished, summarize what you created.
