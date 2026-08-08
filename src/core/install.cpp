#include "core/install.hpp"
#include "core/plugins.hpp"
#include "core/automations.hpp"
#include "core/envstore.hpp"

#include "util/output.hpp"
#include "util/paths.hpp"
#include "util/progress.hpp"
#include "util/version.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <windows.h>

namespace pp {
namespace fs = std::filesystem;

std::string getExePath() {
  char buf[MAX_PATH];
  const DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
  return n ? std::string(buf, n) : std::string{};
}

static bool readUserPath(std::string& pathOut) {
  HKEY key = nullptr;
  if (RegOpenKeyExA(HKEY_CURRENT_USER, "Environment", 0, KEY_READ, &key) != ERROR_SUCCESS)
    return false;
  char buf[32768];
  DWORD size = sizeof(buf);
  DWORD type = 0;
  const LONG rc = RegQueryValueExA(key, "Path", nullptr, &type, reinterpret_cast<LPBYTE>(buf), &size);
  RegCloseKey(key);
  if (rc != ERROR_SUCCESS || type != REG_EXPAND_SZ) return false;
  pathOut.assign(buf);
  return true;
}

static bool writeUserPath(const std::string& pathValue) {
  HKEY key = nullptr;
  if (RegOpenKeyExA(HKEY_CURRENT_USER, "Environment", 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
    return false;
  const LONG rc =
      RegSetValueExA(key, "Path", 0, REG_EXPAND_SZ,
                     reinterpret_cast<const BYTE*>(pathValue.c_str()),
                     static_cast<DWORD>(pathValue.size() + 1));
  RegCloseKey(key);
  if (rc == ERROR_SUCCESS) SendMessageTimeoutA(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                                                 reinterpret_cast<LPARAM>("Environment"), SMTO_ABORTIFHUNG,
                                                 5000, nullptr);
  return rc == ERROR_SUCCESS;
}

static bool pathContains(const std::string& haystack, const std::string& needle) {
  std::string lowerHay = haystack;
  std::string lowerNeedle = needle;
  auto lower = [](std::string& s) {
    for (auto& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  };
  lower(lowerHay);
  lower(lowerNeedle);
  return lowerHay.find(lowerNeedle) != std::string::npos;
}

static bool addToUserPath(const fs::path& dir) {
  std::string current;
  if (!readUserPath(current)) current.clear();
  const auto dirStr = dir.string();
  if (pathContains(current, dirStr)) return true;
  if (!current.empty() && current.back() != ';') current += ';';
  current += dirStr;
  return writeUserPath(current);
}

bool installBinaryToPath(const fs::path& src, bool updatePath) {
  const auto destDir = installDir();
  ensureDir(destDir);
  const auto dest = destDir / "pp.exe";
  std::error_code ec;
  fs::copy_file(src, dest, fs::copy_options::overwrite_existing, ec);
  if (ec) {
    const auto staging = destDir / "pp.staging.exe";
    std::error_code ec2;
    fs::copy_file(src, staging, fs::copy_options::overwrite_existing, ec2);
    if (!ec2) {
      fs::remove(dest, ec);
      fs::rename(staging, dest, ec);
    }
    if (ec) {
      out::dim("copy failed: " + ec.message() + " (close other pp.exe terminals and retry)");
      return false;
    }
  }
  if (updatePath) return addInstallDirToPath();
  return true;
}

bool addInstallDirToPath() {
  return addToUserPath(installDir());
}

static bool launchDetachedCommand(std::string cmd) {
  STARTUPINFOA si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION pi{};
  if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS,
                      nullptr, nullptr, &si, &pi))
    return false;
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return true;
}

static bool writeDeferredCleanup(const fs::path& removePath) {
  const auto script = appDataDir() / "pp-update-cleanup.ps1";
  ensureDir(appDataDir());
  std::ofstream out(script);
  out << "param([int]$Pid, [string]$RemovePath)\n";
  out << "while (Get-Process -Id $Pid -ErrorAction SilentlyContinue) { Start-Sleep -Milliseconds 250 }\n";
  out << "Remove-Item -LiteralPath $RemovePath -Force -ErrorAction SilentlyContinue\n";
  out << "Remove-Item -LiteralPath $PSCommandPath -Force -ErrorAction SilentlyContinue\n";

  std::string cmd = "powershell.exe -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File \"" +
                    script.string() + "\" -Pid " + std::to_string(GetCurrentProcessId()) + " -RemovePath \"" +
                    removePath.string() + "\"";
  return launchDetachedCommand(std::move(cmd));
}

static bool writeDeferredReplace(const fs::path& staging, const fs::path& dest) {
  const auto script = appDataDir() / "pp-update-apply.ps1";
  ensureDir(appDataDir());
  std::ofstream out(script);
  out << "param([int]$Pid, [string]$NewPath, [string]$DestPath)\n";
  out << "while (Get-Process -Id $Pid -ErrorAction SilentlyContinue) { Start-Sleep -Milliseconds 250 }\n";
  out << "Move-Item -LiteralPath $NewPath -Destination $DestPath -Force\n";
  out << "Remove-Item -LiteralPath $PSCommandPath -Force -ErrorAction SilentlyContinue\n";

  std::string cmd = "powershell.exe -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File \"" +
                    script.string() + "\" -Pid " + std::to_string(GetCurrentProcessId()) + " -NewPath \"" +
                    staging.string() + "\" -DestPath \"" + dest.string() + "\"";
  return launchDetachedCommand(std::move(cmd));
}

bool replaceInstalledBinary(const fs::path& src, std::string& errorOut) {
  const auto destDir = installDir();
  ensureDir(destDir);
  const auto dest = destDir / "pp.exe";
  const auto backup = destDir / "pp.old.exe";
  const auto staging = destDir / "pp.new.exe";

  std::error_code ec;
  fs::remove(backup, ec);
  ec.clear();
  fs::remove(staging, ec);
  ec.clear();

  if (fs::exists(dest)) {
    fs::rename(dest, backup, ec);
    if (!ec) {
      fs::copy_file(src, dest, fs::copy_options::overwrite_existing, ec);
      if (!ec) {
        writeDeferredCleanup(backup);
        return true;
      }
      fs::rename(backup, dest, ec);
      errorOut = "copy after rename failed: " + ec.message();
    } else {
      errorOut = "rename failed (file in use?): " + ec.message();
    }
  }

  ec.clear();
  fs::copy_file(src, staging, fs::copy_options::overwrite_existing, ec);
  if (ec) {
    errorOut = "staging copy failed: " + ec.message();
    return false;
  }
  if (!writeDeferredReplace(staging, dest)) {
    errorOut = "could not launch deferred updater";
    return false;
  }
  return true;
}

static bool removeFromUserPath(const fs::path& dir) {
  std::string current;
  if (!readUserPath(current)) return true;
  const auto dirStr = dir.string();
  std::string result;
  size_t start = 0;
  while (start <= current.size()) {
    size_t end = current.find(';', start);
    if (end == std::string::npos) end = current.size();
    const auto part = current.substr(start, end - start);
    if (!part.empty() && !pathContains(part, dirStr)) {
      if (!result.empty()) result += ';';
      result += part;
    }
    start = end + 1;
  }
  return writeUserPath(result);
}

struct PathRollback {
  std::string previous;
  bool hadEntry = false;
  bool removed = false;
};

static bool removeFromUserPathWithRollback(const fs::path& dir, PathRollback& rollback) {
  if (!readUserPath(rollback.previous)) rollback.previous.clear();
  rollback.hadEntry = pathContains(rollback.previous, dir.string());
  if (!rollback.hadEntry) return true;
  if (!removeFromUserPath(dir)) return false;
  rollback.removed = true;
  return true;
}

static void restoreUserPath(const PathRollback& rollback) {
  if (rollback.removed && rollback.hadEntry) writeUserPath(rollback.previous);
}

static bool pathsEqualInsensitive(const fs::path& a, const fs::path& b) {
  std::error_code ec;
  const auto ca = fs::weakly_canonical(a, ec);
  if (ec) return false;
  const auto cb = fs::weakly_canonical(b, ec);
  if (ec) return false;
  return ca == cb;
}

static bool runningFromInstallDir() {
  const auto exe = getExePath();
  if (exe.empty()) return false;
  std::error_code ec;
  const auto exeDir = fs::path(exe).parent_path();
  return pathsEqualInsensitive(exeDir, installDir());
}

static bool writeDeferredUninstall(const fs::path& appData, const fs::path& binDir) {
  std::error_code ec;
  const auto script = fs::temp_directory_path(ec) / "pp-uninstall-cleanup.ps1";
  if (ec) return false;

  std::ofstream out(script);
  if (!out) return false;
  out << "$ErrorActionPreference = 'SilentlyContinue'\n";
  out << "param([int]$Pid, [string]$AppData, [string]$BinDir)\n";
  out << "while (Get-Process -Id $Pid -ErrorAction SilentlyContinue) { Start-Sleep -Milliseconds 250 }\n";
  out << "$userPath = [Environment]::GetEnvironmentVariable('Path', 'User')\n";
  out << "if ($userPath) {\n";
  out << "  $bin = $BinDir.ToLower()\n";
  out << "  $parts = @($userPath -split ';' | Where-Object {\n";
  out << "    $_ -and ($_.ToLower().Replace('/', '\\') -notlike ('*' + $bin + '*'))\n";
  out << "  })\n";
  out << "  [Environment]::SetEnvironmentVariable('Path', ($parts -join ';'), 'User')\n";
  out << "}\n";
  out << "Remove-Item -LiteralPath $AppData -Recurse -Force -ErrorAction SilentlyContinue\n";
  out << "Remove-Item -LiteralPath $PSCommandPath -Force -ErrorAction SilentlyContinue\n";

  std::string cmd = "powershell.exe -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File \"" +
                    script.string() + "\" -Pid " + std::to_string(GetCurrentProcessId()) +
                    " -AppData \"" + appData.string() + "\" -BinDir \"" + binDir.string() + "\"";
  return launchDetachedCommand(std::move(cmd));
}

static bool removeAppDataTree(std::string& errorOut) {
  std::error_code ec;
  fs::remove_all(appDataDir(), ec);
  if (ec) {
    errorOut = ec.message();
    return false;
  }
  return true;
}

static bool finishScheduledUninstall(Progress& progress) {
  progress.done("ProjectPlatform uninstall scheduled");
  out::blank();
  out::info("Close this terminal to finish removal (PATH + AppData).");
  out::dim("Your projects and templates in Documents were not deleted.");
  out::dim("If cleanup does not finish, run: pp uninstall");
  return true;
}

static std::string psSingleQuote(const std::string& s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') out += "''";
    else out.push_back(c);
  }
  out += "'";
  return out;
}

static std::string hookProfileBlock() {
  std::ostringstream o;
  o << "# ProjectPlatform hook\n";
  o << "$PP_HOOK_PATH = " << psSingleQuote(hookScriptPath().string()) << "\n";
  o << "function pp {\n";
  o << "    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Args)\n";
  o << "    . $PP_HOOK_PATH\n";
  o << "    pp-hook-dispatch @Args\n";
  o << "}\n";
  o << "function prompt {\n";
  o << "    . $PP_HOOK_PATH\n";
  o << "    pp-hook-prompt\n";
  o << "}\n";
  o << "# End ProjectPlatform hook\n";
  return o.str();
}

static bool writeHookScript() {
  ensureDir(appDataDir());
  std::ofstream out(hookScriptPath());
  out << "# ProjectPlatform shell hook - auto-generated\n";
  out << "# PP_HOOK_VERSION=" << PP_APP_VERSION << "\n";
  out << R"PPHOOK(# Loaded via $PROFILE wrapper (re-sources this file each call).

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

function pp-hook-prompt {
    $project = Sync-PpProject
    if ($project) {
        "PP:$project> "
    } else {
        "PS $(Get-Location)> "
    }
}
)PPHOOK";
  return static_cast<bool>(out);
}

