#include <yaml-cpp/yaml.h>
#include <iostream>


#include "ConfigSources.hpp"


namespace Config {
namespace Sources {


Sources
GetSources (
  const std::string& configYAML
) {
  Sources sources;
  try {
    YAML::Node config = YAML::LoadFile(configYAML);

    for (auto node : config["source"]) {
      auto source = std::make_shared<Source>();

      source->name     = node["name"].as<std::string>();
      source->url      = node["rtsp"]      ? node["rtsp"].as<std::string>() : node["file"].as<std::string>();
      source->output   = node["output"]    ? node["output"].as<std::string>() : "";
      source->isAudio  = node["audio"]     ? node["audio"].as<bool>()     : false;
      source->isVideo  = node["video"]     ? node["video"].as<bool>()     : false;
      source->isFile   = node["file"]      ? true                         : false;
      source->isValid  = true;

      sources.push_back(source);
    }
  } catch (const YAML::BadFile& e) {
    std::cout << e.msg << std::endl;
  } catch (const YAML::ParserException& e) {}

  return sources;
}


} // namespace Sources
} // namespace Config

