#include <filesystem>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <map>

#include "FFmpegCPPIncludes.hpp"
#include <libavutil/intreadwrite.h>
#include <libavutil/avutil.h>

#include "ConfigSources.hpp"
#include "M3U8Generator.hpp"

#define MAX_RETRIES 3

namespace fs = std::filesystem;


AVFormatObj::AVFormatContextPtr
ConnectInput(
    const std::string& rtsp_url
) {
    AVDictionary *opts = 0;

    av_dict_set(&opts, "rtsp_transport",    "tcp", 0);
    AVFormatObj::AVFormatContextPtr f = AVFormatObj::Create(rtsp_url.c_str(), &opts, 5);

    if (avformat_find_stream_info(f.get(), NULL) < 0) {
        fprintf(stderr, "Could not find stream information\n");
        return nullptr;
    }

    av_dump_format(f.get(), 0, rtsp_url.c_str(), 0);

    return f;
}


bool
DayRolledOver(
    const std::chrono::system_clock::time_point& prev,
    const std::chrono::system_clock::time_point& now
) {
    std::time_t tPrev = std::chrono::system_clock::to_time_t(prev);
    std::tm tmPrev = *std::localtime(&tPrev);

    std::time_t tNow = std::chrono::system_clock::to_time_t(now);
    std::tm tmNow = *std::localtime(&tNow);

    return (tmPrev.tm_year != tmNow.tm_year ||
            tmPrev.tm_yday != tmNow.tm_yday);
}


// Helper to read big-endian 64-bit
static uint64_t
read_be64(
    const uint8_t *buf
) {
    return ((uint64_t)buf[0] << 56) | ((uint64_t)buf[1] << 48) |
           ((uint64_t)buf[2] << 40) | ((uint64_t)buf[3] << 32) |
           ((uint64_t)buf[4] << 24) | ((uint64_t)buf[5] << 16) |
           ((uint64_t)buf[6] << 8)  | ((uint64_t)buf[7]);
}


inline void
write_be64(
    uint8_t* dst,
    uint64_t v
) {
    dst[0] = (v >> 56) & 0xFF;
    dst[1] = (v >> 48) & 0xFF;
    dst[2] = (v >> 40) & 0xFF;
    dst[3] = (v >> 32) & 0xFF;
    dst[4] = (v >> 24) & 0xFF;
    dst[5] = (v >> 16) & 0xFF;
    dst[6] = (v >>  8) & 0xFF;
    dst[7] =  v        & 0xFF;
}


static uint32_t
read_be32(
    const uint8_t *b
) {
    return (uint32_t(b[0]) << 24) |
           (uint32_t(b[1]) << 16) |
           (uint32_t(b[2]) <<  8) |
           uint32_t(b[3]);
}


uint64_t
GetBaseMediaDecodeTime(
    const std::string &path
) {
    AVIOContext *pb = nullptr;
    if (avio_open2(&pb, path.c_str(), AVIO_FLAG_READ, nullptr, nullptr) < 0) {
        std::cerr << "Could not open file: " << path << "\n";
        return 0;
    }

    uint8_t buf[16];
    uint64_t decode_time = 0;
    while (!avio_feof(pb)) {
        if (avio_read(pb, buf, 4) != 4)
            break;
        uint32_t type = AV_RL32(buf);
        if (type == MKTAG('t','f','d','t')) {
            // read version + flags (4 bytes)
            avio_read(pb, buf, 4);
            uint8_t version = buf[0];
            if (version == 1) {
                avio_read(pb, buf, 8);
                decode_time = read_be64(buf);
            } else {
                avio_read(pb, buf, 4);
                decode_time = AV_RB32(buf);
            }
            break;
        }
    }

    avio_close(pb);
    return decode_time;
}


/**
 * @brief Lists all `.m4s` files in a directory sorted by modification time (oldest first).
 *
 * @param dir Path to the directory to scan.
 * @return Vector of `.m4s` file paths sorted from oldest to newest.
 */
