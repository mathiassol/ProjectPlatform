param(
    [Parameter(Mandatory = $true)][string]$Mode,
    [string]$Id = $env:PP_AUTO_ID,
    [string[]]$Extra = @()
)

$ErrorActionPreference = 'Stop'
$EngineRoot = $PSScriptRoot
. (Join-Path $EngineRoot 'lib.ps1')
. (Join-Path $EngineRoot 'cursor.ps1')

function Resolve-PpAutomationContext {
    param([string]$AutomationId)

    if ([string]::IsNullOrWhiteSpace($AutomationId)) {
        throw 'Automation id required'
    }

    $bundleRoot = Get-PpAutoBundleRoot -Id $AutomationId
    if (-not (Test-Path -LiteralPath $bundleRoot)) {
        throw "Automation bundle not found: $AutomationId (run pp install)"
    }

    $workspace = Get-PpAutoWorkspaceRoot -Id $AutomationId
    $manifest = Get-PpAutoManifest -Id $AutomationId -BundleRoot $bundleRoot

    if (-not (Test-Path $workspace)) {
        New-Item -ItemType Directory -Path $workspace -Force | Out-Null
    }

    return @{
        Id        = $AutomationId
        Bundle    = $bundleRoot
        Workspace = $workspace
        Manifest  = $manifest
    }
}

function Invoke-PpAutoDoctor {
    param($Ctx)

    Write-Host 'PP automation doctor' -ForegroundColor Cyan
    $check = Test-PpAutoPrerequisites
    foreach ($i in $check.Issues) {
        Write-Host "  !! $i" -ForegroundColor Yellow
    }
    if ($check.Ok) {
        Write-Host '  OK prerequisites for agent runs' -ForegroundColor Green
        if ($check.Agent) { Write-Host "  agent:      $($check.Agent)" -ForegroundColor DarkGray }
    } else {
        Write-Host '  Note: pp auto init / prompt / upload work without agent CLI' -ForegroundColor DarkGray
    }

    $engine = Join-Path $EngineRoot 'engine.ps1'
    Write-Host ''
    Write-Host "  engine:     $engine $(if (Test-Path $engine) { '' } else { '(MISSING - run pp install)' })" -ForegroundColor DarkGray
    Write-Host "  workspaces: $(Split-Path (Get-PpAutoWorkspaceRoot -Id 'x') -Parent)" -ForegroundColor DarkGray
    Write-Host "  bundled:    $(Get-PpAutoBundledDir)" -ForegroundColor DarkGray

    if ($Ctx) {
        $ready = Sync-PpAutoSetupState -Workspace $Ctx.Workspace
        Write-Host "  workspace:  $($Ctx.Workspace)" -ForegroundColor DarkGray
        Write-Host "  bootstrap:  $(if ($ready) { 'ready' } else { 'run pp auto init ' + $Ctx.Id })" -ForegroundColor DarkGray
    }
}

function Invoke-PpAutoStatus {
    param($Ctx)

    $ready = Sync-PpAutoSetupState -Workspace $Ctx.Workspace
    $state = Get-PpAutoState -Workspace $Ctx.Workspace

    Write-Host "$($Ctx.Manifest.name) [$($Ctx.Id)]" -ForegroundColor Cyan
    Write-Host "  workspace: $($Ctx.Workspace)"
    Write-Host "  bootstrap: $(if ($ready) { 'ready' } else { 'NOT READY - pp auto init ' + $Ctx.Id })"

    if ($state['last_run']) { Write-Host "  last run:  $($state['last_run'])" -ForegroundColor DarkGray }

    $reposDir = Join-Path $Ctx.Workspace 'repos'
    if (Test-Path $reposDir) {
        $repos = @(Get-ChildItem $reposDir -Directory -ErrorAction SilentlyContinue)
        Write-Host "  repos:     $($repos.Count) task folder(s)" -ForegroundColor Gray
        foreach ($r in $repos | Select-Object -First 8) { Write-Host "    - $($r.Name)" -ForegroundColor DarkGray }
        if ($repos.Count -gt 8) { Write-Host "    ... +$($repos.Count - 8) more" -ForegroundColor DarkGray }
    }

    $completedDir = Join-Path $Ctx.Workspace 'completed'
    if (Test-Path $completedDir) {
        $done = @(Get-ChildItem $completedDir -Filter '*.json' -ErrorAction SilentlyContinue)
        $uploadable = 0
        foreach ($j in $done) {
            try {
                $t = Get-Content $j.FullName -Raw | ConvertFrom-Json
                if ($t.status -eq 'completed') { $uploadable++ }
            } catch { }
        }
        Write-Host "  completed: $($done.Count) total, $uploadable ready to upload" -ForegroundColor Green
    }

    $runsDir = Join-Path $Ctx.Workspace 'runs'
    if (Test-Path $runsDir) {
        $runs = Get-ChildItem $runsDir -Directory | Sort-Object Name -Descending | Select-Object -First 3
        if ($runs) {
            Write-Host '  recent runs:' -ForegroundColor DarkGray
            foreach ($run in $runs) { Write-Host "    $($run.Name)" -ForegroundColor DarkGray }
        }
    }

    if ($ready) { Write-PpAutoNextSteps -Id $Ctx.Id -Phase 'run' }
}

