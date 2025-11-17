#include <yaml-cpp/yaml.h>
#include <iostream>

#include "ConfigOutputDir.hpp" 

namespace Config {
namespace OutputDir {

std::string
GetOutputDir (
  const std::string& configYAML
) {
  try {
    YAML::Node config = YAML::LoadFile(configYAML);
    return config["output_dir"] ? config["output_dir"].as<std::string>() : "/tmp/feed-decoder/Output";
  } catch (const YAML::BadFile& e) {
    std::cout << e.msg << std::endl;
  } catch (const YAML::ParserException& e) {}

  return std::string("");
}

} // namespace OutputDir 
} // namespace Config

