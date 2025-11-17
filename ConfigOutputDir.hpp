#pragma once

#include <string>

namespace Config {
namespace OutputDir {

std::string
GetOutputDir(
  const std::string& configYAML
);

} // namespace OutputDir
} // namespace Config

