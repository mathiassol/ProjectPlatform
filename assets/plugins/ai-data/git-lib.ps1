# PP ai-data plugin - safe git workflow (never push main/master)

$script:AiDataProtectedBranches = @('main', 'master')

$script:AiDataShipExcludeExact = @(
    '.envrc',
    '.envrc.example',
    'package-lock.json',
    '.gitignore',
    'flags.local.json',
    'scripts/load-envrc.mjs',
    'scripts/run-gradle.mjs',
    'scripts/patch-package-json.mjs',
    'frontends/ai-chat/scripts/dev.mjs',
    'windows-dev.ps1',
    'autosetup.ps1'
)

function Test-AiDataGitAvailable {
    return [bool](Get-Command git -ErrorAction SilentlyContinue)
}

function Assert-AiDataGitAvailable {
    if (-not (Test-AiDataGitAvailable)) {
        throw 'git is not on PATH'
    }
}

function Test-AiDataProtectedBranchName {
    param([string]$Name)
    if ([string]::IsNullOrWhiteSpace($Name)) { return $false }
    return ($Name.Trim().ToLowerInvariant() -in $script:AiDataProtectedBranches)
}

function Assert-AiDataSafeBranchName {
    param([string]$Branch)
    if (Test-AiDataProtectedBranchName $Branch) {
        throw "Protected branch '$Branch'. Use a feature branch (pp ai ship -b my-feature -m `"message`")"
    }
}

function Get-AiDataGitRoot {
    param([Parameter(Mandatory = $true)][string]$Repo)
    Push-Location $Repo
    try {
        $root = (& git rev-parse --show-toplevel 2>$null).Trim()
        if ($LASTEXITCODE -ne 0 -or -not $root) {
            throw "Not a git repository: $Repo"
        }
        return (Resolve-Path -LiteralPath $root).Path
    } finally {
        Pop-Location
    }
}

function Get-AiDataGitCurrentBranch {
    param([string]$Repo)
    Push-Location $Repo
    try {
        $branch = (& git symbolic-ref --quiet --short HEAD 2>$null).Trim()
        if ($LASTEXITCODE -ne 0 -or -not $branch) {
            $branch = (& git rev-parse --short HEAD 2>$null).Trim()
            if (-not $branch) { return '(unknown)' }
            return "detached@$branch"
        }
        return $branch
    } finally {
        Pop-Location
    }
}

function Get-AiDataGitDefaultRemoteBranch {
    param([string]$Repo)
    Push-Location $Repo
    try {
        $ref = (& git symbolic-ref --quiet refs/remotes/origin/HEAD 2>$null).Trim()
        if ($ref -match 'refs/remotes/origin/(.+)$') {
            return $Matches[1]
        }
        foreach ($candidate in @('main', 'master')) {
            & git show-ref --verify --quiet "refs/remotes/origin/$candidate" 2>$null | Out-Null
            if ($LASTEXITCODE -eq 0) { return $candidate }
        }
        return 'main'
    } finally {
        Pop-Location
    }
}

function Get-AiDataGitHubCompareUrl {
    param(
        [string]$Repo,
        [string]$FeatureBranch,
        [string]$BaseBranch
    )
    $url = Get-AiDataRepoUrl
    if ($url -match 'github\.com[/:]([^/]+/[^/.]+?)(?:\.git)?/?$') {
        $slug = $Matches[1]
        return "https://github.com/$slug/compare/$BaseBranch...$FeatureBranch?expand=1"
    }
    return $null
}

function Get-AiDataGitStatusLines {
    param([string]$Repo)
    Push-Location $Repo
    try {
        $lines = @(& git status --porcelain=v1 2>&1)
        if ($LASTEXITCODE -ne 0) {
            throw "git status failed: $($lines -join ' ')"
        }
        return @($lines | Where-Object { $_ -match '\S' })
    } finally {
        Pop-Location
    }
}

function Test-AiDataGitHasChanges {
    param([string]$Repo)
    return (Get-AiDataGitStatusLines -Repo $Repo).Count -gt 0
}

function Test-AiDataShipExcludedPath {
    param([string]$Path)
    $norm = ($Path -replace '\\', '/').Trim()
    if ($norm -eq '.pp' -or $norm -like '.pp/*') { return $true }
    foreach ($exact in $script:AiDataShipExcludeExact) {
        if ($norm -eq $exact) { return $true }
    }
    return $false
}

function ConvertFrom-AiDataGitStatusLine {
    param([string]$Line)
    if ($Line.Length -lt 4) { return $null }
    return [PSCustomObject]@{
        Code = $Line.Substring(0, 2)
        Path = ($Line.Substring(3).Trim() -replace '\\', '/')
    }
}

function Get-AiDataShippableChanges {
    param([string]$Repo)

    $shippable = New-Object System.Collections.Generic.List[object]
    $ignored = New-Object System.Collections.Generic.List[object]

    foreach ($line in (Get-AiDataGitStatusLines -Repo $Repo)) {
        $entry = ConvertFrom-AiDataGitStatusLine $line
        if (-not $entry) { continue }
        if (Test-AiDataShipExcludedPath $entry.Path) {
            $ignored.Add($entry)
        } else {
            $shippable.Add($entry)
        }
    }

    return @{
        Shippable = $shippable.ToArray()
        Ignored   = $ignored.ToArray()
    }
}

function Show-AiDataShippableSummary {
    param(
        [string]$Repo,
        [switch]$ShowIgnored
    )

    $changes = Get-AiDataShippableChanges -Repo $Repo
    $branch = Get-AiDataGitCurrentBranch -Repo $Repo

    Write-Host "branch: $branch" -ForegroundColor Cyan
    if (Test-AiDataProtectedBranchName $branch) {
        Write-Host '  (protected - ship wizard creates a feature branch)' -ForegroundColor DarkGray
    }

    if ($changes.Shippable.Count -eq 0) {
        Write-Host 'shippable: (none)' -ForegroundColor DarkGray
    } else {
        Write-Host "shippable: $($changes.Shippable.Count) file(s)" -ForegroundColor Green
        foreach ($e in $changes.Shippable) {
            Write-Host "  $($e.Code) $($e.Path)" -ForegroundColor Gray
        }
    }

    if ($ShowIgnored -and $changes.Ignored.Count -gt 0) {
        Write-Host "ignored (local/setup): $($changes.Ignored.Count) file(s)" -ForegroundColor DarkYellow
        foreach ($e in $changes.Ignored) {
            Write-Host "  $($e.Code) $($e.Path)" -ForegroundColor DarkGray
        }
    }

    return $changes
}

function Show-AiDataGitStatus {
    param([string]$Repo)
    $null = Show-AiDataShippableSummary -Repo $Repo -ShowIgnored
}

function Show-AiDataGitDiffForPaths {
    param(
        [string]$Repo,
        [string[]]$Paths
    )
    if ($Paths.Count -eq 0) { return }
    Push-Location $Repo
    try {
        Write-Host '--- diff (shippable files only) ---' -ForegroundColor Cyan
        & git diff --stat -- @Paths 2>&1
        & git diff -- @Paths 2>&1
    } finally {
        Pop-Location
    }
}

function Show-AiDataGitDiff {
    param(
        [string]$Repo,
        [switch]$Staged,
        [int]$StatWidth = 120
    )

    Push-Location $Repo
    try {
        if ($Staged) {
            Write-Host '--- staged diff (git diff --cached) ---' -ForegroundColor Cyan
            & git diff --cached --stat --stat-width=$StatWidth 2>&1
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
            & git diff --cached 2>&1
        } else {
            $changes = Get-AiDataShippableChanges -Repo $Repo
            $paths = @($changes.Shippable | ForEach-Object { $_.Path })
            if ($paths.Count -eq 0) {
                Write-Host 'No shippable changes to diff.' -ForegroundColor DarkGray
                exit 0
            }
            Show-AiDataGitDiffForPaths -Repo $Repo -Paths $paths
        }
        exit $LASTEXITCODE
    } finally {
        Pop-Location
    }
}

function ConvertTo-AiDataBranchSlug {
    param([string]$Text)
    if ([string]::IsNullOrWhiteSpace($Text)) { return 'update' }
    $slug = $Text.ToLowerInvariant() -replace '[^a-z0-9]+', '-'
    $slug = $slug.Trim('-')
    if ($slug.Length -gt 48) { $slug = $slug.Substring(0, 48).Trim('-') }
    if ([string]::IsNullOrWhiteSpace($slug)) { return 'update' }
    return $slug
}

function New-AiDataFeatureBranchName {
    param([string]$Message)
    $slug = ConvertTo-AiDataBranchSlug $Message
    $user = if ($env:PP_AI_GIT_USER) { $env:PP_AI_GIT_USER } else { $env:USERNAME }
    $user = ($user -replace '[^a-zA-Z0-9]', '').ToLowerInvariant()
    if ([string]::IsNullOrWhiteSpace($user)) { $user = 'pp' }
    return "$user/$(Get-Date -Format 'yyyyMMdd')-$slug"
}

function Parse-AiDataGitShipArgs {
    param([object[]]$InputArgs)

    $result = @{
        Message = $null
        Branch  = $null
        Force   = $false
        DryRun  = $false
    }

    if ($null -eq $InputArgs) { return $result }

    for ($i = 0; $i -lt $InputArgs.Count; $i++) {
        $a = [string]$InputArgs[$i]
        switch ($a) {
            '-m' { if ($i + 1 -lt $InputArgs.Count) { $result.Message = [string]$InputArgs[++$i] } }
            '--message' { if ($i + 1 -lt $InputArgs.Count) { $result.Message = [string]$InputArgs[++$i] } }
            '-b' { if ($i + 1 -lt $InputArgs.Count) { $result.Branch = [string]$InputArgs[++$i] } }
            '--branch' { if ($i + 1 -lt $InputArgs.Count) { $result.Branch = [string]$InputArgs[++$i] } }
            { $_ -in @('-Force', '--force', '-force', '-f') } { $result.Force = $true }
            '--dry-run' { $result.DryRun = $true }
            'dry-run' { $result.DryRun = $true }
            default {
                if (-not $result.Message -and $a -and $a[0] -ne '-') {
                    $result.Message = $a
                }
            }
        }
    }

    if ($env:PP_FORCE -eq '1') { $result.Force = $true }
    if ($env:PP_DRY_RUN -eq '1') { $result.DryRun = $true }

    return $result
}

function Undo-AiDataShipWizard {
    param(
        [string]$Repo,
        [hashtable]$State
    )

    Push-Location $Repo
    try {
        if ($State.DidCommit) {
            Write-Host '[ai] Undo: removing local commit...' -ForegroundColor Yellow
            & git reset --mixed HEAD~1 2>&1 | Out-Host
        } elseif ($State.DidStage) {
            Write-Host '[ai] Undo: unstaging...' -ForegroundColor Yellow
            & git reset HEAD 2>&1 | Out-Host
        }

        if ($State.CreatedBranch -and $State.StartBranch) {
            $cur = Get-AiDataGitCurrentBranch -Repo $Repo
            if ($cur -eq $State.TargetBranch) {
                Write-Host "[ai] Undo: back to $($State.StartBranch)..." -ForegroundColor Yellow
                & git checkout $State.StartBranch 2>&1 | Out-Host
                if ($State.BranchWasNew) {
                    & git branch -D $State.TargetBranch 2>&1 | Out-Host
                }
            }
        }
    } finally {
        Pop-Location
    }
}

function Invoke-AiDataGitShipWizard {
    param(
        [Parameter(Mandatory = $true)][string]$Repo,
        [string]$Message,
        [string]$Branch,
        [switch]$Force,
        [switch]$DryRun
    )

    Assert-AiDataGitAvailable
    $gitRoot = Get-AiDataGitRoot -Repo $Repo

    $state = @{
        StartBranch   = $null
        TargetBranch  = $null
        CreatedBranch = $false
        BranchWasNew  = $false
        DidStage      = $false
        DidCommit     = $false
    }

    Write-Host ''
    Write-Host '=== pp ai ship ===' -ForegroundColor Cyan
    Write-Host ''

    # Step 1: review
    Write-Host 'Step 1/5  Review changes' -ForegroundColor Cyan
    $changes = Show-AiDataShippableSummary -Repo $gitRoot -ShowIgnored

    if ($changes.Shippable.Count -eq 0) {
        Write-Host ''
        if ($changes.Ignored.Count -gt 0) {
            Write-Host '[ai] No shippable changes (only local/setup files differ).' -ForegroundColor Yellow
            Write-Host '  Those files are from pp ai setup and are never shipped.' -ForegroundColor DarkGray
        } else {
            Write-Host '[ai] No changes to ship.' -ForegroundColor Yellow
        }
        exit 0
    }

    if (-not $Force -and -not $DryRun) {
        while ($true) {
            $choice = (Read-Host '  [c] continue  [d] diff  [a] abort').Trim().ToLowerInvariant()
            if ($choice -in @('a', 'abort')) {
                Write-Host 'Aborted.' -ForegroundColor Yellow
                exit 0
            }
            if ($choice -in @('d', 'diff')) {
                $paths = @($changes.Shippable | ForEach-Object { $_.Path })
                Show-AiDataGitDiffForPaths -Repo $gitRoot -Paths $paths
                continue
            }
            if ($choice -in @('c', 'continue', '')) { break }
            Write-Host '  Enter c, d, or a' -ForegroundColor DarkGray
        }
    }

    # Step 2: message
    Write-Host ''
    Write-Host 'Step 2/5  Commit message' -ForegroundColor Cyan
    while ([string]::IsNullOrWhiteSpace($Message)) {
        if ($DryRun) { $Message = 'dry-run message'; break }
        $Message = Read-Host '  Message'
        if ([string]::IsNullOrWhiteSpace($Message)) {
            Write-Host '  Message is required.' -ForegroundColor Yellow
        }
    }

    if (-not $Force -and -not $DryRun) {
        $choice = (Read-Host "  Message: `"$Message`"  [c] continue  [a] abort").Trim().ToLowerInvariant()
        if ($choice -in @('a', 'abort')) {
            Write-Host 'Aborted.' -ForegroundColor Yellow
            exit 0
        }
    }

    # Step 3: branch
    Write-Host ''
    Write-Host 'Step 3/5  Branch' -ForegroundColor Cyan
    $state.StartBranch = Get-AiDataGitCurrentBranch -Repo $gitRoot
    $targetBranch = $Branch

    if ([string]::IsNullOrWhiteSpace($targetBranch)) {
        if (Test-AiDataProtectedBranchName $state.StartBranch) {
            $targetBranch = New-AiDataFeatureBranchName -Message $Message
        } else {
            $targetBranch = $state.StartBranch
        }
    }

    Assert-AiDataSafeBranchName $targetBranch
    $state.TargetBranch = $targetBranch
    $needNewBranch = ($state.StartBranch -ne $targetBranch)

    Write-Host "  branch: $targetBranch" -ForegroundColor Gray
    if ($needNewBranch) {
        Write-Host "  (new branch from $($state.StartBranch))" -ForegroundColor DarkGray
    }

    if (-not $Force -and -not $DryRun) {
        while ($true) {
            $choice = (Read-Host '  [c] continue  [e] edit branch  [a] abort').Trim().ToLowerInvariant()
            if ($choice -in @('a', 'abort')) {
                Write-Host 'Aborted.' -ForegroundColor Yellow
                exit 0
            }
            if ($choice -in @('e', 'edit')) {
                $edited = Read-Host '  Branch name'
                if (-not [string]::IsNullOrWhiteSpace($edited)) {
                    Assert-AiDataSafeBranchName $edited.Trim()
                    $targetBranch = $edited.Trim()
                    $state.TargetBranch = $targetBranch
                    $needNewBranch = ($state.StartBranch -ne $targetBranch)
                    Write-Host "  branch: $targetBranch" -ForegroundColor Gray
                }
                continue
            }
            if ($choice -in @('c', 'continue', '')) { break }
        }
    }

    $shipPaths = @($changes.Shippable | ForEach-Object { $_.Path })

    if ($DryRun) {
        Write-Host ''
        Write-Host '[dry-run] Would ship:' -ForegroundColor Yellow
        foreach ($p in $shipPaths) { Write-Host "  $p" -ForegroundColor DarkGray }
        if ($needNewBranch) { Write-Host "  git checkout -b $targetBranch" -ForegroundColor DarkGray }
        Write-Host "  git add -- $($shipPaths -join ' ')" -ForegroundColor DarkGray
        Write-Host "  git commit -m `"$Message`"" -ForegroundColor DarkGray
        Write-Host '  push requires typing: confirm' -ForegroundColor DarkGray
        exit 0
    }

    # Step 4: commit
    Write-Host ''
    Write-Host 'Step 4/5  Commit' -ForegroundColor Cyan

    Push-Location $gitRoot
    try {
        if ($needNewBranch) {
            Write-Host "  creating branch $targetBranch ..." -ForegroundColor DarkGray
            & git checkout -b $targetBranch 2>&1 | Out-Host
            if ($LASTEXITCODE -ne 0) {
                & git checkout $targetBranch 2>&1 | Out-Host
                if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
                $state.BranchWasNew = $false
            } else {
                $state.BranchWasNew = $true
            }
            $state.CreatedBranch = $true
        }

        Assert-AiDataSafeBranchName (Get-AiDataGitCurrentBranch -Repo $gitRoot)

        Write-Host "  staging $($shipPaths.Count) file(s)..." -ForegroundColor DarkGray
        & git add -- @shipPaths 2>&1 | Out-Host
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        $state.DidStage = $true

        $staged = @(& git diff --cached --name-only 2>&1)
        if ($staged.Count -eq 0) {
            Write-Host '[ai] Nothing staged - aborting.' -ForegroundColor Yellow
            Undo-AiDataShipWizard -Repo $gitRoot -State $state
            exit 1
        }

        Write-Host '  staged:' -ForegroundColor Green
        foreach ($s in $staged) { Write-Host "    $s" -ForegroundColor Gray }

        if (-not $Force) {
            $choice = (Read-Host '  [c] commit  [a] abort (undo branch/staging)').Trim().ToLowerInvariant()
            if ($choice -in @('a', 'abort')) {
                Undo-AiDataShipWizard -Repo $gitRoot -State $state
                Write-Host 'Aborted - changes restored.' -ForegroundColor Yellow
                exit 0
            }
        }

        Write-Host '  committing...' -ForegroundColor DarkGray
        & git commit -m $Message 2>&1 | Out-Host
        if ($LASTEXITCODE -ne 0) {
            Undo-AiDataShipWizard -Repo $gitRoot -State $state
            exit $LASTEXITCODE
        }
        $state.DidCommit = $true
        $state.DidStage = $false
    } finally {
        Pop-Location
    }

    # Step 5: push
    Write-Host ''
    Write-Host 'Step 5/5  Push to GitHub' -ForegroundColor Cyan
    $pushBranch = $state.TargetBranch
    Write-Host "  will push: origin/$pushBranch" -ForegroundColor Gray
    Write-Host '  never pushes main or master' -ForegroundColor DarkGray

    if ($Force) {
        $typed = 'confirm'
    } else {
        Write-Host ''
        Write-Host '  Type confirm to push (anything else aborts and undoes commit/branch)' -ForegroundColor Yellow
        $typed = Read-Host '  >'
    }

    if ($typed.Trim().ToLowerInvariant() -ne 'confirm') {
        Write-Host 'Push cancelled - undoing commit and branch...' -ForegroundColor Yellow
        Undo-AiDataShipWizard -Repo $gitRoot -State $state
        Write-Host 'Aborted - repo restored to pre-ship state.' -ForegroundColor Yellow
        exit 0
    }

    Push-Location $gitRoot
    try {
        Assert-AiDataSafeBranchName (Get-AiDataGitCurrentBranch -Repo $gitRoot)
        Write-Host "  pushing to origin/$pushBranch ..." -ForegroundColor Cyan
        & git push -u origin $pushBranch 2>&1 | Out-Host
        if ($LASTEXITCODE -ne 0) {
            Write-Host '[ai] Push failed - commit kept locally on branch.' -ForegroundColor Red
            exit $LASTEXITCODE
        }

        $base = Get-AiDataGitDefaultRemoteBranch -Repo $gitRoot
        $compare = Get-AiDataGitHubCompareUrl -Repo $gitRoot -FeatureBranch $pushBranch -BaseBranch $base
        Write-Host ''
        Write-Host '[ai] Shipped - open a PR manually on GitHub:' -ForegroundColor Green
        if ($compare) {
            Write-Host "  $compare" -ForegroundColor Cyan
        } else {
            Write-Host "  branch: $pushBranch (base: $base)" -ForegroundColor DarkGray
        }
    } finally {
        Pop-Location
    }
}

function Invoke-AiDataGitShip {
    param(
        [Parameter(Mandatory = $true)][string]$Repo,
        [string]$Message,
        [string]$Branch,
        [switch]$Force,
        [switch]$DryRun
    )
    Invoke-AiDataGitShipWizard -Repo $Repo -Message $Message -Branch $Branch -Force:$Force -DryRun:$DryRun
}
