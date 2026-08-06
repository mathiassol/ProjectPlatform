# PP ai-data plugin — env via global PP profile (not .envrc in repo)
$AiDataEnvProfile = 'ai-data'

function Import-AiDataPpEnv {
    $raw = (& pp.exe env apply --global --profile $AiDataEnvProfile --shell 2>$null | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or -not $raw -or $raw[0] -ne '{') {
        Write-Host "[ai] Profile '$AiDataEnvProfile' missing or empty" -ForegroundColor Yellow
        Write-Host '  pp env new ai-data --global --template ai-data' -ForegroundColor DarkGray
        Write-Host '  pp env edit ai-data --global' -ForegroundColor DarkGray
        return $false
    }
    try {
        $data = $raw | ConvertFrom-Json -ErrorAction Stop
    } catch {
        Write-Host '[ai] Could not parse env profile JSON' -ForegroundColor Red
        return $false
    }
    if ($data.vars) {
        foreach ($prop in $data.vars.PSObject.Properties) {
            Set-Item -Path ("Env:" + $prop.Name) -Value ([string]$prop.Value)
            if ($prop.Name -eq 'GH_USER')  { $env:GITHUB_USER  = [string]$prop.Value }
            if ($prop.Name -eq 'GH_TOKEN') { $env:GITHUB_TOKEN = [string]$prop.Value }
        }
    }
    if (-not [string]::IsNullOrWhiteSpace($env:AWS_REGION_KEY_PROPERTY) -and
        [string]::IsNullOrWhiteSpace($env:AWS_REGION)) {
        $env:AWS_REGION = $env:AWS_REGION_KEY_PROPERTY
    }
    if ($data.keys) { $script:PP_ENV_KEYS = @($data.keys) }
    $env:PP_ENV_LOADED = '1'
    return $true
}

function Ensure-AiDataEnvProfile {
    $profiles = & pp.exe env profiles --global 2>$null
    if ($profiles -match [regex]::Escape($AiDataEnvProfile)) { return }

    Write-Host "[ai] Creating global env profile: $AiDataEnvProfile" -ForegroundColor Cyan
    & pp.exe env new $AiDataEnvProfile --global --template ai-data 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) {
        & pp.exe env new $AiDataEnvProfile --global 2>&1 | Out-Null
    }
}

function Migrate-AiDataEnvrcToProfile {
    param([string]$Repo)
    $envrc = Join-Path $Repo '.envrc'
    if (-not (Test-Path -LiteralPath $envrc)) { return }

    $profilePath = Join-Path $env:LOCALAPPDATA 'ProjectPlatform\env\profiles\ai-data.env'
    $envrcText = Get-Content -LiteralPath $envrc -Raw -ErrorAction SilentlyContinue
    $profileHasCreds = $false
    if (Test-Path -LiteralPath $profilePath) {
        $profileText = Get-Content -LiteralPath $profilePath -Raw -ErrorAction SilentlyContinue
        $profileHasCreds = $profileText -match 'GH_TOKEN=(?!ghp_your|your-github|placeholder|changeme)[^\r\n#]+'
    }
    $envrcHasCreds = $envrcText -match 'GH_TOKEN=(?!ghp_your|your-github|placeholder|changeme)[^\r\n#]+'

    if (-not $profileHasCreds -and $envrcHasCreds) {
        Write-Host '[ai] Moving .envrc -> PP global profile (repo stays clean)' -ForegroundColor Cyan
        & pp.exe env import $envrc --as $AiDataEnvProfile --global 2>&1 | Out-Null
    } else {
        Write-Host '[ai] Removing repo .envrc (credentials live in PP env profile)' -ForegroundColor Cyan
    }

    Remove-Item -LiteralPath $envrc -Force -ErrorAction SilentlyContinue
    & pp.exe env use $AiDataEnvProfile --global 2>&1 | Out-Null
}

function Install-AiDataRepoPatches {
    param([Parameter(Mandatory = $true)][string]$Repo)

    $patchDir = Join-Path $PSScriptRoot 'patches'
    $runGradlePatch = Join-Path $patchDir 'run-gradle.mjs'
    if (-not (Test-Path -LiteralPath $runGradlePatch)) {
        Write-Host '[ai] Warning: run-gradle patch missing in plugin' -ForegroundColor Yellow
        return
    }

    $dest = Join-Path $Repo 'scripts\run-gradle.mjs'
    $destDir = Split-Path $dest -Parent
    if (-not (Test-Path $destDir)) { New-Item -ItemType Directory -Path $destDir -Force | Out-Null }
    Copy-Item -LiteralPath $runGradlePatch -Destination $dest -Force
    Write-Host '[ai] Patched scripts/run-gradle.mjs for PP env (no .envrc required)' -ForegroundColor DarkGray
}

function Invoke-AiDataDevStack {
    param(
        [Parameter(Mandatory = $true)][string]$Repo,
        [Parameter(ValueFromRemainingArguments = $true)][object[]]$ExtraArgs
    )

    if (-not (Import-AiDataPpEnv)) { exit 1 }

    Push-Location $Repo
    try {
        if (-not $env:AI_AGENT_PROXY_TARGET) {
            $env:AI_AGENT_PROXY_TARGET = 'http://localhost:8811'
        }

        if (-not (Test-Path 'node_modules')) {
            Write-Host '[ai] node_modules missing — npm install' -ForegroundColor Yellow
            & npm install
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        }

        Install-AiDataRepoPatches -Repo $Repo

        Write-Host '[ai] Starting dev stack (env from PP profile ai-data, not .envrc)' -ForegroundColor Cyan
        & npm run dev:stack
        exit $LASTEXITCODE
    } finally {
        Pop-Location
    }
}
