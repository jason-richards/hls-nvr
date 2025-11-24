#pragma once

#include <memory>
#include <vector>
#include <future>


extern "C" {

#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/dict.h>
#include <libavutil/mem.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/intreadwrite.h>
#include <libavutil/avutil.h>

#ifdef av_err2str
#undef av_err2str
#endif

    av_always_inline char * av_err2str(int errnum) {
        char str[AV_ERROR_MAX_STRING_SIZE];
        return av_make_error_string(str, AV_ERROR_MAX_STRING_SIZE, errnum);
    }

#ifdef av_ts2str
#undef av_ts2str
#endif

    av_always_inline char * av_ts2str(int64_t ts) {
        thread_local char str[AV_TS_MAX_STRING_SIZE];
        memset(str, 0, sizeof(str));
        return av_ts_make_string(str, ts);
    }

#ifdef av_ts2timestr
#undef av_ts2timestr
#endif

    av_always_inline char * av_ts2timestr(int64_t ts, AVRational *tb) {
        thread_local char str[AV_TS_MAX_STRING_SIZE];
        memset(str, 0, sizeof(str));
        return av_ts_make_time_string(str, ts, tb);
    }
}


class TimeoutException : public std::exception {
public:
    virtual const char* what() const throw() {
        return "Timeout exception";
    }
};


static inline int
run_async(
    std::packaged_task<int()> &task,
    int timeout_seconds
) {
    std::future<int> future = task.get_future();
    std::thread task_thread(std::move(task));

    std::chrono::milliseconds span (timeout_seconds * 1000);
    std::future_status status = future.wait_for(span);
    if (status != std::future_status::ready) {
        task_thread.detach();
        throw TimeoutException();
    } else {
        task_thread.join();
    }

    return future.get();
}


static inline int
av_read_frame_timeout(
    AVFormatContext* format_context,
    AVPacket* packet,
    int timeout_seconds
) {
    std::packaged_task<int()> task(
        [&] {
            return av_read_frame(format_context, packet);
        }
    );

    return run_async(task, timeout_seconds);
}

static inline void
av_copy_stream(
    AVStream* destination,
    const AVStream* source
) {
    destination->id                  = source->id;
    destination->index               = source->index;
    destination->time_base           = source->time_base;
    destination->start_time          = source->start_time;
    destination->duration            = source->duration;
    destination->nb_frames           = source->nb_frames;
    destination->disposition         = source->disposition;
    destination->discard             = source->discard;
    destination->sample_aspect_ratio = source->sample_aspect_ratio;
    destination->avg_frame_rate      = source->avg_frame_rate;
    destination->event_flags         = source->event_flags;
    destination->r_frame_rate        = source->r_frame_rate;
    destination->pts_wrap_bits       = source->pts_wrap_bits;

    destination->codecpar = avcodec_parameters_alloc();
    avcodec_parameters_copy(destination->codecpar, source->codecpar);
}



// AVFrame smart pointer wrapper.
class AVFrameObj {
public:
    using AVFramePtr = std::unique_ptr<AVFrame, AVFrameObj>;

    static AVFramePtr
    Create() {
        return AVFramePtr(av_frame_alloc(), AVFrameObj());
    }

    void operator()(AVFrame* frame) {
        if (frame) {
            av_frame_free(&frame);
        }
    }
};


// AVPacket smart pointer wrapper.
class AVPacketObj {
public:
    using AVPacketPtr = std::unique_ptr<AVPacket, AVPacketObj>;
    using AVPackets = std::vector<AVPacketPtr>;


    static AVPacketPtr
    Create() {
        auto p = AVPacketPtr(av_packet_alloc(), AVPacketObj());
        p->data = nullptr;
        p->size = 0;
        return p;
    }


    void operator()(AVPacket* pkt) {
        if (pkt) {
            av_packet_unref(pkt);
            av_packet_free(&pkt);
        }
    }
};



static inline int
avformat_open_input_timeout(
    AVFormatContext **ps,
    const char *url,
    AVDictionary** opts,
    int timeout_seconds
) {
    std::packaged_task<int()> task(
        [&] {
            AVDictionary *opts_copy = nullptr;

            if (opts) {
                av_dict_copy(&opts_copy, *opts, 0);
            }

            return avformat_open_input(ps, url, nullptr, &opts_copy);
        }
    );

    return run_async(task, timeout_seconds);
}



// AVFormatContext smart pointer wrapper.
class AVFormatObj {
public:
    using AVFormatContextPtr = std::unique_ptr<AVFormatContext, AVFormatObj>;


    static AVFormatContextPtr
    Create(
        const char * connection,
        AVDictionary** options,
        int timeout_seconds
    ) {
        AVFormatContext* ifmt_ctx = nullptr;

        auto ret = avformat_open_input_timeout(&ifmt_ctx, connection, options, timeout_seconds);

        if (ret != 0) {
            return nullptr;
        }

        return AVFormatContextPtr(ifmt_ctx, AVFormatObj());
    }

    static AVFormatContextPtr
    Create(
        const char * output,
        AVDictionary** options
    ) { 
        AVFormatContext* ofmt_ctx = NULL;
        auto ret = avformat_alloc_output_context2(&ofmt_ctx, NULL, "mp4", NULL);

        if (ret != 0) {
            return nullptr;
        }

        return AVFormatContextPtr(ofmt_ctx, AVFormatObj());
    }

    void
    operator()(
        AVFormatContext* ctx
    ) {
        avformat_close_input(&ctx);
    }
};


// AVCodecContext smart pointer wrapper.
class AVCodecObj {
public:
    using AVCodecPtr = std::unique_ptr<AVCodecContext, AVCodecObj>;
    using AVCodecs = std::vector<AVCodecPtr>;

    static AVCodecPtr
    Create(const AVCodec * codec) {
        return AVCodecPtr(avcodec_alloc_context3(codec), AVCodecObj());
    }

    void operator()(AVCodecContext* ctx) {
        if (ctx) {
            avcodec_free_context(&ctx);
        }
    }
};


// Scaling context smart pointer wrapper.
class SwsContextObj {
public:
    using SwsContextPtr = std::unique_ptr<struct SwsContext, SwsContextObj>;

    static SwsContextPtr
    Create(int srcW, int srcH, enum AVPixelFormat srcFormat,
           int dstW, int dstH, enum AVPixelFormat dstFormat,
           int flags, SwsFilter *srcFilter,
           SwsFilter *dstFilter, const double *param)
    {
        return
            SwsContextPtr(
                sws_getContext(
                    srcW, srcH, srcFormat,
                    dstW, dstH, dstFormat,
                    flags, srcFilter,
                    dstFilter, param
                ),
                SwsContextObj()
            );
    }

    void operator()(struct SwsContext* sws_ctx) {
        if (sws_ctx) {
            sws_freeContext(sws_ctx);
        }
    }
};


// AVCodecParserContext smart pointer wrapper.
class AVCodecParserObj {
public:
    using AVCodecParserPtr = std::unique_ptr<AVCodecParserContext, AVCodecParserObj>;

    static AVCodecParserPtr
    Create(AVCodecID id) {
        return AVCodecParserPtr(av_parser_init(id), AVCodecParserObj());
    }

    void operator()(AVCodecParserContext* ctx) {
        if (ctx) {
            av_parser_close(ctx);
        }
    }
};

