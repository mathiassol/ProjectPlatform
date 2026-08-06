# Task: AI-DATA backlog BOT

Title: **AI-DATA backlog BOT**

## Goal

Process Notion backlog issues into isolated local git workspaces and implement safe, codebase-only changes using the Cursor agent workflow.

## Parameters (from PP)

- Task count this run: **{{TASK_COUNT}}**
- Upstream repo: **{{REPO_URL}}**
- Base branch: **{{BASE_BRANCH}}**
- Workspace root: **{{WORKSPACE}}**

## Prerequisites

- **Notion MCP** is connected in Cursor (user has Notion integrated)
- **git** and network access to GitHub
- Read `README.md` in the workspace before starting

## Workflow

### 1. Fresh clone baseline

For each selected task you will work in `repos/<task-slug>/` - a **fresh clone** from GitHub `{{BASE_BRANCH}}`.
Do not reuse dirty repos between tasks.

### 2. Notion backlog

- Connect via Notion MCP
- Navigate to: **AI & Data / backlog issues**
- Fetch issues that are:
  - **Code related** / **codebase-only changes** (no infra-only, no pure docs unless explicitly code-accompanied)
  - Not already marked done/shipped
- Sort by **criticalness** (most critical first for awareness) but **select tasks to implement** starting from the **easiest to complete** among safe candidates
- Take **{{TASK_COUNT}}** tasks this run

### 3. Safety gate (mandatory per task)

Before implementing, decide if the task is **100% safe for AI**:
- Safe: localized code changes, clear acceptance criteria, no prod deploys, no secret rotation, no billing/legal
- NOT safe: ambiguous requirements, cross-repo migrations, auth model changes, data deletion, production config

If NOT safe: write `completed/<slug>.json` with `"status": "skipped", "reason": "..."` and move on.

### 4. Per-task isolated repo

For each approved task:

1. Create folder `repos/<notion-slug-or-id>/`
2. `git clone --branch {{BASE_BRANCH}} --single-branch {{REPO_URL}} repos/<slug>/`
3. Create descriptive feature branch: `<github-user>/<date>-<short-slug>` (never `main`/`master`)
4. Implement the task with minimal scope
5. Commit with message referencing Notion title/id
6. Write `completed/<slug>.json`:

```json
{
  "notion_id": "...",
  "title": "...",
  "repo_path": "repos/...",
  "branch": "user/20260806-slug",
  "status": "completed",
  "safe": true,
  "summary": "what changed"
}
```

### 5. Do NOT push

- **Never push to GitHub** in this run
- Upload is human-controlled via `pp auto upload ai-data-backlog-bot` -> `upload-task.ps1` -> type `confirm`

### 6. PP env (optional — not pp ai plugin)

If clones need credentials, the human can use `pp env edit ai-data --global` (PP global profile).
This is optional and independent of the pp ai plugin. Do not create `.envrc` in clones.

## Output

When done, print a summary table: task title, repo folder, branch, status (completed/skipped/failed).
