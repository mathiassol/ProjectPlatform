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

function Invoke-PpScript {
    param([string[]]$PpArgs)
    $script = (& pp.exe @PpArgs 2>$null | Out-String).Trim()
    if ($LASTEXITCODE -eq 0 -and $script) {
        Invoke-Expression $script
    }
    return $LASTEXITCODE
}

function Invoke-PpEnvApply {
    [void](Invoke-PpScript @('env', 'apply', '--shell'))
}

function Invoke-PpEnvShell {
    param([string[]]$EnvArgs)
    $code = Invoke-PpScript @('env') + $EnvArgs + @('--shell')
    if ($code -ne 0) { & pp.exe env @EnvArgs }
}

function Invoke-PpCd {
    param([string]$Name, [switch]$Quiet)
    $path = & pp.exe cd $Name --quiet 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $path) {
        & pp.exe cd $Name
        return $false
    }
    [void](Invoke-PpScript @('env', 'clear', '--shell'))
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
