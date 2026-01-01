#pragma once
#include "PluginAPI.hpp"
#include <vector>
#include <memory>
#include <string>


namespace Plugins {

class PluginManager : public Plugin{
public:
    struct LoadedPlugin {
        void* handle;
        std::unique_ptr<Plugin> instance;
        std::string path;
    };

    ~PluginManager();
    int load_directory(const std::string& dir);

    const std::vector<LoadedPlugin>& plugins() const { return plugins_; }

    virtual std::string
    Name() const override;

    virtual bool 
    Configure(
        const Config::Sources::SourcePtr& source,
        const AVStream *stream
    ) override;

    virtual bool
    OnPacket(AVPacket *packet) override;

    virtual void
    OnExit() override;

private:
    bool load_one(const std::string& path);

    std::vector<LoadedPlugin> plugins_;
};

} // namespace Plugins