function Invoke-PpAutoStatusAll {
    $bundleBase = Get-PpAutoBundledDir
    if (-not (Test-Path $bundleBase)) {
        Write-Host 'No automations installed. Run: pp install' -ForegroundColor Yellow
        return
    }
    foreach ($dir in Get-ChildItem $bundleBase -Directory) {
        try {
            $ctx = Resolve-PpAutomationContext -AutomationId $dir.Name
            Invoke-PpAutoStatus -Ctx $ctx
            Write-Host ''
        } catch { }
    }
}

function Invoke-PpAutoInit {
    param($Ctx)

    $ready = Initialize-PpAutoWorkspace -Ctx $Ctx
    if ($ready) {
        Sync-PpAutoSetupState -Workspace $Ctx.Workspace | Out-Null
        Write-Host '[auto] Workspace bootstrapped (no agent required).' -ForegroundColor Green
        Write-PpAutoNextSteps -Id $Ctx.Id -Phase 'init'
    } else {
        Write-Host '[auto] Bootstrap incomplete - check templates in bundle' -ForegroundColor Yellow
        exit 1
    }
}

function Invoke-PpAutoSetup {
    param($Ctx)

    Initialize-PpAutoWorkspace -Ctx $Ctx | Out-Null

    $agentMode = $env:PP_AUTO_AGENT -ne '0'
    if (-not $agentMode) {
        Sync-PpAutoSetupState -Workspace $Ctx.Workspace | Out-Null
        Write-Host '[auto] Local bootstrap only (--no-agent).' -ForegroundColor Green
        Write-PpAutoNextSteps -Id $Ctx.Id -Phase 'init'
        return
    }

    if ($Ctx.Manifest.safety -and $Ctx.Manifest.safety.require_setup_confirm -eq $false) { }
    elseif (-not (Confirm-PpAutoAction "Setup will run Cursor agent to enrich README/docs:`n  $($Ctx.Workspace)")) {
        Write-Host 'Aborted. Local files kept - run pp auto init or pp auto run when ready.' -ForegroundColor Yellow
        return
    }

    $runDir = New-PpAutoRunLogDir -Workspace $Ctx.Workspace
    $prompt = Get-PpAutoCompiledPrompt -Kind 'setup' -Id $Ctx.Id -BundleRoot $Ctx.Bundle
    $result = Invoke-PpCursorAgent -Prompt $prompt -Workspace $Ctx.Workspace -RunLogDir $runDir -SkipConfirm

    if ($env:PP_DRY_RUN -eq '1') {
        Write-Host '[auto] dry-run complete (setup not marked).' -ForegroundColor Yellow
        return
    }

    $ready = Sync-PpAutoSetupState -Workspace $Ctx.Workspace
    if ($result.Code -ne 0 -and -not $ready) {
        Write-Host '[auto] Setup failed - run pp auto init for offline bootstrap' -ForegroundColor Red
        exit $result.Code
    }

    if ($result.Code -ne 0) {
        Write-Host '[auto] Agent had issues but workspace is bootstrapped - you can pp auto run' -ForegroundColor Yellow
    } else {
        Write-Host '[auto] Setup complete.' -ForegroundColor Green
    }
    Write-PpAutoNextSteps -Id $Ctx.Id -Phase 'setup'
}

