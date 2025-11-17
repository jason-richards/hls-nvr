
#include <stdio.h>
#include <string.h>
#include <map>
#include <iostream>
#include <sstream>
#include <unordered_map>

#include "M3U8Generator.hpp"


namespace M3U8 {

static std::string 
GetCodecString (
    AVStream *st
) {
    AVCodecParameters *par = st->codecpar;

    char codec_buf[64] = {0};

    switch (par->codec_id) {
    case AV_CODEC_ID_H264: {
        // H.264 = avc1.<profile><constraints><level>
        int profile_idc   = (par->profile > 0) ? par->profile : 0x42;
        int constraint    = 0x00;
        int level_idc     = (par->level > 0) ? par->level : 0x1F;
        snprintf(codec_buf, sizeof(codec_buf), "avc1.%02x%02x%02x",
                 profile_idc, constraint, level_idc);
        break;
    }

    case AV_CODEC_ID_HEVC: {
        int profile = (par->profile > 0) ? par->profile : 1;
        int level   = (par->level > 0) ? par->level : 120;
        snprintf(codec_buf, sizeof(codec_buf), "hvc1.%d.L%d.00", profile, level);
        break;
    }

    case AV_CODEC_ID_AAC: {
        int audio_obj_type = 2; // AAC-LC default
        if (par->profile == FF_PROFILE_AAC_HE)
            audio_obj_type = 5;
        snprintf(codec_buf, sizeof(codec_buf), "mp4a.40.%d", audio_obj_type);
        break;
    }

    case AV_CODEC_ID_OPUS: {
        snprintf(codec_buf, sizeof(codec_buf), "opus");
        break;
    }

    default: {
        // Use codec name as fallback, like "vp9" or "opus"
        const char *name = avcodec_get_name(par->codec_id);
        snprintf(codec_buf, sizeof(codec_buf), "%s", name);
        break;
    }
    }

    return std::string(codec_buf);
}


class M3U8Base {
public:

    struct Fragment {
        std::string name;
        float duration;
        bool discontinuity;
    };

    using FragmentPtr = std::shared_ptr<struct Fragment>;
    std::vector<FragmentPtr> m_Fragments;


    M3U8Base(
        const AVFormatObj::AVFormatContextPtr& ctx,
        int duration=5,
        int version=7
    ) : m_TargetDuration(duration), m_Version(version) {}


    virtual ~M3U8Base() = default;
    virtual void print(std::ostream& os) const = 0;


    friend std::ostream& operator<<(std::ostream& os, const M3U8Base& mg) {
        mg.print(os);
        return os;
    }


    virtual void
    SetHeader(
        std::ostream& os
    ) const {
        os << "#EXTM3U\n";
    }


    virtual void
    SetVersion(
        std::ostream& os
    ) const {
        os << "#EXT-X-VERSION:"
           << m_Version
           << "\n";
    }


    void
    SetTargetDuration(
        std::ostream& os
    ) const {
        os << "#EXT-X-TARGETDURATION:"
           << m_TargetDuration
           << "\n";
    }


    void
    SetFragmentsList(
        std::ostream& os
    ) const {
        for (auto f : m_Fragments) {
            if (f->discontinuity) {
                os << "\n#EXT-X-DISCONTINUITY\n";
                os << "#EXT-X-MAP:URI=\"init.mp4\"\n";
            }

            os << "#EXTINF:"
               << f->duration
               << ",\n"
               << f->name
               << std::endl;
        } 
    }

    void
    PublishFragment(
        const std::string& name,
        float duration,
        bool discontinuity
    ) {
        m_Fragments.push_back(std::make_shared<struct Fragment>(name, duration, discontinuity));
    }


    int m_TargetDuration;
    int m_Version;
};


/**********************************************************************************************************************
#EXTM3U
#EXT-X-VERSION:6

# Two audio groups: AAC and Opus

# AAC audio group
#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="aac",NAME="English (AAC)",LANGUAGE="en",AUTOSELECT=YES,DEFAULT=YES,URI="audio_aac.m3u8"

# Opus audio group
#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="opus",NAME="English (Opus)",LANGUAGE="en",AUTOSELECT=NO,DEFAULT=NO,URI="audio_opus.m3u8"

# A single video variant that can reference either audio group
# (You can list more variants if you want multiple bitrates.)
#EXT-X-STREAM-INF:BANDWIDTH=2500000,RESOLUTION=1920x1080,CODECS="avc1.640029,mp4a.40.2",AUDIO="aac"
video_1080p.m3u8

#EXT-X-STREAM-INF:BANDWIDTH=2500000,RESOLUTION=1920x1080,CODECS="avc1.640029,opus",AUDIO="opus"
video_1080p.m3u8

**********************************************************************************************************************/
class M3U8Master : public M3U8Base {
public:
    using M3U8Base::M3U8Base;

    std::vector<std::string> m_AudioGroups;
    std::vector<std::string> m_VideoStreams;


    int
    EstimateBandwidth(
        const AVStream* st
    ) {
        constexpr int FPS = 30;     // Rough Framerate
        constexpr float BPP = 0.06; // Bits-per-pixel-per-frame
        return st->codecpar->width * st->codecpar->height * FPS * BPP;
    }


