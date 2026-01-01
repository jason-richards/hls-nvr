#pragma once

#include <string>

namespace Config {
namespace PluginDir {

std::string
GetPluginDir(
  const std::string& configYAML
);

} // namespace PluginDir
} // namespace Config

