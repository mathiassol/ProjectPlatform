#include "util/output.hpp"

#include <windows.h>

namespace pp {
namespace out {

static bool g_vt = false;

void initConsole() {
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  if (hOut == INVALID_HANDLE_VALUE) return;
  DWORD mode = 0;
  if (!GetConsoleMode(hOut, &mode)) return;
  if (SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) g_vt = true;
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
