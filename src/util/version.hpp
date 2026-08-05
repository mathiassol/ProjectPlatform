#pragma once

#include <string>

namespace pp {

#define PP_APP_VERSION "1.1.1"
#define PP_GITHUB_REPO "mathiassol/ProjectPlatform"

int compareVersions(const std::string& a, const std::string& b);

}  // namespace pp
