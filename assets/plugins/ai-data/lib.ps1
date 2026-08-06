# AI-Data plugin shared helpers
. "$PSScriptRoot\env-lib.ps1"

function Get-AiDataRepoName {
    if ($env:PP_AI_REPO_NAME) { return $env:PP_AI_REPO_NAME }
    return 'ai-data-monorepo'
}

function Get-AiDataRepoUrl {
    if ($env:PP_AI_REPO_URL) { return $env:PP_AI_REPO_URL }
    return 'https://github.com/Enterprise-Plus/ai-data-monorepo.git'
}

function Get-AiDataProjectsRoot {
    $cfg = Join-Path $env:LOCALAPPDATA 'ProjectPlatform\config.json'
    if (Test-Path $cfg) {
        try {
            $j = Get-Content -LiteralPath $cfg -Raw | ConvertFrom-Json
            if ($j.projects_dir) { return [string]$j.projects_dir }
        } catch { }
    }
    return Join-Path $env:USERPROFILE 'Documents\Projects'
}

. "$PSScriptRoot\repo-lib.ps1"
. "$PSScriptRoot\git-lib.ps1"

function Get-AiDataRepoPath {
    Get-AiDataDefaultRepoPath
}

function Ensure-AiDataDefaultRepo {
    $repo = Get-AiDataDefaultRepoPath
    $url = Get-AiDataRepoUrl
    $name = Get-AiDataRepoName

    if ((Test-Path $repo) -and (Test-AiDataRepoRoot $repo)) {
        Set-AiDataSavedRepoPath $repo | Out-Null
        return (Resolve-Path -LiteralPath $repo).Path
    }

    $parent = Split-Path $repo -Parent
    if (-not (Test-Path $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }

    if (Test-Path $repo) {
        Write-Host "[ai] Removing incomplete folder: $repo" -ForegroundColor Yellow
        Remove-Item -LiteralPath $repo -Recurse -Force
    }

    Write-Host "[ai] Cloning $url -> $repo" -ForegroundColor Cyan
    $clone = & git clone $url $repo 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "git clone failed: $clone"
    }

    if (-not (Test-AiDataRepoRoot $repo)) {
        throw "Cloned repo does not look like ai-data-monorepo"
    }

    $resolved = (Resolve-Path -LiteralPath $repo).Path
    Set-AiDataSavedRepoPath $resolved | Out-Null
    Write-Host "[ai] Repository ready: $resolved" -ForegroundColor Green
    return $resolved
}

# Back-compat alias
function Ensure-AiDataRepo {
    param(
        [string]$ExplicitPath,
        [switch]$CloneIfMissing
    )
    if (-not $PSBoundParameters.ContainsKey('CloneIfMissing')) {
        $CloneIfMissing = $true
    }
    return Get-AiDataRepo -ExplicitPath $ExplicitPath -CloneIfMissing:$CloneIfMissing
}

function Get-AiDataSetupScript([string]$Repo) {
    foreach ($rel in @('autosetup.ps1', 'windows-dev.ps1')) {
        $p = Join-Path $Repo $rel
        if (Test-Path $p) { return $p }
    }
    $globalScript = Join-Path (Get-AiDataProjectsRoot) '.scripts\aisetup.ps1'
    if (Test-Path $globalScript) {
        Copy-Item -LiteralPath $globalScript -Destination (Join-Path $Repo 'autosetup.ps1') -Force
        return Join-Path $Repo 'autosetup.ps1'
    }
    throw 'No setup script found (autosetup.ps1 / windows-dev.ps1 / global aisetup.ps1)'
}

function Protect-AiDataGitLocal {
    param([string]$Repo)

    $gitignore = Join-Path $Repo '.gitignore'
    $marker = '# PP ai-data plugin — local Windows dev (never commit)'
    $block = @'

# PP ai-data plugin — local Windows dev (never commit)
.envrc
flags.local.json
scripts/load-envrc.mjs
scripts/run-gradle.mjs
scripts/patch-package-json.mjs
frontends/ai-chat/scripts/dev.mjs
windows-dev.ps1
autosetup.ps1
.pp/
'@

    if (Test-Path $gitignore) {
        $text = Get-Content -LiteralPath $gitignore -Raw
        if ($text -notmatch [regex]::Escape($marker)) {
            Add-Content -LiteralPath $gitignore -Value $block -Encoding UTF8
            Write-Host '[ai] Updated .gitignore for local dev files' -ForegroundColor DarkGray
        }
    } else {
        Set-Content -LiteralPath $gitignore -Value $block.TrimStart() -Encoding UTF8
    }

    if (-not (Get-Command git -ErrorAction SilentlyContinue)) { return }
    if (-not (Test-Path (Join-Path $Repo '.git'))) { return }

    Push-Location $Repo
    try {
        $skip = @(
            'package.json',
            'frontends/ai-chat/package.json',
            'backends/plus-agent/build.gradle'
        )
        foreach ($rel in $skip) {
            $full = Join-Path $Repo ($rel -replace '/', '\')
            if (-not (Test-Path $full)) { continue }
            & git update-index --skip-worktree $rel 2>$null | Out-Null
        }
        Write-Host '[ai] Marked patched tracked files as skip-worktree (local-only changes)' -ForegroundColor DarkGray
    } finally {
        Pop-Location
    }
}

function Invoke-AiDataInRepo {
    param(
        [Parameter(Mandatory = $true)][string]$Command,
        [string]$Repo,
        [Parameter(ValueFromRemainingArguments = $true)][object[]]$ExtraArgs
    )

    if (-not $Repo) { $Repo = Get-AiDataRepo -CloneIfMissing }
    Protect-AiDataGitLocal -Repo $Repo
    $setup = Get-AiDataSetupScript -Repo $Repo

    Push-Location $Repo
    try {
        $pass = @($ExtraArgs | Where-Object { $null -ne $_ -and $_ -ne '' })
        $skipNpm = ($pass -contains '-SkipNpmInstall') -or ($pass -contains '--SkipNpmInstall')
        $force = ($pass -contains '-Force') -or ($pass -contains '--force') -or ($pass -contains '-force')

        if ($skipNpm -or $force) {
            & $setup $Command -SkipNpmInstall:$skipNpm -Force:$force
        } elseif ($pass.Count -gt 0) {
            & $setup $Command @pass
        } else {
            & $setup $Command
        }
        if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    } finally {
        Pop-Location
    }
}
