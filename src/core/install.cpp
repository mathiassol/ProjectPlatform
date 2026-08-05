#include "core/install.hpp"

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
  if (ec) return false;
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

static bool writeHookScript() {
  ensureDir(appDataDir());
  std::ofstream out(hookScriptPath());
  out << R"(# ProjectPlatform shell hook - auto-generated
# Makes `pp cd`, `pp goto`, `pp enter` actually change directory.

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

# Alias for users who prefer a dedicated jump command
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
)";
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

  progress.done(std::string("ProjectPlatform ") + PP_APP_VERSION + " installed to " + installDir().string());
  out::blank();
  out::info("Restart your terminal, then run:  pp list");
  out::dim("Optional — enable in-terminal project jumping:");
  out::dim("  pp hook install");
  out::dim("Shell hook is off by default. Enable it only if you want pp cd to change directory.");
  return true;
}

bool uninstallSelf() {
  const auto destDir = installDir();
  removeFromUserPath(destDir);
  std::error_code ec;
  fs::remove_all(appDataDir(), ec);
  out::success("ProjectPlatform uninstalled (restart terminal)");
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
  out << "\n" << marker << "\n";
  out << ". \"" << hookScriptPath().string() << "\"\n";
}

bool refreshHookScript() { return writeHookScript(); }

bool installHook() {
  writeHookScript();
  appendHookToProfile(profilePath(true));
  appendHookToProfile(profilePath(false));
  out::success("hook installed — restart PowerShell, then use: pp cd <name>");
  return true;
}

static void stripHookFromProfile(const fs::path& profile) {
  if (!fs::exists(profile)) return;
  std::ifstream in(profile);
  std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const auto pos = content.find("# ProjectPlatform hook");
  if (pos == std::string::npos) return;
  content.erase(pos);
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
