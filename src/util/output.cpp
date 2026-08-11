#include "util/output.hpp"

#include "platform/platform.hpp"

namespace pp {
namespace out {

static bool g_vt = true;

void initConsole() {
  platform::initConsole();
#if defined(_WIN32)
  // VT may still be off if console mode failed; keep colors opportunistic.
  g_vt = true;
#else
  g_vt = true;
#endif
}

static const char* code(Color c) {
  if (!g_vt) return "";
  switch (c) {
    case Color::Reset: return "\033[0m";
    case Color::Red: return "\033[31m";
    case Color::Green: return "\033[32m";
    case Color::Yellow: return "\033[33m";
    case Color::Cyan: return "\033[36m";
    case Color::Dim: return "\033[2m";
    case Color::Bold: return "\033[1m";
  }
  return "";
}

std::ostream& stream(Color c) {
  std::cout << code(c);
  return std::cout;
}

void info(const std::string& msg) { stream(Color::Cyan) << msg << code(Color::Reset) << "\n"; }

void success(const std::string& msg) {
  stream(Color::Green) << msg << code(Color::Reset) << "\n";
}

void warn(const std::string& msg) {
  stream(Color::Yellow) << "warning: " << msg << code(Color::Reset) << "\n";
}

void error(const std::string& msg) {
  stream(Color::Red) << "error: " << msg << code(Color::Reset) << "\n";
}

void dim(const std::string& msg) { stream(Color::Dim) << msg << code(Color::Reset) << "\n"; }

void title(const std::string& msg) {
  stream(Color::Bold) << msg << code(Color::Reset) << "\n";
}

void blank() { std::cout << "\n"; }

}  // namespace out
}  // namespace pp
