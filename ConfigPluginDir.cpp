#include <yaml-cpp/yaml.h>
#include <iostream>

#include "ConfigPluginDir.hpp" 

namespace Config {
namespace PluginDir {

std::string
GetPluginDir (
  const std::string& configYAML
) {
  try {
    YAML::Node config = YAML::LoadFile(configYAML);
    return config["plugin_dir"] ? config["plugin_dir"].as<std::string>() : "/etc/hls-nvr/plugins";
  } catch (const YAML::BadFile& e) {
    std::cout << e.msg << std::endl;
  } catch (const YAML::ParserException& e) {}

  return std::string("");
}

} // namespace PluginDir 
} // namespace Config

