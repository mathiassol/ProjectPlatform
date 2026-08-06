# PP automation - Cursor CLI integration + safety

function Find-PpCursorAgentCli {
    $paths = @(
        (Get-Command agent -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source),
        (Join-Path $env:LOCALAPPDATA 'cursor-agent\agent.cmd'),
        (Join-Path $env:LOCALAPPDATA 'cursor-agent\cursor-agent.cmd'),
        (Join-Path $env:LOCALAPPDATA 'Programs\cursor\resources\app\bin\agent.exe'),
        (Join-Path $env:USERPROFILE '.cursor\bin\agent.exe')
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }

    $paths = @($paths)
    if ($paths.Count -eq 0) { return $null }
    foreach ($p in $paths) {
        if ($p -match '\.(cmd|exe)$') { return $p }
    }
    return $paths[0]
}

function Find-PpCursorEditor {
    $cmd = Get-Command cursor -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $p = Join-Path $env:LOCALAPPDATA 'Programs\cursor\resources\app\bin\cursor.cmd'
    if (Test-Path -LiteralPath $p) { return $p }
    return $null
}

function Test-PpAutoPrerequisites {
    $blocking = [System.Collections.Generic.List[string]]::new()
    $optional = [System.Collections.Generic.List[string]]::new()

    $agent = Find-PpCursorAgentCli
    if (-not $agent) {
        $blocking.Add('Cursor Agent CLI (agent) not found - pp auto init works offline; install CLI for run/setup agent')
    } elseif ($agent.Length -lt 4) {
        $blocking.Add("Cursor Agent CLI path looks invalid: '$agent'")
    }
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
        $blocking.Add('git not on PATH (required for task automations that clone repos)')
    }
    if (-not (Find-PpCursorEditor)) {
        $optional.Add('cursor editor CLI not found (optional: pp auto open)')
    }

    return @{
        Ok     = ($blocking.Count -eq 0)
        Issues = @($blocking) + @($optional)
        Agent  = $agent
    }
}

function Invoke-PpCursorAgentRun {
    param(
        [Parameter(Mandatory = $true)][string]$AgentPath,
        [Parameter(Mandatory = $true)][string]$PromptPath,
        [Parameter(Mandatory = $true)][string]$LogOut,
        [Parameter(Mandatory = $true)][string]$LogErr
    )

    $promptText = Get-Content -LiteralPath $PromptPath -Raw -Encoding UTF8

    if ($AgentPath -match '\.ps1$') {
        $argList = @(
            '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $AgentPath,
            '-p', '-f', '--output-format', 'text', $promptText
        )
        $proc = Start-Process -FilePath 'powershell.exe' -ArgumentList $argList `
            -NoNewWindow -Wait -PassThru `
            -RedirectStandardOutput $LogOut -RedirectStandardError $LogErr
        return $proc.ExitCode
    }

    $output = & $AgentPath -p -f --output-format text $promptText 2>&1
    $exitCode = $LASTEXITCODE
    $output | Set-Content -LiteralPath $LogOut -Encoding UTF8
    if ($exitCode -ne 0) {
        $output | Set-Content -LiteralPath $LogErr -Encoding UTF8
    }
    return $exitCode
}

function Invoke-PpCursorAgent {
    param(
        [Parameter(Mandatory = $true)][string]$Prompt,
        [Parameter(Mandatory = $true)][string]$Workspace,
        [Parameter(Mandatory = $true)][string]$RunLogDir,
        [switch]$SkipConfirm
    )

    $promptPath = Join-Path $RunLogDir 'prompt.md'
    Set-Content -LiteralPath $promptPath -Value $Prompt -Encoding UTF8

    $agent = Find-PpCursorAgentCli
    $meta = @{
        started   = (Get-Date).ToString('o')
        workspace = $Workspace
        agent     = $agent
        prompt    = $promptPath
    }
    $meta | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $RunLogDir 'meta.json') -Encoding UTF8

    if ($env:PP_DRY_RUN -eq '1') {
        Write-PpAutoMsg "[auto] dry-run: prompt written to $promptPath" 'Yellow'
        return @{ Code = 0; PromptPath = $promptPath }
    }

    if (-not $agent -or $agent.Length -lt 4) {
        Write-Host '[auto] Cursor Agent CLI not found.' -ForegroundColor Red
        Write-Host '  Offline: pp auto init / pp auto prompt' -ForegroundColor DarkGray
        Write-Host "  Prompt saved: $promptPath" -ForegroundColor DarkGray
        $cursor = Find-PpCursorEditor
        if ($cursor) {
            Write-Host '  Opening workspace + prompt in Cursor.' -ForegroundColor Yellow
            & $cursor -n $Workspace $promptPath
        }
        return @{ Code = 1; PromptPath = $promptPath }
    }

    if (-not $SkipConfirm -and $env:PP_FORCE -ne '1') {
        if (-not (Confirm-PpAutoAction "Invoke Cursor agent in:`n  $Workspace")) {
            Write-Host 'Aborted.' -ForegroundColor Yellow
            return @{ Code = 0; PromptPath = $promptPath }
        }
    }

    Push-Location $Workspace
    try {
        Write-PpAutoMsg '[auto] Running Cursor agent...' 'Cyan'
        $logOut = Join-Path $RunLogDir 'agent.stdout.log'
        $logErr = Join-Path $RunLogDir 'agent.stderr.log'
        $code = Invoke-PpCursorAgentRun -AgentPath $agent -PromptPath $promptPath -LogOut $logOut -LogErr $logErr

        Write-Host "[auto] Agent exit code: $code" -ForegroundColor $(if ($code -eq 0) { 'Green' } else { 'Yellow' })
        if (Test-Path $logOut) {
            Get-Content $logOut -Tail 15 | ForEach-Object { Write-Host "  $_" -ForegroundColor DarkGray }
        }
        if ($code -ne 0 -and (Test-Path $logErr)) {
            Write-Host '[auto] stderr (tail):' -ForegroundColor Yellow
            Get-Content $logErr -Tail 8 | ForEach-Object { Write-Host "  $_" -ForegroundColor DarkGray }
        }
        return @{ Code = $code; PromptPath = $promptPath }
    } finally {
        Pop-Location
    }
}

function Assert-PpNeverPushMain {
    param([string]$Branch)
    if ($Branch -match '^(main|master)$') {
        throw "Refusing protected branch: $Branch"
    }
}
