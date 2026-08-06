# pp ai status — changed files before commit
$script:AiDataCmdArgs = @($args)
. "$PSScriptRoot\lib.ps1"

$parsed = Split-AiDataArgs $script:AiDataCmdArgs
$repo = Get-AiDataRepo -ExplicitPath $parsed.RepoPath -CloneIfMissing:$false
Write-AiDataRepoContext -Repo $repo

Assert-AiDataGitAvailable
$null = Show-AiDataShippableSummary -Repo $repo -ShowIgnored