bool installSelf() {
  Progress progress("install");
  const auto src = getExePath();
  if (src.empty()) {
    out::error("could not locate executable");
    return false;
  }

  progress.step("copying to " + (installDir() / "pp.exe").string());
  if (!installBinaryToPath(src, true)) {
    out::error("failed to install");
    return false;
  }

  ensureHookScriptFresh();
  installBundledPlugins();
  installBundledAutomations();
  installEnvTemplates();

  progress.done(std::string("ProjectPlatform ") + PP_APP_VERSION + " installed to " + installDir().string());
  out::blank();
  out::info("Restart your terminal, then run:  pp list");
  out::dim("Optional — enable in-terminal project jumping:");
  out::dim("  pp hook install");
  out::dim("Shell hook is off by default. Enable it only if you want pp cd to change directory.");
  return true;
}

bool uninstallSelf() {
  Progress progress("uninstall");
  const auto dataDir = appDataDir();
  const auto binDir = installDir();
  PathRollback pathRollback;

  progress.step("removing shell hook from PowerShell profiles");
  uninstallHook();

  if (runningFromInstallDir()) {
    progress.step("scheduling AppData + PATH cleanup");
    if (!writeDeferredUninstall(dataDir, binDir)) {
      out::error("could not schedule uninstall cleanup");
      out::dim("Close other pp.exe windows and run: pp uninstall");
      return false;
    }
    return finishScheduledUninstall(progress);
  }

  progress.step("removing " + dataDir.string());
  std::string removeError;
  if (!removeAppDataTree(removeError)) {
    progress.step("scheduling deferred cleanup");
    if (writeDeferredUninstall(dataDir, binDir)) return finishScheduledUninstall(progress);
    out::error("failed to remove install data: " + removeError);
    out::dim("Close other pp.exe windows and run: pp uninstall");
    return false;
  }

  progress.step("removing from user PATH");
  if (!removeFromUserPathWithRollback(binDir, pathRollback)) {
    out::error("failed to update PATH");
    return false;
  }

  progress.done("ProjectPlatform uninstalled");
  out::blank();
  out::info("Restart your terminal to clear PATH changes.");
  out::dim("Your projects and templates in Documents were not deleted.");
  return true;
}

