#pragma once

#include <memory>
#include <string>
#include <functional>

#include "FFmpegCPPIncludes.hpp"
#include "ConfigSources.hpp"

namespace Plugins {


class Plugin {
public:
    virtual ~Plugin() = default;


    /* Return the name of the Plugin 
     *
     * @result std::string - The name of the Plugin.
     */
    virtual std::string
    Name() const = 0;


    /* Configure call for Plugin.  Called one time per thread.
     *
     * @param const Config::Sources::SourcePtr& - Configuration Source object.
     * @param const AVStream * - VIDEO/AUDIO/METADATA stream from libav
     * @result bool - Returns false if error encountered while configuring.
     */
    virtual bool
    Configure(
        const Config::Sources::SourcePtr& source,
        const AVStream * stream
    ) = 0;


    /* This function is called every time a packet is received.
     *
     * @param const AVPacket * - Livav VIDEO/AUDIO/METADATA packet from previously configured stream.
     * @result bool - Returns false if error encountered while processing packet.
     */
    virtual bool
    OnPacket(AVPacket *packet) = 0;


    /* This function is called before exitting.  The process promises not to exit until all plugins have returned.
     */
    virtual void
    OnExit() = 0;
};


struct PluginFactory {
    std::function<std::unique_ptr<Plugin>()> create;
};


extern "C" {
    using GetPluginFactoryFn = PluginFactory*();
}


} // namespace Plugins

