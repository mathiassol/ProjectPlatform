#include "cli/commands.hpp"
#include "cli/parser.hpp"
#include "core/install.hpp"
#include "util/output.hpp"
#include "util/paths.hpp"

int main(int argc, char** argv) {
  pp::out::initConsole();
  pp::ensureDir(pp::appDataDir());
  pp::ensureHookScriptFresh();
  pp::ensureDir(pp::loadConfig().projects_dir);
  pp::ensureDir(pp::loadConfig().templates_dir);
  pp::ensureDir(pp::globalScriptsDir());

  const pp::Args args = pp::parseArgs(argc, argv);
  return pp::runCommand(args);
}
