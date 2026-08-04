#pragma once

#include <string>

namespace pp {

bool installSelf();
bool uninstallSelf();
bool installHook();
bool uninstallHook();
std::string getExePath();

}  // namespace pp
