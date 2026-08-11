# ProjectPlatform PowerShell hook (reference copy)
# Installed via $PROFILE wrapper — re-sources this file on each pp/prompt call.

function Normalize-PpPath {
    param([string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path)) { return '' }
    $clean = $Path.Trim().Replace('\\', '\').TrimEnd('\')
    try {
        return ([System.IO.Path]::GetFullPath($clean)).TrimEnd('\')
    } catch {
        return $clean
    }
}

function Sync-PpProject {
    $info = & pp.exe here --json 2>$null
    if ($LASTEXITCODE -eq 0 -and $info) {
        $name = $info.Trim()
        $env:PP_PROJECT = $name
        $root = & pp.exe cd $name --quiet 2>$null
        if ($LASTEXITCODE -eq 0 -and $root) {
            $env:PP_PROJECT_ROOT = Normalize-PpPath $root
        }
        $env:PP_PROJECT_PATH = (Get-Location).Path
        return $name
    }
    if (-not $env:PP_PROJECT) {
        $env:PP_PROJECT_PATH = $null
        $env:PP_PROJECT_ROOT = $null
    }
    return $null
}

function Invoke-PpEnvJson {
    param([string[]]$PpArgs)
    $raw = (& pp.exe @PpArgs 2>$null | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or -not $raw -or $raw[0] -ne '{') { return $LASTEXITCODE }
    try {
        $data = $raw | ConvertFrom-Json -ErrorAction Stop
    } catch {
        return 1
    }
    if ($data.clear) {
        foreach ($k in @($data.keys)) {
            Remove-Item -Path ("Env:" + $k) -ErrorAction SilentlyContinue
        }
        $script:PP_ENV_KEYS = @()
        Remove-Item Env:PP_ENV_LOADED -ErrorAction SilentlyContinue
    } else {
        if ($data.keys) { $script:PP_ENV_KEYS = @($data.keys) }
        if ($data.vars) {
            foreach ($prop in $data.vars.PSObject.Properties) {
                Set-Item -Path ("Env:" + $prop.Name) -Value ([string]$prop.Value)
            }
            $env:PP_ENV_LOADED = '1'
        }
    }
    return 0
}

function Invoke-PpEnvApply {
    [void](Invoke-PpEnvJson @('env', 'apply', '--shell'))
}

function Invoke-PpEnvShell {
    param([string[]]$EnvArgs)
    $code = Invoke-PpEnvJson @('env') + $EnvArgs + @('--shell')
    if ($code -ne 0) { & pp.exe env @EnvArgs }
}

function Invoke-PpCd {
    param([string]$Name, [switch]$Quiet)
    $path = & pp.exe cd $Name --quiet 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $path) {
        & pp.exe cd $Name
        return $false
    }
    [void](Invoke-PpEnvJson @('env', 'clear', '--shell'))
    Set-Location $path
    $env:PP_PROJECT = $Name
    $env:PP_PROJECT_PATH = $path
    $env:PP_PROJECT_ROOT = Normalize-PpPath $path
    if (-not $Quiet) { Write-Host "-> $path" -ForegroundColor Cyan }
    Invoke-PpEnvApply
    return $true
}

function pp-hook-restore {
    param([string]$SessionPath)
    if (-not (Test-Path $SessionPath)) {
        Write-Host '[pp] No restart session found' -ForegroundColor Yellow
        return
    }
    try {
        $data = Get-Content -Path $SessionPath -Raw -Encoding UTF8 | ConvertFrom-Json
    } catch {
        Write-Host '[pp] Could not read restart session' -ForegroundColor Red
        return
    }
    if ($data.project_path -and (Test-Path $data.project_path)) {
        Set-Location $data.project_path
    } elseif ($data.cwd -and (Test-Path $data.cwd)) {
        Set-Location $data.cwd
    }
    if ($data.project) {
        $env:PP_PROJECT = [string]$data.project
        if ($data.project_path) { $env:PP_PROJECT_PATH = [string]$data.project_path }
        & pp.exe cd $data.project --quiet 2>$null | Out-Null
    }
    if ($data.env_vars) {
        $keys = @()
        foreach ($prop in $data.env_vars.PSObject.Properties) {
            Set-Item -Path ("Env:" + $prop.Name) -Value ([string]$prop.Value)
            $keys += $prop.Name
        }
        $script:PP_ENV_KEYS = $keys
        $env:PP_ENV_LOADED = '1'
    } elseif ($data.project) {
        Invoke-PpEnvApply
    }
    Remove-Item $SessionPath -ErrorAction SilentlyContinue
    Write-Host "[pp] Session restored -> $(Get-Location)" -ForegroundColor Green
}

function Invoke-PpRestart {
    $sessionPath = Join-Path $env:TEMP 'pp-restart-session.json'
    $envVars = [ordered]@{}
    $keys = @()
    if ($script:PP_ENV_KEYS -and @($script:PP_ENV_KEYS).Count -gt 0) {
        $keys = @($script:PP_ENV_KEYS)
        foreach ($k in $keys) {
            $item = Get-Item -Path ("Env:" + $k) -ErrorAction SilentlyContinue
            if ($item) { $envVars[$k] = [string]$item.Value }
        }
    } elseif ($env:PP_ENV_LOADED -eq '1') {
        $raw = (& pp.exe env apply --shell 2>$null | Out-String).Trim()
        if ($raw -and $raw[0] -eq '{') {
            try {
                $bundle = $raw | ConvertFrom-Json
                if ($bundle.keys) { $keys = @($bundle.keys) }
                if ($bundle.vars) {
                    foreach ($prop in $bundle.vars.PSObject.Properties) {
                        $envVars[$prop.Name] = [string]$prop.Value
                    }
                }
            } catch { }
        }
    }
    $project = $env:PP_PROJECT
    if (-not $project) {
        $project = (& pp.exe here --json 2>$null)
        if ($project) { $project = $project.Trim() }
    }
    if ($project) { & pp.exe cd $project --quiet 2>$null | Out-Null }
    $session = [ordered]@{
        version = 1
        cwd = (Get-Location).Path
        project = $project
        project_path = $env:PP_PROJECT_PATH
        env_keys = @($keys)
        env_vars = $envVars
    }
    $session | ConvertTo-Json -Depth 6 | Set-Content -Path $sessionPath -Encoding UTF8
    $hookPath = Join-Path $env:LOCALAPPDATA 'ProjectPlatform\pp-hook.ps1'
    $init = ". '$($hookPath.Replace("'", "''"))'; pp-hook-restore '$($sessionPath.Replace("'", "''"))'"
    $shell = if (Get-Command pwsh -ErrorAction SilentlyContinue) { (Get-Command pwsh).Source } else { 'powershell.exe' }
    Start-Process -FilePath $shell -ArgumentList '-NoExit', '-Command', $init
    exit
}

function pp-hook-dispatch {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Args)
    if ($Args.Count -ge 1 -and $Args[0] -eq 'restart') {
        Invoke-PpRestart
        return
    }
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

function Get-PpPromptLabel {
    param([string]$Project)

    $root = Normalize-PpPath $env:PP_PROJECT_ROOT
    if (-not $root) {
        $root = (& pp.exe cd $Project --quiet 2>$null)
        if ($LASTEXITCODE -ne 0 -or -not $root) { return $Project }
        $root = Normalize-PpPath $root
        $env:PP_PROJECT_ROOT = $root
    }

    $cwd = Normalize-PpPath (Get-Location).Path
    if ($cwd.Length -le $root.Length) { return $Project }
    if (-not $cwd.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) { return $Project }

    $rel = $cwd.Substring($root.Length).TrimStart('\')
    if (-not $rel) { return $Project }
    return ($Project + '/' + ($rel -replace '\\', '/'))
}

function pp-hook-prompt {
    $project = Sync-PpProject
    if ($project) {
        $label = Get-PpPromptLabel -Project $project
        "PP:$label> "
    } else {
        "PS $(Get-Location)> "
    }
}
