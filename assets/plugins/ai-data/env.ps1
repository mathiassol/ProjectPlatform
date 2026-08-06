# pp ai env — manage ai-data credentials in PP global env profile
. "$PSScriptRoot\lib.ps1"

Ensure-AiDataEnvProfile

$sub = if ($args.Count -gt 0) { [string]$args[0] } else { 'edit' }

switch ($sub) {
    'edit' {
        & pp.exe env edit ai-data --global
        exit $LASTEXITCODE
    }
    'use' {
        & pp.exe env use ai-data --global
        exit $LASTEXITCODE
    }
    'show' {
        & pp.exe env profiles --global
        Write-Host ''
        Write-Host 'Profile file: %LOCALAPPDATA%\ProjectPlatform\env\profiles\ai-data.env' -ForegroundColor DarkGray
        exit 0
    }
    default {
        Write-Host 'Usage: pp ai env [edit|use|show]' -ForegroundColor Yellow
        exit 1
    }
}