    M3U8Master(
        const AVFormatObj::AVFormatContextPtr& ctx,
        int version=7
    ) : M3U8Base(ctx, version) {
        bool first_audio = true;
        bool first_video = true;
        for (int i = 0; i < ctx->nb_streams; i++) {
            std::stringstream ss;
            if (ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                std::string codec_name = avcodec_get_name(ctx->streams[i]->codecpar->codec_id);
                ss  << "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\""
                    << codec_name
                    << "\",NAME=\"ENGLISH ("
                    << codec_name
                    << ")\",LANGUAGE=\"en\","
                    << (first_audio ? "AUTOSELECT=YES,"  : "AUTOSELECT=NO,")
                    << (first_audio ? "DEFAULT=YES,"     : "DEFAULT=NO,")
                    << "URI=\""
                    << "audio/"
                    << codec_name
                    << "/audio.m3u8\"\n";
                m_AudioGroups.push_back(ss.str());
                first_audio = false;
                continue;
            }

            if (ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                for (int j = 0; j < ctx->nb_streams; j++) {
                    ss.str("");

                    if (ctx->streams[j]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                        continue;
                    }

                    ss  << "#EXT-X-STREAM-INF:"
                        << "BANDWIDTH="
                        << EstimateBandwidth(ctx->streams[i])
                        << ",RESOLUTION="
                        << ctx->streams[i]->codecpar->width
                        << "x"
                        << ctx->streams[i]->codecpar->height
                        << ",CODECS=\""
                        << GetCodecString(ctx->streams[i])
                        << ","
                        << GetCodecString(ctx->streams[j])
                        << "\",AUDIO=\""
                        << avcodec_get_name(ctx->streams[j]->codecpar->codec_id)
                        << "\"\n"
                        << "video/"
                        << avcodec_get_name(ctx->streams[i]->codecpar->codec_id)
                        << "/video.m3u8\n";

                    m_VideoStreams.push_back(ss.str());
                }

                continue;
            } 
        }
    }

    void
    SetAudioGroups(
        std::ostream& os
    ) const {
        os << "\n# Audio Groups\n";

        for (const auto& a : m_AudioGroups) {
            os << a << "\n";
        }
    }

    void
    SetVideoStreams(
        std::ostream& os
    ) const {
        os << "\n# Video Streams\n";
        for (const auto& v : m_VideoStreams) {
            os << v << "\n";
        }
    }


    virtual void
    print(
        std::ostream& os
    ) const {
        SetHeader(os);
        SetVersion(os);
        SetAudioGroups(os);
        SetVideoStreams(os);
    }
};


class M3U8Stream : public M3U8Base {
public:
    using M3U8Base::M3U8Base;
    bool m_Init = true;

    void
    SetXMap(
        std::ostream& os
    ) const {
        os << "#EXT-X-MAP:URI=\"init.mp4\"\n";
    }


    void
    SetMediaSequence(
        std::ostream& os
    ) const {
        os << "#EXT-X-MEDIA-SEQUENCE:0\n";
    }


    void
    SetEndList(
        std::ostream& os
    ) const {
        os << "#EXT-X-ENDLIST\n";
    }


    virtual void
    print(
        std::ostream& os
    ) const {
        SetHeader(os);
        SetVersion(os);
        SetTargetDuration(os);
        SetXMap(os);
        SetMediaSequence(os);
        SetFragmentsList(os);
        SetEndList(os);
    }
};


class M3U8Generator {
public:
    using Creator = std::function<std::unique_ptr<M3U8Base>(const AVFormatObj::AVFormatContextPtr&)>;


    static M3U8Generator&
    instance() {
        static M3U8Generator generator;
        return generator;
    }


    void
    RegisterM3U8(
        const std::string& name,
        Creator creator
    ) {
        creators_[name] = std::move(creator);
    }


    std::unique_ptr<M3U8Base>
    create(const std::string& name, const AVFormatObj::AVFormatContextPtr& ctx) const {
        if (auto it = creators_.find(name); it != creators_.end()) {
            return it->second(ctx);
        }
        return nullptr;
    }


    M3U8Generator() {}

private:
    std::unordered_map<std::string, Creator> creators_;
};


struct M3U8Registrar {
    M3U8Registrar() {
        M3U8Generator::instance().RegisterM3U8("master.m3u8",
            [](const AVFormatObj::AVFormatContextPtr& ctx) { return std::make_unique<M3U8Master>(ctx); });
        M3U8Generator::instance().RegisterM3U8("audio.m3u8", 
            [](const AVFormatObj::AVFormatContextPtr& ctx) { return std::make_unique<M3U8Stream>(ctx);  });
        M3U8Generator::instance().RegisterM3U8("video.m3u8",
            [](const AVFormatObj::AVFormatContextPtr& ctx) { return std::make_unique<M3U8Stream>(ctx);  });
    }
};

static M3U8Registrar registrar;


M3U8BasePtr
Create(
    const std::string name,
    const AVFormatObj::AVFormatContextPtr& ctx
) {
    return M3U8Generator::instance().create(name, ctx);
}


void
Print(
    const M3U8BasePtr& m,
    std::ostream& os
) {
    os << *m.get();
}


void
PublishFragment(
    const M3U8BasePtr& m,
    const std::string& name,
    float duration,
    bool discontinuity
) {
    m->PublishFragment(name, duration, discontinuity);
}


std::ostream&
operator<<(
    std::ostream& os,
    const M3U8BasePtr& m
) {
    Print(m, os);
    return os;
}

} // namespace M3U8

