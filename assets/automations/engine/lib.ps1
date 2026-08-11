# PP automation engine - shared helpers (no plugin dependencies)

function Write-PpAutoMsg {
    param([string]$Text, [string]$Color = 'Gray')
    if ($env:PP_QUIET -eq '1') { return }
    Write-Host $Text -ForegroundColor $Color
}

function Test-PpIsWindows {
    if ($null -ne (Get-Variable -Name IsWindows -ErrorAction SilentlyContinue)) {
        return [bool]$IsWindows
    }
    return ($env:OS -match 'Windows')
}

function Get-PpAppDataRoot {
    if ($env:PP_APP_DATA -and (Test-Path -LiteralPath $env:PP_APP_DATA)) {
        return $env:PP_APP_DATA
    }
    if ($env:LOCALAPPDATA) {
        return (Join-Path $env:LOCALAPPDATA 'ProjectPlatform')
    }
    $home = if ($env:HOME) { $env:HOME } elseif ($env:USERPROFILE) { $env:USERPROFILE } else { $null }
    if (-not $home) { throw 'Could not resolve ProjectPlatform app data root' }
    return (Join-Path $home 'Library/Application Support/ProjectPlatform')
}

function Confirm-PpAutoAction {
    param(
        [Parameter(Mandatory = $true)][string]$Message,
        [string]$DefaultNo = $true
    )
    if ($env:PP_FORCE -eq '1' -or $env:PP_DRY_RUN -eq '1') { return $true }
    Write-PpAutoMsg $Message 'Cyan'
    $a = Read-Host '  Continue? [y/N]'
    return ($a -match '^[yY]')
}

function Get-PpAutoBundledDir {
    Join-Path (Get-PpAppDataRoot) 'automations/bundled'
}

function Get-PpAutoBundleRoot {
    param([string]$Id)
    if ($env:PP_AUTO_BUNDLE -and (Test-Path -LiteralPath (Join-Path $env:PP_AUTO_BUNDLE 'automation.json'))) {
        return $env:PP_AUTO_BUNDLE
    }
    if ($Id) { return Join-Path (Get-PpAutoBundledDir) $Id }
    return Get-PpAutoBundledDir
}

function Get-PpAutoWorkspaceRoot {
    param([string]$Id)
    if ($env:PP_AUTO_WORKSPACE) { return $env:PP_AUTO_WORKSPACE }
    $docs = [Environment]::GetFolderPath('MyDocuments')
    Join-Path $docs "Automations\$Id"
}

function Get-PpAutoManifest {
    param([string]$Id, [string]$BundleRoot)
    $path = Join-Path $BundleRoot 'automation.json'
    if (-not (Test-Path -LiteralPath $path)) { throw "automation.json not found: $path" }
    return Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
}

function Read-PpAutoPromptFile {
    param([string]$BundleRoot, [string]$FileName)
    $path = Join-Path $BundleRoot $FileName
    if (-not (Test-Path -LiteralPath $path)) { return '' }
    return Get-Content -LiteralPath $path -Raw
}

function Get-PpAutoCompiledPrompt {
    param(
        [string]$Kind,
        [string]$Id,
        [string]$BundleRoot,
        [hashtable]$Vars = @{}
    )

    $manifest = Get-PpAutoManifest -Id $Id -BundleRoot $BundleRoot
    $setupFile = if ($manifest.setup_prompt_file) { $manifest.setup_prompt_file } else { 'setup.prompt.md' }
    $taskFile = if ($manifest.task_prompt_file) { $manifest.task_prompt_file } else { 'task.prompt.md' }

    $taskText = Read-PpAutoPromptFile -BundleRoot $BundleRoot -FileName $taskFile
    $body = if ($Kind -eq 'setup') {
        Read-PpAutoPromptFile -BundleRoot $BundleRoot -FileName $setupFile
    } else {
        $taskText
    }

    $header = @"
# PP Automation: $($manifest.name) ($Id)
# Mode: $Kind
# Workspace: $(Get-PpAutoWorkspaceRoot -Id $Id)

You are running inside a ProjectPlatform (PP) automation workspace.
This system is independent of PP plugins (pp ai). Follow safety rules in README.md.
Never push to main/master without the human upload script (type confirm).

## Task automation context (read this for setup too)
$taskText

---

## Your instructions ($Kind)
"@

    $compiled = $header + "`n`n" + $body
    foreach ($key in $Vars.Keys) {
        $compiled = $compiled -replace [regex]::Escape("{{$key}}"), [string]$Vars[$key]
    }
    return $compiled.Trim()
}

function Get-PpAutoState {
    param([string]$Workspace)
    $path = Join-Path $Workspace 'state.json'
    $defaults = @{
        setup_complete  = $false
        runs            = @()
        completed_tasks = @()
    }
    if (-not (Test-Path -LiteralPath $path)) { return @{} + $defaults }
    try {
        $raw = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
        $state = @{}
        foreach ($p in $raw.PSObject.Properties) { $state[$p.Name] = $p.Value }
        foreach ($k in $defaults.Keys) {
            if (-not $state.ContainsKey($k)) { $state[$k] = $defaults[$k] }
        }
        return $state
    } catch {
        return @{} + $defaults
    }
}

function Set-PpAutoState {
    param([string]$Workspace, [hashtable]$State)
    $path = Join-Path $Workspace 'state.json'
    $State | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $path -Encoding UTF8
}