function Invoke-PpAutoRun {
    param($Ctx)

    $ready = Sync-PpAutoSetupState -Workspace $Ctx.Workspace
    if (-not $ready -and $env:PP_DRY_RUN -ne '1') {
        Write-Host "[auto] Workspace not ready. Run: pp auto init $($Ctx.Id)" -ForegroundColor Yellow
        exit 1
    }

    $taskCount = Get-PpAutoTaskCount -Manifest $Ctx.Manifest
    Write-PpAutoMsg "[auto] Task count this run: $taskCount" 'DarkGray'

    $runDir = New-PpAutoRunLogDir -Workspace $Ctx.Workspace
    $vars = Get-PpAutoRunVars -Ctx $Ctx -TaskCount $taskCount
    $prompt = Get-PpAutoCompiledPrompt -Kind 'run' -Id $Ctx.Id -BundleRoot $Ctx.Bundle -Vars $vars

    $result = Invoke-PpCursorAgent -Prompt $prompt -Workspace $Ctx.Workspace -RunLogDir $runDir

    if ($env:PP_DRY_RUN -ne '1') {
        $state = Get-PpAutoState -Workspace $Ctx.Workspace
        $runEntry = @{
            at         = (Get-Date).ToString('o')
            task_count = $taskCount
            log_dir    = $runDir
            exit_code  = $result.Code
        }
        if (-not $state['runs']) { $state['runs'] = @() }
        $state['runs'] = @($state['runs']) + @($runEntry)
        $state['last_run'] = (Get-Date).ToString('o')
        Set-PpAutoState -Workspace $Ctx.Workspace -State $state
    }

    if ($result.Code -eq 0 -and $env:PP_DRY_RUN -ne '1') {
        Write-PpAutoNextSteps -Id $Ctx.Id -Phase 'run'
    }
    if ($result.Code -ne 0 -and $env:PP_DRY_RUN -ne '1') { exit $result.Code }
}

function Invoke-PpAutoPrompt {
    param($Ctx, [string]$Kind = 'run')

    if ($Kind -notin @('setup', 'run')) {
        if ($env:PP_AUTO_PROMPT_KIND -eq 'setup') { $Kind = 'setup' }
    }
    $taskCount = Get-PpAutoTaskCount -Manifest $Ctx.Manifest
    $vars = Get-PpAutoRunVars -Ctx $Ctx -TaskCount $taskCount
    $prompt = Get-PpAutoCompiledPrompt -Kind $Kind -Id $Ctx.Id -BundleRoot $Ctx.Bundle -Vars $vars

    $outDir = Join-Path $Ctx.Workspace 'runs'
    if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir -Force | Out-Null }
    $outFile = Join-Path $outDir "prompt-$Kind-$(Get-Date -Format 'yyyyMMdd-HHmmss').md"
    Set-Content -LiteralPath $outFile -Value $prompt -Encoding UTF8

    Write-Host $outFile -ForegroundColor Cyan
    if ($env:PP_QUIET -ne '1') {
        Write-Host '[auto] Open in editor or paste into Cursor agent manually.' -ForegroundColor DarkGray
    }
}

function Invoke-PpAutoUpload {
    param($Ctx)

    $uploadScript = Join-Path $Ctx.Workspace 'upload-task.ps1'
    if (-not (Test-Path -LiteralPath $uploadScript)) {
        Write-Host "[auto] upload-task.ps1 not found - run: pp auto init $($Ctx.Id)" -ForegroundColor Yellow
        exit 1
    }
    & $uploadScript
    exit $LASTEXITCODE
}

function Invoke-PpAutoLogs {
    param($Ctx, [int]$Tail = 40)

    $runsDir = Join-Path $Ctx.Workspace 'runs'
    if (-not (Test-Path $runsDir)) {
        Write-Host 'No runs yet.' -ForegroundColor DarkGray
        return
    }
    $latest = Get-ChildItem $runsDir -Directory | Sort-Object Name -Descending | Select-Object -First 1
    if (-not $latest) { return }

    Write-Host "$($Ctx.Id) latest run: $($latest.Name)" -ForegroundColor Cyan
    foreach ($f in @('meta.json', 'prompt.md', 'agent.stdout.log', 'agent.stderr.log')) {
        $p = Join-Path $latest.FullName $f
        if (Test-Path $p) {
            Write-Host "--- $f ---" -ForegroundColor DarkGray
            Get-Content $p -Tail $Tail
            Write-Host ''
        }
    }
}

