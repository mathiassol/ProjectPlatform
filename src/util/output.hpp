#pragma once

#include <iostream>
#include <string>

namespace pp {
namespace out {

enum class Color { Reset, Red, Green, Yellow, Cyan, Dim, Bold };

void initConsole();
std::ostream& stream(Color c);

void info(const std::string& msg);
void success(const std::string& msg);
void warn(const std::string& msg);
void error(const std::string& msg);
void dim(const std::string& msg);
void title(const std::string& msg);
void blank();

}  // namespace out
}  // namespace pp
