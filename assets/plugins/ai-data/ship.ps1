# pp ai ship — interactive wizard (never main/master, excludes local setup noise)
$script:AiDataShipArgs = @($args)
. "$PSScriptRoot\lib.ps1"

$parsed = Split-AiDataArgs $script:AiDataShipArgs
$repo = Get-AiDataRepo -ExplicitPath $parsed.RepoPath -CloneIfMissing:$false
Write-AiDataRepoContext -Repo $repo

$ship = Parse-AiDataGitShipArgs -InputArgs $parsed.Args
Invoke-AiDataGitShipWizard -Repo $repo `
    -Message $ship.Message `
    -Branch $ship.Branch `
    -Force:$ship.Force `
    -DryRun:$ship.DryRun
