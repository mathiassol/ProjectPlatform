# pp ai . — go to ai-data repo (clone default if nothing else configured)
. "$PSScriptRoot\lib.ps1"

$parsed = Split-AiDataArgs @args
$repo = Get-AiDataRepo -ExplicitPath $parsed.RepoPath -CloneIfMissing
Write-AiDataRepoContext -Repo $repo
Protect-AiDataGitLocal -Repo $repo

Set-Location $repo
$env:PP_PROJECT = Split-Path $repo -Leaf
$env:PP_PROJECT_PATH = $repo
Write-Host "-> $repo" -ForegroundColor Cyan
