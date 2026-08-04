# ProjectPlatform PowerShell hook (reference copy)

function Sync-PpProject {
    $info = & pp.exe here --json 2>$null
    if ($LASTEXITCODE -eq 0 -and $info) {
        $env:PP_PROJECT = $info.Trim()
        $env:PP_PROJECT_PATH = (Get-Location).Path
        return $info.Trim()
    }
    if (-not $env:PP_PROJECT) {
        $env:PP_PROJECT_PATH = $null
    }
    return $null
}

function Invoke-PpEnvApply {
    $block = & pp.exe env apply --shell 2>$null
    if ($LASTEXITCODE -eq 0 -and $block) { Invoke-Expression $block }
}

function Invoke-PpEnvShell {
    param([string[]]$EnvArgs)
    $shellArgs = @('env') + $EnvArgs + @('--shell')
    $block = & pp.exe @shellArgs 2>$null
    if ($LASTEXITCODE -ne 0) {
        & pp.exe env @EnvArgs
        return
    }
    if ($block) { Invoke-Expression $block }
}

function Invoke-PpCd {
    param([string]$Name, [switch]$Quiet)
    $path = & pp.exe cd $Name --quiet 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $path) {
        & pp.exe cd $Name
        return $false
    }
    & pp.exe env clear --shell 2>$null | ForEach-Object { Invoke-Expression $_ }
    Set-Location $path
    $env:PP_PROJECT = $Name
    $env:PP_PROJECT_PATH = $path
    if (-not $Quiet) { Write-Host "-> $path" -ForegroundColor Cyan }
    Invoke-PpEnvApply
    return $true
}

function pp {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Args)
    if ($Args.Count -ge 2) {
        $verb = $Args[0]
        if ($verb -in @('cd', 'goto', 'go', 'enter')) {
            $quiet = $Args -contains '--quiet' -or $Args -contains '-q'
            [void](Invoke-PpCd -Name $Args[1] -Quiet:$quiet)
            return
        }
        if ($verb -eq 'env' -and $Args[1] -in @('apply', 'clear', 'load')) {
            Invoke-PpEnvShell -EnvArgs $Args[1..($Args.Length - 1)]
            return
        }
    }
    & pp.exe @Args
}

function ppgo {
    param([Parameter(Mandatory = $true)][string]$Name)
    Invoke-PpCd -Name $Name
}

function prompt {
    $project = Sync-PpProject
    if ($project) {
        "PP:$project> "
    } else {
        "PS $(Get-Location)> "
    }
}
