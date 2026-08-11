#include "platform/platform.hpp"

#include <CommonCrypto/CommonCryptor.h>
#include <CommonCrypto/CommonRandom.h>
#include <Security/Security.h>
#include <mach-o/dyld.h>
#include <unistd.h>
#include <limits.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace pp {
namespace platform {
namespace {

constexpr const char* kKeychainService = "com.projectplatform.secrets";
constexpr const char* kKeychainAccount = "aes-master-key-v1";
constexpr size_t kKeyLen = 32;
constexpr size_t kIvLen = kCCBlockSizeAES128;

std::string base64Encode(const unsigned char* data, size_t len) {
  static const char* k =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((len + 2) / 3 * 4);
  for (size_t i = 0; i < len; i += 3) {
    const unsigned a = data[i];
    const unsigned b = i + 1 < len ? data[i + 1] : 0;
    const unsigned c = i + 2 < len ? data[i + 2] : 0;
    out.push_back(k[a >> 2]);
    out.push_back(k[((a & 3) << 4) | (b >> 4)]);
    out.push_back(i + 1 < len ? k[((b & 15) << 2) | (c >> 6)] : '=');
    out.push_back(i + 2 < len ? k[c & 63] : '=');
  }
  return out;
}

bool base64Decode(const std::string& in, std::vector<unsigned char>& out) {
  auto val = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  std::string clean;
  for (char c : in) {
    if (c == '=' || val(c) >= 0) clean.push_back(c);
  }
  if (clean.size() % 4 != 0) return false;
  out.clear();
  for (size_t i = 0; i < clean.size(); i += 4) {
    const int a = val(clean[i]);
    const int b = val(clean[i + 1]);
    const int c = clean[i + 2] == '=' ? 0 : val(clean[i + 2]);
    const int d = clean[i + 3] == '=' ? 0 : val(clean[i + 3]);
    if (a < 0 || b < 0) return false;
    out.push_back(static_cast<unsigned char>((a << 2) | (b >> 4)));
    if (clean[i + 2] != '=') out.push_back(static_cast<unsigned char>(((b & 15) << 4) | (c >> 2)));
    if (clean[i + 3] != '=') out.push_back(static_cast<unsigned char>(((c & 3) << 6) | d));
  }
  return true;
}

bool loadOrCreateMasterKey(std::vector<unsigned char>& keyOut) {
  keyOut.assign(kKeyLen, 0);

  CFStringRef service = CFStringCreateWithCString(nullptr, kKeychainService, kCFStringEncodingUTF8);
  CFStringRef account = CFStringCreateWithCString(nullptr, kKeychainAccount, kCFStringEncodingUTF8);

  const void* keys[] = {kSecClass, kSecAttrService, kSecAttrAccount, kSecReturnData, kSecMatchLimit};
  const void* vals[] = {kSecClassGenericPassword, service, account, kCFBooleanTrue, kSecMatchLimitOne};
  CFDictionaryRef query =
      CFDictionaryCreate(nullptr, keys, vals, 5, &kCFTypeDictionaryKeyCallBacks,
                         &kCFTypeDictionaryValueCallBacks);

  CFTypeRef result = nullptr;
  OSStatus st = SecItemCopyMatching(query, &result);
  CFRelease(query);

  if (st == errSecSuccess && result) {
    CFDataRef data = static_cast<CFDataRef>(result);
    if (CFDataGetLength(data) == static_cast<CFIndex>(kKeyLen)) {
      std::memcpy(keyOut.data(), CFDataGetBytePtr(data), kKeyLen);
      CFRelease(result);
      CFRelease(service);
      CFRelease(account);
      return true;
    }
    CFRelease(result);
  }

  // Create new key
  if (CCRandomGenerateBytes(keyOut.data(), kKeyLen) != kCCSuccess) {
    CFRelease(service);
    CFRelease(account);
    return false;
  }

  CFDataRef keyData = CFDataCreate(nullptr, keyOut.data(), static_cast<CFIndex>(kKeyLen));
  const void* akeys[] = {kSecClass, kSecAttrService, kSecAttrAccount, kSecValueData,
                         kSecAttrAccessible};
  const void* avals[] = {kSecClassGenericPassword, service, account, keyData,
                         kSecAttrAccessibleWhenUnlocked};
  CFDictionaryRef add = CFDictionaryCreate(nullptr, akeys, avals, 5, &kCFTypeDictionaryKeyCallBacks,
                                           &kCFTypeDictionaryValueCallBacks);
  st = SecItemAdd(add, nullptr);
  CFRelease(add);
  CFRelease(keyData);
  CFRelease(service);
  CFRelease(account);
  return st == errSecSuccess || st == errSecDuplicateItem;
}

}  // namespace

const char* osName() { return "macos"; }

std::filesystem::path getExePath() {
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  if (size == 0) return {};
  std::string buf(size, '\0');
  if (_NSGetExecutablePath(buf.data(), &size) != 0) return {};
  buf.resize(std::char_traits<char>::length(buf.c_str()));
  std::error_code ec;
  auto p = std::filesystem::weakly_canonical(buf, ec);
  return ec ? std::filesystem::path(buf) : p;
}

std::filesystem::path getCwd() {
  char buf[PATH_MAX];
  if (!getcwd(buf, sizeof(buf))) return {};
  return std::filesystem::path(buf);
}

bool setCwd(const std::filesystem::path& path) { return ::chdir(path.c_str()) == 0; }

std::filesystem::path userHomeDir() {
  if (const char* v = std::getenv("HOME")) return std::filesystem::path(v);
  return {};
}

std::filesystem::path userDocumentsDir() {
  const auto home = userHomeDir();
  if (home.empty()) return std::filesystem::path("Documents");
  return home / "Documents";
}

std::filesystem::path appDataRoot() {
  const auto home = userHomeDir();
  if (home.empty()) return std::filesystem::path("ProjectPlatform");
  return home / "Library" / "Application Support" / "ProjectPlatform";
}

std::optional<std::string> getEnv(const char* name) {
  const char* v = std::getenv(name);
  if (!v) return std::nullopt;
  return std::string(v);
}

bool setEnv(const char* name, const char* value) {
  if (!value) return unsetEnv(name);
  return ::setenv(name, value, 1) == 0;
}

bool unsetEnv(const char* name) { return ::unsetenv(name) == 0; }

bool encryptSecret(const std::string& plain, std::string& out) {
  std::vector<unsigned char> key;
  if (!loadOrCreateMasterKey(key)) return false;

  unsigned char iv[kIvLen];
  if (CCRandomGenerateBytes(iv, kIvLen) != kCCSuccess) return false;

  size_t outLen = plain.size() + kCCBlockSizeAES128;
  std::vector<unsigned char> cipher(outLen);
  size_t moved = 0;
  const CCCryptorStatus st =
      CCCrypt(kCCEncrypt, kCCAlgorithmAES, kCCOptionPKCS7Padding, key.data(), key.size(), iv,
              plain.data(), plain.size(), cipher.data(), cipher.size(), &moved);
  if (st != kCCSuccess) return false;
  cipher.resize(moved);

  std::vector<unsigned char> packed;
  packed.reserve(kIvLen + cipher.size());
  packed.insert(packed.end(), iv, iv + kIvLen);
  packed.insert(packed.end(), cipher.begin(), cipher.end());
  out = base64Encode(packed.data(), packed.size());
  return true;
}

bool decryptSecret(const std::string& encrypted, std::string& plainOut) {
  std::vector<unsigned char> key;
  if (!loadOrCreateMasterKey(key)) return false;

  std::vector<unsigned char> packed;
  if (!base64Decode(encrypted, packed) || packed.size() <= kIvLen) return false;

  unsigned char iv[kIvLen];
  std::memcpy(iv, packed.data(), kIvLen);
  const unsigned char* cipher = packed.data() + kIvLen;
  const size_t cipherLen = packed.size() - kIvLen;

  std::vector<unsigned char> plain(cipherLen + kCCBlockSizeAES128);
  size_t moved = 0;
  const CCCryptorStatus st =
      CCCrypt(kCCDecrypt, kCCAlgorithmAES, kCCOptionPKCS7Padding, key.data(), key.size(), iv, cipher,
              cipherLen, plain.data(), plain.size(), &moved);
  if (st != kCCSuccess) return false;
  plainOut.assign(reinterpret_cast<char*>(plain.data()), moved);
  return true;
}

std::string httpGet(const std::string& url) {
  auto shellQuote = [](const std::string& s) {
    std::string out = "'";
    for (char c : s) {
      if (c == '\'')
        out += "'\\''";
      else
        out += c;
    }
    out += "'";
    return out;
  };
  const std::string cmd =
      "curl -fsSL --max-time 60 -A ProjectPlatform " + shellQuote(url);
  std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
  if (!pipe) return {};
  std::string body;
  std::array<char, 4096> buf{};
  while (fgets(buf.data(), static_cast<int>(buf.size()), pipe.get())) body += buf.data();
  return body;
}

void initConsole() {}

bool runCommand(const std::string& command, const std::filesystem::path& cwd) {
  std::filesystem::path prev;
  if (!cwd.empty()) {
    prev = getCwd();
    if (!setCwd(cwd)) return false;
  }
  const int code = std::system(command.c_str());
  if (!cwd.empty() && !prev.empty()) setCwd(prev);
  return code == 0;
}

bool openPath(const std::filesystem::path& path) {
  const std::string cmd = "open \"" + path.string() + "\"";
  return std::system(cmd.c_str()) == 0;
}

}  // namespace platform
}  // namespace pp
