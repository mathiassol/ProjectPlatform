#include "util/editor.hpp"

#include "util/output.hpp"
#include "util/paths.hpp"

#include <cstdlib>
#include <fstream>
#include <shlobj.h>
#include <windows.h>

namespace pp {
namespace fs = std::filesystem;

static bool fileExists(const fs::path& p) {
  std::error_code ec;
  return fs::is_regular_file(p, ec);
}

static std::optional<fs::path> whichOnPath(const std::string& name) {
  char buf[MAX_PATH];
  const DWORD n = SearchPathA(nullptr, name.c_str(), ".exe", MAX_PATH, buf, nullptr);
  if (n == 0 || n >= MAX_PATH) return std::nullopt;
  return fs::path(buf);
}

std::optional<fs::path> findZedExecutable() {
  if (auto p = whichOnPath("zed")) return *p;
  const char* local = std::getenv("LOCALAPPDATA");
  if (!local) return std::nullopt;
  const fs::path base = fs::path(local) / "Programs" / "Zed";
  for (const auto& candidate :
       {base / "bin" / "zed.exe", base / "Zed.exe", base / "zed.exe"}) {
    if (fileExists(candidate)) return candidate;
  }
  return std::nullopt;
}

static std::optional<fs::path> resolveConfiguredEditor() {
  const auto cfg = loadConfig();
  if (cfg.editor.empty()) return std::nullopt;
  if (cfg.editor == "zed" || cfg.editor == "Zed") return findZedExecutable();
  const fs::path p = expandEnv(cfg.editor);
  if (fileExists(p)) return p;
  if (auto found = whichOnPath(cfg.editor)) return *found;
  return std::nullopt;
}

static bool launchEditor(const fs::path& editor, const fs::path& file) {
  const std::string args = "\"" + file.string() + "\"";
  HINSTANCE r = ShellExecuteA(nullptr, "open", editor.string().c_str(), args.c_str(), nullptr,
                              SW_SHOWNORMAL);
  return reinterpret_cast<intptr_t>(r) > 32;
}

bool openInEditor(const fs::path& file) {
  if (!fileExists(file)) {
    out::error("file not found: " + file.string());
    return false;
  }

  if (auto editor = resolveConfiguredEditor()) {
    if (launchEditor(*editor, file)) {
      out::success("opened in " + editor->filename().string() + ": " + file.filename().string());
      return true;
    }
    out::warn("configured editor failed, trying system default");
  }

  HINSTANCE r =
      ShellExecuteA(nullptr, "open", file.string().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  if (reinterpret_cast<intptr_t>(r) <= 32) {
    out::error("could not open " + file.string());
    out::dim("Set an editor: pp editor setup");
    return false;
  }
  out::success("opened: " + file.filename().string());
  return true;
}

static bool setRegistryString(HKEY root, const std::string& subKey, const std::string& valueName,
                              const std::string& data) {
  HKEY key = nullptr;
  if (RegCreateKeyExA(root, subKey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr,
                      &key, nullptr) != ERROR_SUCCESS)
    return false;
  const LSTATUS st = RegSetValueExA(key, valueName.empty() ? nullptr : valueName.c_str(), 0,
                                    REG_SZ, reinterpret_cast<const BYTE*>(data.c_str()),
                                    static_cast<DWORD>(data.size() + 1));
  RegCloseKey(key);
  return st == ERROR_SUCCESS;
}

static bool associateExtensionWithZed(const std::string& ext, const fs::path& zedPath) {
  const std::string progId = "Zed" + ext;
  const std::string openKey = "Software\\Classes\\" + progId + "\\shell\\open\\command";
  const std::string command = "\"" + zedPath.string() + "\" \"%1\"";

  if (!setRegistryString(HKEY_CURRENT_USER, "Software\\Classes\\" + ext, "", progId)) return false;
  if (!setRegistryString(HKEY_CURRENT_USER, "Software\\Classes\\" + progId, "", "Zed")) return false;
  if (!setRegistryString(HKEY_CURRENT_USER, openKey, "", command)) return false;
  return true;
}

bool configureZedAsDefaultEditor() {
  const auto zed = findZedExecutable();
  if (!zed) {
    out::error("Zed not found");
    out::dim("Install Zed from https://zed.dev or add zed.exe to PATH");
    return false;
  }

  static const char* kExts[] = {".ps1", ".bat", ".cmd", ".env", ".envrc"};
  int ok = 0;
  for (const char* ext : kExts) {
    if (associateExtensionWithZed(ext, *zed)) {
      out::dim(std::string("  ") + ext + " -> Zed");
      ++ok;
    } else {
      out::warn(std::string("could not register ") + ext);
    }
  }

  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

  auto cfg = loadConfig();
  cfg.editor = "zed";
  saveConfig(cfg);

  if (ok == 0) {
    out::error("no file associations were updated");
    return false;
  }

  out::success("Zed set as default editor for script/env files");
  out::dim("PP config editor: zed (" + zed->string() + ")");
  out::dim("pp script edit <name> will open files in Zed");
  return true;
}

void showEditorStatus() {
  const auto cfg = loadConfig();
  if (cfg.editor.empty())
    out::dim("PP editor: (system default)");
  else
    out::info("PP editor: " + cfg.editor);

  if (auto zed = findZedExecutable())
    out::dim("Zed: " + zed->string());
  else
    out::warn("Zed: not found");

  out::blank();
  out::info("Windows associations (HKCU):");
  static const char* kExts[] = {".ps1", ".bat", ".cmd", ".env", ".envrc"};
  for (const char* ext : kExts) {
    char buf[256] = {};
    DWORD n = static_cast<DWORD>(sizeof(buf));
    const LSTATUS st =
        RegGetValueA(HKEY_CURRENT_USER, ("Software\\Classes\\" + std::string(ext)).c_str(), nullptr,
                     RRF_RT_REG_SZ, nullptr, buf, &n);
    if (st == ERROR_SUCCESS) {
      const std::string progId = buf;
      const bool isZed = progId.rfind("Zed", 0) == 0;
      std::cout << "  " << ext << " -> " << progId << (isZed ? " (Zed)" : "") << "\n";
    } else {
      std::cout << "  " << ext << " -> (not set in HKCU)\n";
    }
  }
}

}  // namespace pp
