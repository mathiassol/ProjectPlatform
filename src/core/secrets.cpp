#include "core/secrets.hpp"

#include "platform/platform.hpp"

namespace pp {

bool dpapiEncrypt(const std::string& plain, std::string& b64Out) {
  return platform::encryptSecret(plain, b64Out);
}

bool dpapiDecrypt(const std::string& b64In, std::string& plainOut) {
  return platform::decryptSecret(b64In, plainOut);
}

}  // namespace pp
