#include "PluginManager.hpp"
#include <dlfcn.h>
#include <dirent.h>
#include <iostream>

namespace Plugins {


PluginManager::~PluginManager() {
    for (auto& p : plugins_) {
        dlclose(p.handle);
    }
}

bool
PluginManager::load_one(
    const std::string& path
) {
    void* handle = dlopen(path.c_str(), RTLD_NOW);
    if (!handle) {
        std::cerr << "dlopen failed: " << dlerror() << "\n";
        return false;
    }

    auto get_factory = reinterpret_cast<GetPluginFactoryFn*>(
        dlsym(handle, "get_plugin_factory")
    );
    if (!get_factory) {
        std::cerr << "dlsym failed: " << dlerror() << "\n";
        dlclose(handle);
        return false;
    }

    PluginFactory* factory = get_factory();
    std::unique_ptr<Plugin> plugin = factory->create();

    plugins_.push_back({
        handle,
        std::move(plugin),
        path
    });

    return true;
}


int
PluginManager::load_directory(
    const std::string& dir
) {
    int count = 0;

    DIR* dp = opendir(dir.c_str());
    if (!dp) {
        std::cerr << "Cannot open directory: " << dir << "\n";
        return 0;
    }

    struct dirent* ent;
    while ((ent = readdir(dp))) {
        std::string name = ent->d_name;
        if (name.ends_with(".so")) {
            if (load_one(dir + "/" + name))
                count++;
        }
    }
    closedir(dp);
    return count;
}


std::string
PluginManager::Name() const {
    std::string names;
    for (auto& p : plugins()) {
        if (!names.empty()) {
            names += ",";
        }
        names += p.instance->Name();
    }
    
    return names;
}


bool
PluginManager::Configure(
    const Config::Sources::SourcePtr& source,
    const AVStream * stream
) {
    for (auto& p : plugins()) {
        if (!p.instance->Configure(source, stream)) {
            return false;
        }
    }

    return true;
}


bool
PluginManager::OnPacket(
    AVPacket *packet
) {
    for (auto& p : plugins()) {
        if (!p.instance->OnPacket(packet)) {
            return false;
        }
    }

    return true;
}


void
PluginManager::OnExit() {
    for (auto& p : plugins()) {
        p.instance->OnExit();
    }
}


} // namespace Plugins

