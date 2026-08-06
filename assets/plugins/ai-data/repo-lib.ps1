# PP ai-data plugin — repo resolution (any folder / any clone location)

function Test-AiDataRepoRoot([string]$Path) {
    foreach ($m in @('gradlew.bat', 'package.json', 'backends\plus-agent')) {
        if (-not (Test-Path (Join-Path $Path $m))) { return $false }
    }
    return $true
}

function Get-AiDataDefaultRepoPath {
    Join-Path (Get-AiDataProjectsRoot) (Get-AiDataRepoName)
}

function Get-AiDataPluginStatePath {
    Join-Path $env:LOCALAPPDATA 'ProjectPlatform\plugins\ai-data\state.json'
}

function Get-AiDataPluginState {
    $path = Get-AiDataPluginStatePath
    if (-not (Test-Path -LiteralPath $path)) { return $null }
    try {
        return Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    } catch {
        return $null
    }
}

function Get-AiDataSavedRepoPath {
    $state = Get-AiDataPluginState
    if ($state -and $state.repo_path) {
        $saved = [string]$state.repo_path
        if ($saved -and (Test-Path -LiteralPath $saved) -and (Test-AiDataRepoRoot $saved)) {
            return (Resolve-Path -LiteralPath $saved).Path
        }
    }
    return $null
}

function Set-AiDataSavedRepoPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    $resolved = (Resolve-Path -LiteralPath $Path).Path
    if (-not (Test-AiDataRepoRoot $resolved)) {
        throw "Not an ai-data-monorepo root (need gradlew.bat, package.json, backends\plus-agent): $resolved"
    }

    $dir = Split-Path (Get-AiDataPluginStatePath) -Parent
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }

    $payload = @{ repo_path = $resolved } | ConvertTo-Json
    Set-Content -LiteralPath (Get-AiDataPluginStatePath) -Value $payload -Encoding UTF8
    return $resolved
}

function Find-AiDataRepoFromCwd {
    param([string]$Start)

    if ([string]::IsNullOrWhiteSpace($Start)) {
        $Start = (Get-Location).Path
    }

    $dir = $Start
    while ($dir) {
        if (Test-AiDataRepoRoot $dir) {
            return (Resolve-Path -LiteralPath $dir).Path
        }
        $parent = Split-Path $dir -Parent
        if (-not $parent -or $parent -eq $dir) { break }
        $dir = $parent
    }
    return $null
}

function Split-AiDataArgs {
    param([object[]]$InputArgs)

    $repoPath = $null
    $remaining = New-Object System.Collections.Generic.List[object]

    for ($i = 0; $i -lt $InputArgs.Count; $i++) {
        $a = [string]$InputArgs[$i]
        if ([string]::IsNullOrWhiteSpace($a)) { continue }

        if ($a -in @('--path', '-p') -and ($i + 1) -lt $InputArgs.Count) {
            $repoPath = [string]$InputArgs[++$i]
            continue
        }
        if ($a -match '^--path=(.+)$') {
            $repoPath = $Matches[1]
            continue
        }

        $remaining.Add($InputArgs[$i])
    }

    return @{
        RepoPath = $repoPath
        Args     = $remaining.ToArray()
    }
}

function Resolve-AiDataRepo {
    param(
        [string]$ExplicitPath,
        [switch]$CloneIfMissing
    )

    # 1) explicit --path / argument
    if ($ExplicitPath) {
        if (-not (Test-Path -LiteralPath $ExplicitPath)) {
            throw "Repo path not found: $ExplicitPath"
        }
        $resolved = (Resolve-Path -LiteralPath $ExplicitPath).Path
        if (-not (Test-AiDataRepoRoot $resolved)) {
            throw "Not an ai-data-monorepo root: $resolved"
        }
        Set-AiDataSavedRepoPath $resolved | Out-Null
        return $resolved
    }

    # 2) env override
    if ($env:PP_AI_REPO_PATH) {
        $envPath = [string]$env:PP_AI_REPO_PATH
        if ((Test-Path -LiteralPath $envPath) -and (Test-AiDataRepoRoot $envPath)) {
            return (Resolve-Path -LiteralPath $envPath).Path
        }
    }

    # 3) walk up from current directory
    $fromCwd = Find-AiDataRepoFromCwd
    if ($fromCwd) {
        Set-AiDataSavedRepoPath $fromCwd | Out-Null
        return $fromCwd
    }

    # 4) saved plugin state
    $saved = Get-AiDataSavedRepoPath
    if ($saved) { return $saved }

    # 5) default projects_dir location if it exists
    $default = Get-AiDataDefaultRepoPath
    if ((Test-Path -LiteralPath $default) -and (Test-AiDataRepoRoot $default)) {
        Set-AiDataSavedRepoPath $default | Out-Null
        return (Resolve-Path -LiteralPath $default).Path
    }

    # 6) clone default location
    if ($CloneIfMissing) {
        return Ensure-AiDataDefaultRepo
    }

    return $null
}

function Show-AiDataRepoHint {
    Write-Host '[ai] No ai-data-monorepo found.' -ForegroundColor Yellow
    Write-Host '  cd into a clone, or:' -ForegroundColor DarkGray
    Write-Host '  pp ai use <path>          bind a clone' -ForegroundColor DarkGray
    Write-Host '  pp ai dev --path <path>   one-off' -ForegroundColor DarkGray
    Write-Host '  pp ai .                   clone default + go there' -ForegroundColor DarkGray
}

function Write-AiDataRepoContext {
    param([string]$Repo)
    Write-Host "[ai] Using repo: $Repo" -ForegroundColor DarkGray
}

function Get-AiDataRepo {
    param(
        [string]$ExplicitPath,
        [switch]$CloneIfMissing
    )

    $repo = Resolve-AiDataRepo -ExplicitPath $ExplicitPath -CloneIfMissing:$CloneIfMissing
    if (-not $repo) {
        Show-AiDataRepoHint
        exit 1
    }
    return $repo
}