function New-PpAutoRunLogDir {
    param([string]$Workspace)
    $runs = Join-Path $Workspace 'runs'
    if (-not (Test-Path $runs)) { New-Item -ItemType Directory -Path $runs -Force | Out-Null }
    $dir = Join-Path $runs (Get-Date -Format 'yyyyMMdd-HHmmss')
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
    return $dir
}

function Test-PpAutoWorkspaceReady {
    param([string]$Workspace)
    foreach ($item in @('repos', 'completed', 'upload-task.ps1')) {
        if (-not (Test-Path -LiteralPath (Join-Path $Workspace $item))) { return $false }
    }
    return $true
}

function Sync-PpAutoSetupState {
    param([string]$Workspace)
    $state = Get-PpAutoState -Workspace $Workspace
    $ready = Test-PpAutoWorkspaceReady -Workspace $Workspace
    if ($ready -and -not $state['setup_complete']) {
        $state['setup_complete'] = $true
        if (-not $state['last_setup']) {
            $state['last_setup'] = (Get-Date).ToString('o')
        }
        Set-PpAutoState -Workspace $Workspace -State $state
    }
    return $ready
}

function Get-PpAutoTaskCount {
    param($Manifest)
    $taskCount = 3
    if ($Manifest.defaults -and $Manifest.defaults.task_count) {
        $taskCount = [int]$Manifest.defaults.task_count
    }
    if ($env:PP_AUTO_TASK_COUNT) {
        $taskCount = [int]$env:PP_AUTO_TASK_COUNT
    }
    if ($Manifest.safety -and $Manifest.safety.max_tasks_per_run) {
        $max = [int]$Manifest.safety.max_tasks_per_run
        if ($taskCount -gt $max) {
            Write-PpAutoMsg "[auto] Capping tasks at $max (manifest max_tasks_per_run)" 'Yellow'
            $taskCount = $max
        }
    }
    if ($taskCount -lt 1) { $taskCount = 1 }
    return $taskCount
}

function Initialize-PpAutoWorkspace {
    param($Ctx)

    foreach ($sub in @('repos', 'completed', 'runs')) {
        $p = Join-Path $Ctx.Workspace $sub
        if (-not (Test-Path $p)) {
            New-Item -ItemType Directory -Path $p -Force | Out-Null
            Write-PpAutoMsg "[auto] Created $sub/" 'DarkGray'
        }
    }

    $templates = Join-Path $Ctx.Bundle 'templates'
    $copies = @(
        @{ Src = 'upload-task.ps1'; Dest = 'upload-task.ps1' }
        @{ Src = 'gitignore'; Dest = '.gitignore' }
    )
    foreach ($c in $copies) {
        $src = Join-Path $templates $c.Src
        $dest = Join-Path $Ctx.Workspace $c.Dest
        if ((Test-Path -LiteralPath $src) -and -not (Test-Path -LiteralPath $dest)) {
            Copy-Item -LiteralPath $src -Destination $dest -Force
            Write-PpAutoMsg "[auto] Installed $($c.Dest)" 'DarkGray'
        }
    }

    Write-PpAutoReadmeIfMissing -Workspace $Ctx.Workspace -Name $Ctx.Manifest.name -Id $Ctx.Id
    return (Test-PpAutoWorkspaceReady -Workspace $Ctx.Workspace)
}

function Write-PpAutoReadmeIfMissing {
    param([string]$Workspace, [string]$Name, [string]$Id)

    $readme = Join-Path $Workspace 'README.md'
    if (Test-Path -LiteralPath $readme) { return }

    @"
# $Name - PP Automation Workspace

Managed by **pp auto** (independent of pp ai plugin). Do not commit secrets here.

## Layout

| Path | Purpose |
|------|---------|
| ``repos/`` | Isolated git clone per task |
| ``completed/`` | Task metadata JSON |
| ``runs/`` | Agent prompts + logs |
| ``upload-task.ps1`` | Push branch (type ``confirm``) |

## Commands

``````powershell
pp auto status $Id
pp auto run $Id --tasks 3
pp auto upload $Id
pp auto prompt $Id --run      # preview prompt without agent
``````

## Safety

- Never push ``main``/``master``
- Upload requires typing ``confirm``
- Each task uses its own repo folder + feature branch
"@ | Set-Content -LiteralPath $readme -Encoding UTF8
}

function Get-PpAutoRunVars {
    param($Ctx, [int]$TaskCount)
    return @{
        TASK_COUNT  = [string]$TaskCount
        REPO_URL    = [string]$Ctx.Manifest.defaults.repo_url
        BASE_BRANCH = [string]$Ctx.Manifest.defaults.base_branch
        WORKSPACE   = $Ctx.Workspace
    }
}

function Write-PpAutoNextSteps {
    param([string]$Id, [string]$Phase)
    if ($env:PP_QUIET -eq '1') { return }
    switch ($Phase) {
        'init'   { Write-Host "  Next: pp auto setup $Id  (optional agent README) or pp auto run $Id" -ForegroundColor DarkGray }
        'setup'  { Write-Host "  Next: pp auto run $Id --tasks 3" -ForegroundColor DarkGray }
        'run'    { Write-Host "  Next: pp auto status $Id  |  pp auto upload $Id" -ForegroundColor DarkGray }
    }
}