static fs::path profilePath(bool pwsh7) {
  const char* home = std::getenv("USERPROFILE");
  if (!home) return {};
  if (pwsh7)
    return fs::path(home) / "Documents" / "PowerShell" / "Microsoft.PowerShell_profile.ps1";
  return fs::path(home) / "Documents" / "WindowsPowerShell" / "Microsoft.PowerShell_profile.ps1";
}

static void appendHookToProfile(const fs::path& profile) {
  ensureDir(profile.parent_path());
  const std::string marker = "# ProjectPlatform hook";
  std::string existing;
  if (fs::exists(profile)) {
    std::ifstream in(profile);
    existing.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (existing.find(marker) != std::string::npos) return;
  }
  std::ofstream out(profile, std::ios::app);
  out << "\n" << hookProfileBlock();
}

static bool migrateHookProfile(const fs::path& profile) {
  if (!fs::exists(profile)) return false;
  std::ifstream in(profile);
  std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const auto marker = "# ProjectPlatform hook";
  const auto pos = content.find(marker);
  if (pos == std::string::npos) return false;
  if (content.find("$PP_HOOK_PATH") != std::string::npos &&
      content.find("pp-hook-dispatch") != std::string::npos)
    return false;

  const std::string endMarker = "# End ProjectPlatform hook";
  const auto end = content.find(endMarker, pos);
  if (end != std::string::npos) {
    size_t eraseEnd = end + endMarker.size();
    while (eraseEnd < content.size() && (content[eraseEnd] == '\r' || content[eraseEnd] == '\n'))
      ++eraseEnd;
    content.erase(pos, eraseEnd - pos);
  } else {
    content.erase(pos);
  }
  while (!content.empty() && (content.back() == '\n' || content.back() == '\r')) content.pop_back();
  content += "\n\n";
  content += hookProfileBlock();
  std::ofstream out(profile);
  out << content;
  return true;
}

