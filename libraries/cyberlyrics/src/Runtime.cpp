#include "CyberLyrics/Runtime.h"

extern "C" {
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
}

#include <chrono>
#include <cstdio>
#include <memory>

namespace {
int interrupted(void* context) {
    return std::chrono::steady_clock::now() >= *static_cast<std::chrono::steady_clock::time_point*>(context);
}
}  // namespace

int cyberlyrics_read_lrc(const char* path, CyberLyricsLineCallback callback, void* context, char* error,
                         int error_capacity) {
    if (!path || !callback || !error || error_capacity <= 0) {
        return AVERROR(EINVAL);
    }
    error[0] = 0;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    AVFormatContext* raw = avformat_alloc_context();
    if (!raw) {
        std::snprintf(error, error_capacity, "Cannot allocate lyric demuxer");
        return AVERROR(ENOMEM);
    }
    raw->interrupt_callback = {interrupted, &deadline};
    const auto* demuxer = av_find_input_format("lrc");
    if (!demuxer) {
        avformat_free_context(raw);
        std::snprintf(error, error_capacity, "FFmpeg LRC demuxer is unavailable");
        return AVERROR_DEMUXER_NOT_FOUND;
    }
    AVDictionary* options = nullptr;
    av_dict_set(&options, "protocol_whitelist", "file", 0);
    int result = avformat_open_input(&raw, path, demuxer, &options);
    av_dict_free(&options);
    if (result < 0) {
        av_strerror(result, error, error_capacity);
        avformat_close_input(&raw);
        return result;
    }
    const auto close = [](AVFormatContext* value) {
        avformat_close_input(&value);
    };
    const std::unique_ptr<AVFormatContext, decltype(close)> format(raw, close);
    const auto freePacket = [](AVPacket* value) {
        av_packet_free(&value);
    };
    const std::unique_ptr<AVPacket, decltype(freePacket)> packet(av_packet_alloc(), freePacket);
    if (!packet) {
        std::snprintf(error, error_capacity, "Cannot allocate lyric packet");
        return AVERROR(ENOMEM);
    }
    int count = 0;
    while (count < 20000 && (result = av_read_frame(format.get(), packet.get())) >= 0) {
        if (packet->pts != AV_NOPTS_VALUE && packet->size <= 65536) {
            callback(context, packet->pts * av_q2d(format->streams[packet->stream_index]->time_base),
                     reinterpret_cast<const char*>(packet->data), packet->size);
            ++count;
        }
        av_packet_unref(packet.get());
    }
    if (count == 20000) {
        std::snprintf(error, error_capacity, "Lyrics exceed 20000 lines");
        return AVERROR(EFBIG);
    }
    if (result != AVERROR_EOF && result < 0) {
        av_strerror(result, error, error_capacity);
        return result;
    }
    return count;
}

const char* cyberlyrics_runtime_license(void) {
    return avformat_license();
}