std::vector<fs::path>
list_m4s_files_by_mtime(const fs::path& dir)
{
    std::vector<fs::directory_entry> entries;

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".m4s") {
            entries.push_back(entry);
        }
    }

    std::sort(entries.begin(), entries.end(),
              [](const fs::directory_entry& a, const fs::directory_entry& b) {
                  return fs::last_write_time(a) < fs::last_write_time(b);
              });

    std::vector<fs::path> result;
    result.reserve(entries.size());
    for (const auto& e : entries)
        result.push_back(e.path());

    return result;
}


class OutputStream {
public:
    using OutputStreamPtr = std::shared_ptr<OutputStream>;

    AVFormatObj::AVFormatContextPtr m_Format;
    std::string                     m_Path;
    std::string                     m_Name;
    std::string                     m_CodecName;
    std::string                     m_CodecType;
    AVPacketObj::AVPackets          m_FrameBuffer;
    std::vector<uint8_t>            m_InitSegment;

    std::string                     m_CurrentFragment;
    std::shared_ptr<int64_t>        m_PTSOffset;
    


    /**
     * @brief Create and return an output stream pointer
     *
     * @param source - Source configuration Object from YAML file
     * @param stream - FFmpeg stream to create output for.
     */
    static OutputStreamPtr
    Create(
        const Config::Sources::SourcePtr& source,
        const AVStream* stream
    ) {
        return std::make_shared<OutputStream>(source, stream);
    }


    /**
     * @brief Assures the existence of the output base path and returns it.
     *        eg. [/scratch2/Recordings/Output]/NVR/[Camera Name]/[Time Stamp - Y-M-D]/[AUDIO/VIDEO]/[CODEC NAME]
     */
    std::string
    GetPath() {
        const std::time_t& timestamp = std::time(0);
        std::stringstream ss;

        ss << m_Path
           << "/NVR/"
           << m_Name 
           << "/"
           << std::put_time(std::localtime(&timestamp), "%Y-%m-%d")
           << "/"
           << m_CodecType
           << "/"
           << m_CodecName;

        std::string base_path = ss.str();
        if (!std::filesystem::exists(base_path)) {
            if (!std::filesystem::create_directories(base_path)) {
                throw std::runtime_error("Unable to create NVR storage: " + base_path);
            }
        }

        return base_path;
    }


    /**
     * @brief FFmpeg does not allow you to control the start PTS for streams; they always start at 0.  This method
     *        "patches" the timestamps in the output m4s by finding the 'sidx' and 'tfdt' ATOM's and adding an
     *        offset
     *
     * @param data - Pointer to the memory buffer where the m4s resides
     * @param size - How large the m4s is.
     */
    void
    FixTimestamps(
        uint8_t * data,
        int size
    ) {
        uint8_t * p = data;

        while (p < data+size) {
            int32_t size = read_be32(p);
            int32_t type = *reinterpret_cast<int32_t*>(p+4);

            /**
             *  Fix earliest_presentation_time in 'sidx'
             *      (1)  version
             *      (3)  flags
             *      (4)  reference_ID
             *      (4)  timescale
             *      (8)  earliest_presentation_time
             *      (8)  first_offset
             *      (2)  reserved
             *      (2)  reference_count
             */
            
            if (type == MKTAG('s','i','d','x')) {
                uint64_t earliest_presentation_time = read_be64(p+20);
                earliest_presentation_time += *m_PTSOffset;
                write_be64(p+20, earliest_presentation_time);
                p+=size;
                continue;
            }


            /**
             *  Fix base_media_decode_time in 'tfdt'
             *      (1)  version
             *      (3)  flags
             *      (8)  base_media_decode_time
             */
            if (type == MKTAG('t','f','d','t')) {
                uint64_t base_media_decode_time = read_be64(p+12);
                base_media_decode_time += *m_PTSOffset;
                write_be64(p+12, base_media_decode_time);
                p+=size;
                continue;
            }


            /**
             *  Fix sequence number in 'mfhd'
             *      (1)  version
             *      (3)  flags
             *      (4)  sequence_number
             */
            if (type == MKTAG('m','f','h','d')) {
                //uint32_t sequence_number = read_be32(p+12);
                p+=size;
                continue;
            }


            /**
             *  We are looking for ATOMs nested with the 'moof' and 'traf' ATOMs
             */
            if (type == MKTAG('m','o','o','f') ||
                type == MKTAG('t','r','a','f')
            ) {
                p+=8;
                continue;
            }

            p+=size;
        }
    }