function Invoke-PpAutoLogsAll {
    $bundleBase = Get-PpAutoBundledDir
    if (-not (Test-Path $bundleBase)) { return }
    foreach ($dir in Get-ChildItem $bundleBase -Directory) {
        try {
            $ctx = Resolve-PpAutomationContext -AutomationId $dir.Name
            Invoke-PpAutoLogs -Ctx $ctx -Tail 15
            Write-Host ''
        } catch { }
    }
}

function Invoke-PpAutoOpen {
    param($Ctx)
    $cursor = Find-PpCursorEditor
    if (-not $cursor) {
        Write-Host 'cursor CLI not found - use: pp auto goto' -ForegroundColor Red
        exit 1
    }
    & $cursor -n $Ctx.Workspace
}

function Invoke-PpAutoGoto {
    param($Ctx)
    Write-Output $Ctx.Workspace
}

function Invoke-PpAutoExplore {
    param($Ctx)
    explorer.exe $Ctx.Workspace
}

function Invoke-PpAutoReset {
    param($Ctx)

    if ($env:PP_FORCE -ne '1') {
        Write-Host "Reset clears state.json only (keeps repos/, completed/, runs/)." -ForegroundColor Yellow
        $a = Read-Host 'Type reset to confirm'
        if ($a.Trim().ToLowerInvariant() -ne 'reset') {
            Write-Host 'Aborted.' -ForegroundColor Yellow
            return
        }
    }

    $statePath = Join-Path $Ctx.Workspace 'state.json'
    if (Test-Path $statePath) { Remove-Item -LiteralPath $statePath -Force }
    Sync-PpAutoSetupState -Workspace $Ctx.Workspace | Out-Null
    Write-Host '[auto] state.json cleared; bootstrap files kept.' -ForegroundColor Green
}

# Parse --setup / --run / --no-agent from Extra
$promptKind = 'run'
if ($Extra -contains '--setup') { $promptKind = 'setup' }
if ($Extra -contains '--run') { $promptKind = 'run' }
if ($Extra -contains '--no-agent') { $env:PP_AUTO_AGENT = '0' }

try {
    switch ($Mode) {
        'doctor' {
            $ctx = $null
            if ($Id) { $ctx = Resolve-PpAutomationContext -AutomationId $Id }
            Invoke-PpAutoDoctor -Ctx $ctx
        }
        'status-all' { Invoke-PpAutoStatusAll }
        'logs-all'   { Invoke-PpAutoLogsAll }
        'status'     { Invoke-PpAutoStatus -Ctx (Resolve-PpAutomationContext -AutomationId $Id) }
        'init'       { Invoke-PpAutoInit -Ctx (Resolve-PpAutomationContext -AutomationId $Id) }
        'setup'      { Invoke-PpAutoSetup -Ctx (Resolve-PpAutomationContext -AutomationId $Id) }
        'run'        { Invoke-PpAutoRun -Ctx (Resolve-PpAutomationContext -AutomationId $Id) }
        'prompt'     { Invoke-PpAutoPrompt -Ctx (Resolve-PpAutomationContext -AutomationId $Id) -Kind $promptKind }
        'upload'     { Invoke-PpAutoUpload -Ctx (Resolve-PpAutomationContext -AutomationId $Id) }
        'logs'       { Invoke-PpAutoLogs -Ctx (Resolve-PpAutomationContext -AutomationId $Id) }
        'open'       { Invoke-PpAutoOpen -Ctx (Resolve-PpAutomationContext -AutomationId $Id) }
        'goto'       { Invoke-PpAutoGoto -Ctx (Resolve-PpAutomationContext -AutomationId $Id) }
        'explore'    { Invoke-PpAutoExplore -Ctx (Resolve-PpAutomationContext -AutomationId $Id) }
        'reset'      { Invoke-PpAutoReset -Ctx (Resolve-PpAutomationContext -AutomationId $Id) }
        default {
            Write-Host "Unknown mode: $Mode" -ForegroundColor Red
            exit 1
        }
    }
} catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}
