#pragma once

// ProjectPlatform OS seam — compile-time selected backends (win / darwin).
// Hot paths (list, env apply, prompt) stay free of runtime OS branching.

#include <filesystem>
#include <optional>
#include <string>

namespace pp {
namespace platform {

enum class Os { Windows, Darwin, Linux, Unknown };

constexpr Os currentOs() {
#if defined(_WIN32)
  return Os::Windows;
#elif defined(__APPLE__)
  return Os::Darwin;
#elif defined(__linux__)
  return Os::Linux;
#else
  return Os::Unknown;
#endif
}

constexpr bool isWindows() { return currentOs() == Os::Windows; }
constexpr bool isDarwin() { return currentOs() == Os::Darwin; }

const char* osName();

// --- Paths / process identity ---
std::filesystem::path getExePath();
std::filesystem::path getCwd();
bool setCwd(const std::filesystem::path& path);
std::filesystem::path userHomeDir();
std::filesystem::path userDocumentsDir();
std::filesystem::path appDataRoot();  // LocalAppData / Application Support / XDG

// --- Environment ---
std::optional<std::string> getEnv(const char* name);
bool setEnv(const char* name, const char* value);   // nullptr value => unset
bool unsetEnv(const char* name);

// --- Secrets (DPAPI on Win, Keychain on Darwin — Phase 0 Darwin may stub) ---
bool encryptSecret(const std::string& plain, std::string& out);
bool decryptSecret(const std::string& encrypted, std::string& plainOut);

// --- HTTP (WinHTTP / curl) ---
std::string httpGet(const std::string& url);

// --- Console ---
void initConsole();

// --- Shell helper for scripts ---
enum class ScriptKind { PowerShell, Batch, Shell };
bool runCommand(const std::string& command, const std::filesystem::path& cwd);

// Open path in default file manager / associated app
bool openPath(const std::filesystem::path& path);

}  // namespace platform
}  // namespace pp
