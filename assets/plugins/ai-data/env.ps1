# pp ai env — manage ai-data credentials in PP global env profile
. "$PSScriptRoot\lib.ps1"

Ensure-AiDataEnvProfile

$pp = Get-PpCli
$sub = if ($args.Count -gt 0) { [string]$args[0] } else { 'edit' }

switch ($sub) {
    'edit' {
        & $pp env edit ai-data --global
        exit $LASTEXITCODE
    }
    'use' {
        & $pp env use ai-data --global
        exit $LASTEXITCODE
    }
    'show' {
        & $pp env profiles --global
        Write-Host ''
        $profilePath = Join-Path (Get-PpAppDataRoot) 'env/profiles/ai-data.env'
        Write-Host "Profile file: $profilePath" -ForegroundColor DarkGray
        exit 0
    }
    default {
        Write-Host 'Usage: pp ai env [edit|use|show]' -ForegroundColor Yellow
        exit 1
    }
}
