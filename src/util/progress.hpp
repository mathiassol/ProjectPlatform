#pragma once

#include <functional>
#include <string>

namespace pp {

class Progress {
 public:
  explicit Progress(std::string label);
  ~Progress();

  void step(const std::string& message);
  void done(const std::string& message = "");

 private:
  std::string label_;
  int step_ = 0;
  bool done_ = false;
};

class Spinner {
 public:
  explicit Spinner(std::string message);
  ~Spinner();
  void update(const std::string& message);
  void finish(const std::string& message);

 private:
  std::string message_;
  bool done_ = false;
};

}  // namespace pp
