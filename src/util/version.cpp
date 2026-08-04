#include "util/version.hpp"

#include <sstream>

namespace pp {

int compareVersions(const std::string& a, const std::string& b) {
  auto nextPart = [](const std::string& s, size_t& i) -> int {
    int n = 0;
    while (i < s.size() && s[i] != '.') {
      if (s[i] >= '0' && s[i] <= '9') n = n * 10 + (s[i] - '0');
      ++i;
    }
    if (i < s.size() && s[i] == '.') ++i;
    return n;
  };
  size_t ia = 0, ib = 0;
  for (int part = 0; part < 3; ++part) {
    const int va = nextPart(a, ia);
    const int vb = nextPart(b, ib);
    if (va != vb) return va < vb ? -1 : 1;
  }
  return 0;
}

}  // namespace pp
