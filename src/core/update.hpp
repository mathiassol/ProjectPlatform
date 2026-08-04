#pragma once

#include <optional>
#include <string>

namespace pp {

struct ReleaseInfo {
  std::string version;
  std::string tag;
  std::string zip_url;
  std::string html_url;
};

std::optional<ReleaseInfo> fetchLatestRelease();
bool checkUpdate(bool quiet);
bool performUpdate(bool force);

}  // namespace pp
