#pragma once

#include <string>
#include <memory>
#include <vector>

namespace Config {
namespace Sources {


struct Source {
    std::string name;
    std::string url;
    std::string hls_segment_filename;
    std::string master_pl_name;
    std::string hls_playlist_type;
    std::string output;
    int hls_list_size;
    int hls_time;
    bool isAudio;
    bool isVideo;
    bool isValid;
    bool isFile;
};


using SourcePtr = std::shared_ptr<Source>;
using Sources   = std::vector<SourcePtr>;


Sources
GetSources(
    const std::string& configYAML
);



} // namespace Sources
} // namespace Config

