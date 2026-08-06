# pp ai diff — diff before commit (working tree + staged)
$script:AiDataCmdArgs = @($args)
. "$PSScriptRoot\lib.ps1"

$parsed = Split-AiDataArgs $script:AiDataCmdArgs
$repo = Get-AiDataRepo -ExplicitPath $parsed.RepoPath -CloneIfMissing:$false
Write-AiDataRepoContext -Repo $repo

Assert-AiDataGitAvailable

$staged = ($parsed.Args -contains '--staged') -or ($parsed.Args -contains '-s')
Show-AiDataGitDiff -Repo $repo -Staged:$staged
