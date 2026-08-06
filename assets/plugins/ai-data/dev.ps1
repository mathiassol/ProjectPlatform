# pp ai dev / run — works from any folder
. "$PSScriptRoot\lib.ps1"

$parsed = Split-AiDataArgs @args
$repo = Get-AiDataRepo -ExplicitPath $parsed.RepoPath -CloneIfMissing
Write-AiDataRepoContext -Repo $repo
Protect-AiDataGitLocal -Repo $repo
Invoke-AiDataDevStack -Repo $repo @($parsed.Args)
