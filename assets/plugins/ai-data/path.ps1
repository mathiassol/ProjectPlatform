# pp ai path — show which repo pp ai commands will use
. "$PSScriptRoot\lib.ps1"

$parsed = Split-AiDataArgs @args
$explicit = $parsed.RepoPath

Write-Host 'ai-data repo resolution:' -ForegroundColor Cyan

if ($explicit) {
    if ((Test-Path -LiteralPath $explicit) -and (Test-AiDataRepoRoot $explicit)) {
        Write-Host "  --path: $(Resolve-Path -LiteralPath $explicit)" -ForegroundColor Green
    } else {
        Write-Host "  --path: $explicit (invalid or not a repo root)" -ForegroundColor Yellow
    }
    exit 0
}

if ($env:PP_AI_REPO_PATH) {
    Write-Host "  PP_AI_REPO_PATH: $env:PP_AI_REPO_PATH" -ForegroundColor DarkGray
}

$fromCwd = Find-AiDataRepoFromCwd
if ($fromCwd) {
    Write-Host "  cwd:      $fromCwd" -ForegroundColor Green
} else {
    Write-Host '  cwd:      (not inside a clone)' -ForegroundColor DarkGray
}

$saved = Get-AiDataSavedRepoPath
if ($saved) {
    Write-Host "  saved:    $saved" -ForegroundColor Green
} else {
    Write-Host '  saved:    (none — run pp ai use)' -ForegroundColor DarkGray
}

$default = Get-AiDataDefaultRepoPath
if ((Test-Path -LiteralPath $default) -and (Test-AiDataRepoRoot $default)) {
    Write-Host "  default:  $default" -ForegroundColor DarkGray
} else {
    Write-Host "  default:  $default (missing)" -ForegroundColor DarkGray
}

$resolved = Resolve-AiDataRepo -CloneIfMissing:$false
Write-Host ''
if ($resolved) {
    Write-Host "active:   $resolved" -ForegroundColor Green
} else {
    Write-Host 'active:   (none — pp ai . or pp ai use <path>)' -ForegroundColor Yellow
}
