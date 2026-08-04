#pragma once

#include <optional>
#include <string>

namespace pp {

bool dpapiEncrypt(const std::string& plain, std::string& b64Out);
bool dpapiDecrypt(const std::string& b64In, std::string& plainOut);

}  // namespace pp
