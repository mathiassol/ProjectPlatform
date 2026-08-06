# pp ai wipe — delete resolved repo and re-clone
param([switch]$Force)

. "$PSScriptRoot\lib.ps1"

if (-not $Force) {
    foreach ($a in @($args)) {
        if ($a -match '^(--force|-force|-Force)$') { $Force = $true; break }
    }
}

$parsed = Split-AiDataArgs @args
$repo = Get-AiDataRepo -ExplicitPath $parsed.RepoPath -CloneIfMissing:$false
$url = Get-AiDataRepoUrl

if (-not $repo) {
    Write-Host '[ai] No repo bound — nothing to wipe' -ForegroundColor Yellow
    Show-AiDataRepoHint
    exit 1
}

Write-AiDataRepoContext -Repo $repo

if (-not $Force) {
    Write-Host "[ai] This deletes $repo and re-clones from $url" -ForegroundColor Yellow
    Write-Host 'Re-run with -Force or --force to confirm' -ForegroundColor DarkGray
    exit 1
}

Write-Host "[ai] Removing $repo ..." -ForegroundColor Cyan
$parent = Split-Path $repo -Parent
if ((Get-Location).Path -like "$repo*") {
    Set-Location $parent
}
Remove-Item -LiteralPath $repo -Recurse -Force -ErrorAction SilentlyContinue
if (Test-Path $repo) {
    Write-Host '[ai] Could not fully remove repo (files in use). Close editors/terminals in that folder and retry.' -ForegroundColor Yellow
    exit 1
}

Write-Host "[ai] Cloning fresh from $url ..." -ForegroundColor Cyan
if ($repo -eq (Get-AiDataDefaultRepoPath)) {
    Ensure-AiDataDefaultRepo | Out-Null
} else {
    $parentDir = Split-Path $repo -Parent
    if (-not (Test-Path $parentDir)) { New-Item -ItemType Directory -Path $parentDir -Force | Out-Null }
    $clone = & git clone $url $repo 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "git clone failed: $clone" -ForegroundColor Red
        exit 1
    }
    Set-AiDataSavedRepoPath $repo | Out-Null
}

Protect-AiDataGitLocal -Repo $repo
Write-Host '[ai] Wipe complete — run: pp ai setup' -ForegroundColor Green
