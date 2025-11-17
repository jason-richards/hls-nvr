#pragma once

#include <memory>

#include "FFmpegCPPIncludes.hpp"

namespace M3U8 {


class M3U8Base;
using M3U8BasePtr = std::shared_ptr<M3U8Base>;


M3U8BasePtr
Create(
    const std::string name,
    const AVFormatObj::AVFormatContextPtr& ctx
);


void
PublishFragment(
    const M3U8BasePtr& m,
    const std::string& name,
    float duration,
    bool discontinuity
);


std::ostream&
operator<<(
    std::ostream& os,
    const M3U8BasePtr& m
);



} // namespace M3U8

