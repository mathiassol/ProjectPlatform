#include "util/editor.hpp"

#include "platform/platform.hpp"
#include "util/output.hpp"
#include "util/paths.hpp"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>

#if defined(__APPLE__)
#include <limits.h>
#endif

namespace pp {
namespace fs = std::filesystem;

namespace {

bool fileExists(const fs::path& p) {
  std::error_code ec;
  return fs::is_regular_file(p, ec);
}

std::string shellSingleQuote(const std::string& s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'')
      out += "'\\''";
    else
      out += c;
  }
  out += "'";
  return out;
}

std::optional<fs::path> whichOnPath(const char* name) {
  const std::string cmd = std::string("command -v ") + name;
  std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
  if (!pipe) return std::nullopt;
  char buf[PATH_MAX];
  if (!fgets(buf, sizeof(buf), pipe.get())) return std::nullopt;
  std::string line(buf);
  while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
  if (line.empty()) return std::nullopt;
  if (fileExists(line)) return fs::path(line);
  return std::nullopt;
}

std::optional<fs::path> resolveConfiguredEditor() {
  const auto cfg = loadConfig();
  if (cfg.editor.empty()) return std::nullopt;
  if (cfg.editor == "zed" || cfg.editor == "Zed") return findZedExecutable();
  const fs::path p = expandEnv(cfg.editor);
  if (fileExists(p)) return p;
  if (auto found = whichOnPath(cfg.editor.c_str())) return *found;
  return std::nullopt;
}

bool launchEditor(const fs::path& editor, const fs::path& file) {
  const std::string cmd =
      shellSingleQuote(editor.string()) + " " + shellSingleQuote(file.string()) + " >/dev/null 2>&1 &";
  return std::system(cmd.c_str()) == 0;
}

bool dutiAvailable() { return static_cast<bool>(whichOnPath("duti")); }

bool associateWithDuti(const std::string& utiOrExt, const std::string& bundleId) {
  // duti -s bundleId extension all
  const std::string cmd =
      "duti -s " + shellSingleQuote(bundleId) + " " + shellSingleQuote(utiOrExt) + " all >/dev/null 2>&1";
  return std::system(cmd.c_str()) == 0;
}

}  // namespace

std::optional<fs::path> findZedExecutable() {
  if (auto p = whichOnPath("zed")) return *p;
  for (const char* cand : {"/opt/homebrew/bin/zed", "/usr/local/bin/zed",
                           "/Applications/Zed.app/Contents/MacOS/cli",
                           "/Applications/Zed.app/Contents/MacOS/Zed"}) {
    if (fileExists(cand)) return fs::path(cand);
  }
  if (auto home = platform::userHomeDir(); !home.empty()) {
    const auto app = home / "Applications" / "Zed.app" / "Contents" / "MacOS" / "cli";
    if (fileExists(app)) return app;
  }
  return std::nullopt;
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
    out::warn("configured editor failed, trying fallbacks");
  }

  if (auto zed = findZedExecutable()) {
    if (launchEditor(*zed, file)) {
      out::success("opened in zed: " + file.filename().string());
      return true;
    }
  }

  // open -a Zed / system default
  {
    const std::string zedApp =
        "open -a Zed " + shellSingleQuote(file.string()) + " >/dev/null 2>&1";
    if (std::system(zedApp.c_str()) == 0) {
      out::success("opened in Zed: " + file.filename().string());
      return true;
    }
  }

  if (platform::openPath(file)) {
    out::success("opened: " + file.filename().string());
    return true;
  }
  out::error("could not open " + file.string());
  out::dim("Set an editor: pp editor setup  (brew install --cask zed)");
  return false;
}

bool configureZedAsDefaultEditor() {
  const auto zed = findZedExecutable();
  if (!zed) {
    out::error("Zed not found");
    out::dim("Install: brew install --cask zed");
    return false;
  }

  auto cfg = loadConfig();
  cfg.editor = "zed";
  saveConfig(cfg);

  out::success("PP editor set to zed (" + zed->string() + ")");
  out::dim("pp script edit / pp env edit will open in Zed");

  // Optional system associations via duti (Homebrew).
  if (dutiAvailable()) {
    static const char* kExts[] = {".sh", ".bash", ".zsh", ".env", ".envrc", ".ps1"};
    int ok = 0;
    for (const char* ext : kExts) {
      if (associateWithDuti(ext, "dev.zed.Zed")) {
        out::dim(std::string("  ") + ext + " -> Zed (duti)");
        ++ok;
      }
    }
    if (ok == 0) out::dim("duti present but associations skipped (UTI/bundle may differ)");
  } else {
    out::dim("Optional system associations: brew install duti && pp editor setup");
  }
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
    out::warn("Zed: not found (brew install --cask zed)");

  out::blank();
  out::dim(dutiAvailable() ? "duti: available (used for optional file associations)"
                           : "duti: not installed (optional)");
}

}  // namespace pp