    /**
     * @brief Helper function that handles splitting the video stream into separate m4s files.
     * 
     * @param ts = The timestamp of when the split occurred.
     */
    void
    Split(
        const std::time_t ts
    ) {
        if (m_Format->pb && m_CurrentFragment.size()) {
            av_write_frame(m_Format.get(), nullptr);    // flush

            uint8_t * buf = nullptr;
            int size = avio_close_dyn_buf(m_Format->pb, &buf);

            FixTimestamps(buf, size);

            m_Format->pb = nullptr;
            std::ofstream of(m_CurrentFragment, std::ios::binary); 
            of.write(reinterpret_cast<const char *>(buf), size);
            of.close();
            av_free(buf);
        }

        if (!m_PTSOffset) {
            int64_t pts_offset = 0;
            std::tm* t = std::localtime(&ts);
            std::cout << t->tm_hour << ":" << t->tm_min << ":" << t->tm_sec << std::endl;
            pts_offset += t->tm_hour * 60 * 60;
            pts_offset += t->tm_min  * 60;
            pts_offset += t->tm_sec;
            pts_offset *= m_Format->streams[0]->time_base.den;
            m_PTSOffset = std::make_shared<int64_t>(pts_offset); 
        }

        std::stringstream ss;
        ss  << GetPath()
            << "/"
            << std::put_time(std::localtime(&ts), "%H-%M-%S")
            << ".m4s";

        std::cout << ss.str() << std::endl;
        m_CurrentFragment = ss.str();

        if (avio_open_dyn_buf(&m_Format->pb) < 0) {
            fprintf(stderr, "Could not open m4s fragment buffer\n");
            return;
        }

        m_Format->flags |= AVFMT_FLAG_CUSTOM_IO;
    }



    /**
     * @brief Writes a single packet to the output stream.
     * 
     * @param p = The packet data to write to the output stream
     */
    void
    WritePacket(
        const AVPacketObj::AVPacketPtr& p
    ) {
        if (m_Format->pb) {
            p->stream_index=0;
            p->pos = -1;
            if (av_interleaved_write_frame(m_Format.get(), p.get()) < 0) {
                fprintf(stderr, "Error muxing packet\n");
            }
        }
    }


    /**
     * @brief Helper function that assures the init segment is present and up to date.
     */
    void
    VerifyInitFile() {
        std::string base_path = GetPath(); 
        std::string init = base_path + "/init.mp4";
        if (!std::filesystem::exists(init) ||
            std::filesystem::file_size(init) != m_InitSegment.size()
        ) {
            std::ofstream init(base_path + "/init.mp4", std::ios::binary);
            init.write(reinterpret_cast<const char*>(m_InitSegment.data()), m_InitSegment.size());
            init.flush();
            init.close();
        }
    }