static bool migrateHookProfiles() {
  bool changed = false;
  if (migrateHookProfile(profilePath(true))) changed = true;
  if (migrateHookProfile(profilePath(false))) changed = true;
  return changed;
}

bool refreshHookScript() {
  writeHookScript();
  migrateHookProfiles();
  return true;
}

bool ensureHookScriptFresh() {
  const auto path = hookScriptPath();
  const std::string verLine = std::string("# PP_HOOK_VERSION=") + PP_APP_VERSION;
  bool hookUpdated = false;
  bool profileUpdated = false;

  if (!fs::exists(path)) {
    hookUpdated = writeHookScript();
  } else {
    std::ifstream in(path);
    const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (content.find("Invoke-PpScript") != std::string::npos ||
        content.find("pp-hook-dispatch") == std::string::npos ||
        content.find("pp-hook-restore") == std::string::npos ||
        content.find(verLine) == std::string::npos) {
      hookUpdated = writeHookScript();
    }
  }

  profileUpdated = migrateHookProfiles();

  if (hookUpdated || profileUpdated) {
    out::dim("[pp] Shell hook updated — reload this session: . $PROFILE");
  }
  return true;
}

bool installHook() {
  writeHookScript();
  migrateHookProfile(profilePath(true));
  migrateHookProfile(profilePath(false));
  appendHookToProfile(profilePath(true));
  appendHookToProfile(profilePath(false));
  out::success("hook installed");
  out::dim("Reload this session:");
  out::dim("  . $PROFILE");
  out::dim("Or open a new PowerShell window.");
  return true;
}

static void stripHookFromProfile(const fs::path& profile) {
  if (!fs::exists(profile)) return;
  std::ifstream in(profile);
  std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const auto pos = content.find("# ProjectPlatform hook");
  if (pos == std::string::npos) return;
  const std::string endMarker = "# End ProjectPlatform hook";
  const auto end = content.find(endMarker, pos);
  if (end != std::string::npos) {
    size_t eraseEnd = end + endMarker.size();
    while (eraseEnd < content.size() && (content[eraseEnd] == '\r' || content[eraseEnd] == '\n'))
      ++eraseEnd;
    content.erase(pos, eraseEnd - pos);
  } else {
    content.erase(pos);
  }
  std::ofstream out(profile);
  out << content;
}

bool uninstallHook() {
  stripHookFromProfile(profilePath(true));
  stripHookFromProfile(profilePath(false));
  out::success("hook removed from profiles");
  return true;
}

}  // namespace pp
