# pp ai use [path] — remember which clone to use (defaults to cwd if inside repo)
. "$PSScriptRoot\lib.ps1"

$parsed = Split-AiDataArgs @args
$target = $parsed.RepoPath

if (-not $target) {
    $target = Find-AiDataRepoFromCwd
}

if (-not $target -and $parsed.Args.Count -gt 0) {
    $target = [string]$parsed.Args[0]
}

if (-not $target) {
    Write-Host 'Usage: pp ai use [path]' -ForegroundColor Yellow
    Write-Host '  cd into a clone and run pp ai use' -ForegroundColor DarkGray
    Write-Host '  pp ai use D:\work\ai-data-monorepo' -ForegroundColor DarkGray
    exit 1
}

try {
    $saved = Set-AiDataSavedRepoPath $target
    Write-Host "[ai] Bound repo: $saved" -ForegroundColor Green
    Write-Host '  pp ai dev / setup / wipe work from any folder now' -ForegroundColor DarkGray
} catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}