    /**
     * @brief Helper function that assures the stream M3U8 is up to date.
     */
    void
    VerifyM3U8() {
        std::string base_path = GetPath(); 
        std::string m3u8 = m_CodecType + ".m3u8";
        std::string path = base_path + "/" + m3u8;
        M3U8::M3U8BasePtr m = M3U8::Create(m3u8, m_Format);
        std::cout << path << std::endl;
        auto files = list_m4s_files_by_mtime(base_path);
        std::ofstream of(path, std::ios::trunc);

        std::string prev_file;
        int64_t prev_dts = 0; 
        float prev_duration = 0.0;
        for (auto f : files) {
            if (std::filesystem::is_empty(f)) {
                continue;
            }
            int64_t dts = GetBaseMediaDecodeTime(f.string());
            if (!prev_file.empty()) {
                if (dts > prev_dts) {
                    prev_duration = static_cast<float>(dts - prev_dts) / m_Format->streams[0]->time_base.den,
                    PublishFragment(
                        m,
                        f.string(),
                        prev_duration,
                        false);
                } else {
                    PublishFragment(
                        m,
                        f.string(),
                        prev_duration,
                        true);
                }
            }

            prev_file = f;
            prev_dts = dts;
        }
        of << m;
    }



    /**
     * @brief Generate the initialization mp4.  Store it in an internal buffer so that it may be written to disk
     *        as needed.
     */
    void
    GenerateInitSegment() {
        if (avio_open_dyn_buf(&m_Format->pb) < 0) {
            fprintf(stderr, "Could not open init segment buffer\n");
            return;
        }

        m_Format->flags |= AVFMT_FLAG_CUSTOM_IO;

        AVDictionary *options = NULL;
        av_dict_set(&options, "movflags", "+frag_custom+empty_moov+default_base_moof+dash", 0);
        if (avformat_write_header(m_Format.get(), &options) < 0) {
            fprintf(stderr, "Error occurred when writing header\n");
            return;
        }

        uint8_t * buf = nullptr;
        int size = avio_close_dyn_buf(m_Format->pb, &buf);
        m_InitSegment = std::vector<uint8_t>(buf, buf+size);
        av_free(buf);

        m_Format->flags &= ~AVFMT_FLAG_CUSTOM_IO;
        m_Format->pb = nullptr;
    }


    /**
     * @brief OutputStream constructor; Copies the necessary data from the input stream to the output stream
     *
     * @param source - The 'source' configuration object from the YAML
     * @param stream - The FFmpeg stream to create
     */
    OutputStream(
        const Config::Sources::SourcePtr& source,
        const AVStream* stream
    ) : m_Format(AVFormatObj::Create(nullptr, nullptr)),
        m_Path(source->output),
        m_Name(source->name),
        m_CodecName(avcodec_get_name(stream->codecpar->codec_id)),
        m_CodecType(av_get_media_type_string(stream->codecpar->codec_type))
    {
        AVStream *out_stream = avformat_new_stream(m_Format.get(), nullptr);
        if (!out_stream) {
            fprintf(stderr, "Failed allocating output stream\n");
            avformat_free_context(m_Format.get());
            return;
        }

        out_stream->id                  = m_Format->nb_streams - 1;
        out_stream->avg_frame_rate      = stream->avg_frame_rate;
        out_stream->r_frame_rate        = stream->r_frame_rate;
        out_stream->start_time          = stream->start_time;
        out_stream->time_base           = stream->time_base;
        out_stream->codecpar->bit_rate  = stream->codecpar->bit_rate;

        if (avcodec_parameters_copy(out_stream->codecpar, stream->codecpar) < 0) {
            fprintf(stderr, "Failed to copy codec parameters\n");
            avformat_free_context(m_Format.get());
            return;
        }

        out_stream->codecpar->codec_tag = 0;
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && 
            stream->codecpar->codec_id   == AV_CODEC_ID_OPUS &&
            stream->codecpar->frame_size == 0
        ) {
            out_stream->codecpar->frame_size = 960; // Opus 20 ms
        }

        GenerateInitSegment();
    }
};


