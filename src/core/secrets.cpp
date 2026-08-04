#include "core/secrets.hpp"

#include <vector>
#include <windows.h>
#include <wincrypt.h>

namespace pp {

static std::string base64Encode(const BYTE* data, DWORD len) {
  static const char* k =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((len + 2) / 3 * 4);
  for (DWORD i = 0; i < len; i += 3) {
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

static bool base64Decode(const std::string& in, std::vector<BYTE>& out) {
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
    out.push_back(static_cast<BYTE>((a << 2) | (b >> 4)));
    if (clean[i + 2] != '=') out.push_back(static_cast<BYTE>(((b & 15) << 4) | (c >> 2)));
    if (clean[i + 3] != '=') out.push_back(static_cast<BYTE>(((c & 3) << 6) | d));
  }
  return true;
}

bool dpapiEncrypt(const std::string& plain, std::string& b64Out) {
  DATA_BLOB in{};
  in.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()));
  in.cbData = static_cast<DWORD>(plain.size());
  DATA_BLOB out{};
  if (!CryptProtectData(&in, L"ProjectPlatform", nullptr, nullptr, nullptr, 0, &out))
    return false;
  b64Out = base64Encode(out.pbData, out.cbData);
  LocalFree(out.pbData);
  return true;
}

bool dpapiDecrypt(const std::string& b64In, std::string& plainOut) {
  std::vector<BYTE> raw;
  if (!base64Decode(b64In, raw)) return false;
  DATA_BLOB in{};
  in.pbData = raw.data();
  in.cbData = static_cast<DWORD>(raw.size());
  DATA_BLOB out{};
  if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out)) return false;
  plainOut.assign(reinterpret_cast<char*>(out.pbData), out.cbData);
  LocalFree(out.pbData);
  return true;
}

}  // namespace pp
