# pp ai setup — works from any folder
. "$PSScriptRoot\lib.ps1"

$parsed = Split-AiDataArgs @args
$repo = Get-AiDataRepo -ExplicitPath $parsed.RepoPath -CloneIfMissing
Write-AiDataRepoContext -Repo $repo
Protect-AiDataGitLocal -Repo $repo
Ensure-AiDataEnvProfile
Migrate-AiDataEnvrcToProfile -Repo $repo

Invoke-AiDataInRepo -Command setup -Repo $repo @($parsed.Args)

Migrate-AiDataEnvrcToProfile -Repo $repo
Install-AiDataRepoPatches -Repo $repo
Import-AiDataPpEnv | Out-Null

Write-Host ''
Write-Host '[ai] Env profile: ai-data (global PP store, not .envrc in repo)' -ForegroundColor Green
Write-Host '  pp env edit ai-data --global   - edit credentials' -ForegroundColor DarkGray
Write-Host '  pp ai dev                      - start dev stack' -ForegroundColor DarkGray