void
VerifyMasterM3U8(
    const AVFormatObj::AVFormatContextPtr& ctx,
    const std::string& path,
    const std::string& name
) {
    const std::time_t& timestamp = std::time(0);
    std::stringstream ss;

    ss << path 
       << "/NVR/"
       << name
       << "/"
       << std::put_time(std::localtime(&timestamp), "%Y-%m-%d")
       << "/master.m3u8";

    std::string master = ss.str();
    if (!std::filesystem::exists(master)) {
        M3U8::M3U8BasePtr m = M3U8::Create("master.m3u8", ctx);
        std::ofstream of(master);
        of << m;
        of.flush();
        of.close();
    }
}
 

void
transmux_rtsp_to_hls(
    Config::Sources::SourcePtr source
) {
    AVFormatObj::AVFormatContextPtr input_fmt  = ConnectInput(source->url);
    std::map<int, OutputStream::OutputStreamPtr> output_streams;

    for (int i = 0; i < input_fmt->nb_streams; i++) {
        AVStream *in_stream = input_fmt->streams[i];
        output_streams[in_stream->index] = OutputStream::Create(source, in_stream);
    }

    std::atomic<bool> done{false};
    auto metadata_watcher = std::thread(
        [&]{
            while (!done.load(std::memory_order_relaxed)) {
                // Step 1 : Wake up Every 10 Seconds to do Checks
                std::this_thread::sleep_for(std::chrono::milliseconds(10000));

                // Step 2 : Verify Master M3U8 exists
                VerifyMasterM3U8(input_fmt, source->output, source->name);

                // Step 3 : Verify Output Stream "init.mp4" segments exist
                for (const auto s : output_streams) {
                    s.second->VerifyInitFile();
                    s.second->VerifyM3U8();
                }
            }
        }
    );

    const std::time_t& start_time = std::time(0);
    int retries = MAX_RETRIES;

    auto prev = std::chrono::system_clock::now();

    do {
        // Bring the whole thing down and Midnight; let systemd bring it back up
        auto now = std::chrono::system_clock::now();
        if (DayRolledOver(prev, now)) {
            done.store(true, std::memory_order_relaxed);
            continue;
        }

        prev=now;

        auto packet = AVPacketObj::Create();
        packet->data = NULL;
        packet->size = 0;

        try {
            int ret = av_read_frame_timeout(input_fmt.get(), packet.get(), 5);
            if (ret < 0) {
                std::cerr << "av_read_frame error.\n";
                if (retries-- <= 0) {
                    done.store(true, std::memory_order_relaxed);
                }
                continue;
            }
        } catch (const TimeoutException& e) {
            std::cerr << "av_read_frame timed out!\n";
            if (retries-- <= 0) {
                done.store(true, std::memory_order_relaxed);
            }
            continue;
        } catch (const std::exception& e) {
            std::cerr << "unknown exception encountered in av_read_frame.\n";
            if (retries-- <= 0) {
                done.store(true, std::memory_order_relaxed);
            }
            continue;
        }

        retries = MAX_RETRIES;

        if (packet->pts == AV_NOPTS_VALUE) {
            continue;
        }

        AVStream *in_stream  = input_fmt->streams[packet->stream_index];

        if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && packet->flags & AV_PKT_FLAG_KEY) {
            double pts = static_cast<double>(packet->pts) / in_stream->time_base.den;
            const std::time_t& ts = static_cast<time_t>(pts) + start_time;

            for(auto stream : output_streams) {
                stream.second->Split(ts);
            }
        }

        output_streams[packet->stream_index]->WritePacket(packet);
    } while (!done.load(std::memory_order_relaxed));

    metadata_watcher.join();
}



int
main(
    int   argc,
    char *argv[]
) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <config.yaml>\n", argv[0]);
        return 1;
    }

    av_log_set_level(AV_LOG_ERROR);

    avformat_network_init();

    std::vector<std::thread> sources;
    for (auto source : Config::Sources::GetSources(argv[1])) {
        sources.push_back(
            std::thread(
                [source] {
                    transmux_rtsp_to_hls(source);
                }
            )
        );
    }

    for (auto& source : sources) {
        source.join();
    }

    return 0;
}

