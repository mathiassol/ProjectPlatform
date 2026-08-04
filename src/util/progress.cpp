#include "util/progress.hpp"

#include "util/output.hpp"

#include <chrono>
#include <iostream>
#include <thread>

namespace pp {

Progress::Progress(std::string label) : label_(std::move(label)) {
  out::dim("[" + label_ + "] starting...");
}

Progress::~Progress() {
  if (!done_) out::dim("[" + label_ + "] interrupted");
}

void Progress::step(const std::string& message) {
  ++step_;
  out::info("  [" + std::to_string(step_) + "] " + message);
}

void Progress::done(const std::string& message) {
  done_ = true;
  if (message.empty())
    out::success("[" + label_ + "] done");
  else
    out::success("[" + label_ + "] " + message);
}

Spinner::Spinner(std::string message) : message_(std::move(message)) {
  out::dim(message_ + "...");
}

Spinner::~Spinner() {
  if (!done_) std::cout << "\r" << std::string(message_.size() + 4, ' ') << "\r" << std::flush;
}

void Spinner::update(const std::string& message) {
  message_ = message;
  std::cout << "\r  " << message_ << "..." << std::flush;
}

void Spinner::finish(const std::string& message) {
  done_ = true;
  std::cout << "\r" << std::string(message.size() + 6, ' ') << "\r" << std::flush;
  out::success(message);
}

}  // namespace pp
