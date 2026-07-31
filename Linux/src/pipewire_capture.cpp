#include "opendisplay/pipewire_capture.hpp"

#include "opendisplay/log.hpp"

#include <spa/param/format-utils.h>
#include <spa/param/video/raw.h>
#include <spa/utils/result.h>

#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <stdexcept>

namespace od {
namespace {

const pw_stream_events streamEvents = [] {
    pw_stream_events events{};
    events.version = PW_VERSION_STREAM_EVENTS;
    events.state_changed = PipeWireCapture::stateChanged;
    events.param_changed = PipeWireCapture::parameterChanged;
    events.process = PipeWireCapture::process;
    return events;
}();

VideoFormat::PixelFormat pixelFormat(const spa_video_format format) {
    switch (format) {
    case SPA_VIDEO_FORMAT_BGRA: return VideoFormat::PixelFormat::Bgra;
    case SPA_VIDEO_FORMAT_BGRx: return VideoFormat::PixelFormat::Bgrx;
    case SPA_VIDEO_FORMAT_RGBA: return VideoFormat::PixelFormat::Rgba;
    case SPA_VIDEO_FORMAT_RGBx: return VideoFormat::PixelFormat::Rgbx;
    default: return VideoFormat::PixelFormat::Bgra;
    }
}

}  // namespace

PipeWireCapture::~PipeWireCapture() { stop(); }

void PipeWireCapture::start(const int remoteFd, const std::uint32_t nodeId, const int width,
                            const int height, const int fps, FrameCallback callback) {
    stop();
    static std::once_flag initialized;
    std::call_once(initialized, [] { pw_init(nullptr, nullptr); });

    callback_ = std::move(callback);
    loop_ = pw_thread_loop_new("opendisplay-capture", nullptr);
    if (loop_ == nullptr) {
        ::close(remoteFd);
        throw std::runtime_error("cannot create PipeWire thread loop");
    }
    context_ = pw_context_new(pw_thread_loop_get_loop(loop_), nullptr, 0);
    if (context_ == nullptr) {
        ::close(remoteFd);
        stop();
        throw std::runtime_error("cannot create PipeWire context");
    }
    core_ = pw_context_connect_fd(context_, remoteFd, nullptr, 0);
    if (core_ == nullptr) {
        ::close(remoteFd);
        stop();
        throw std::runtime_error("cannot connect to the portal PipeWire remote");
    }

    auto* properties = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Video",
        PW_KEY_MEDIA_CATEGORY, "Capture",
        PW_KEY_MEDIA_ROLE, "Screen",
        PW_KEY_APP_NAME, "OpenDisplay",
        nullptr);
    stream_ = pw_stream_new(core_, "OpenDisplay KDE capture", properties);
    if (stream_ == nullptr) {
        stop();
        throw std::runtime_error("cannot create PipeWire capture stream");
    }
    pw_stream_add_listener(stream_, &listener_, &streamEvents, this);

    std::uint8_t storage[1024]{};
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(storage, sizeof(storage));
    const spa_rectangle requestedSize = SPA_RECTANGLE(
        static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
    const spa_fraction requestedRate = SPA_FRACTION(static_cast<std::uint32_t>(fps), 1);
    const spa_pod* parameters[1];
    parameters[0] = static_cast<spa_pod*>(spa_pod_builder_add_object(
        &builder,
        SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
        SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
        SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
        SPA_FORMAT_VIDEO_format, SPA_POD_Id(SPA_VIDEO_FORMAT_BGRA),
        SPA_FORMAT_VIDEO_size, SPA_POD_Rectangle(&requestedSize),
        SPA_FORMAT_VIDEO_framerate, SPA_POD_Fraction(&requestedRate)));

    const auto flags = static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT
        | PW_STREAM_FLAG_MAP_BUFFERS);
    const int result = pw_stream_connect(stream_, PW_DIRECTION_INPUT, nodeId, flags,
                                         parameters, 1);
    if (result < 0) {
        stop();
        throw std::runtime_error(std::string("cannot connect PipeWire stream: ")
                                 + spa_strerror(result));
    }
    if (pw_thread_loop_start(loop_) < 0) {
        stop();
        throw std::runtime_error("cannot start PipeWire thread loop");
    }
}

void PipeWireCapture::stop() {
    if (loop_ != nullptr) {
        pw_thread_loop_stop(loop_);
    }
    if (stream_ != nullptr) {
        spa_hook_remove(&listener_);
        pw_stream_destroy(stream_);
        stream_ = nullptr;
    }
    if (core_ != nullptr) {
        pw_core_disconnect(core_);
        core_ = nullptr;
    }
    if (context_ != nullptr) {
        pw_context_destroy(context_);
        context_ = nullptr;
    }
    if (loop_ != nullptr) {
        pw_thread_loop_destroy(loop_);
        loop_ = nullptr;
    }
    callback_ = {};
    format_ = {};
}

void PipeWireCapture::stateChanged(void*, pw_stream_state, const pw_stream_state state,
                                   const char* error) {
    if (state == PW_STREAM_STATE_ERROR) {
        log(std::string("PipeWire stream error: ") + (error != nullptr ? error : "unknown"));
    } else if (state == PW_STREAM_STATE_STREAMING) {
        log("PipeWire capture is streaming");
    }
}

void PipeWireCapture::parameterChanged(void* data, const std::uint32_t id,
                                       const spa_pod* parameter) {
    auto& self = *static_cast<PipeWireCapture*>(data);
    if (parameter == nullptr || id != SPA_PARAM_Format) {
        return;
    }
    if (spa_format_video_raw_parse(parameter, &self.format_) < 0) {
        log("PipeWire returned an unsupported video format");
        self.format_ = {};
        return;
    }
    log("PipeWire format: " + std::to_string(self.format_.size.width) + "x"
        + std::to_string(self.format_.size.height));
}

void PipeWireCapture::process(void* data) {
    static_cast<PipeWireCapture*>(data)->handleProcess();
}

void PipeWireCapture::handleProcess() {
    pw_buffer* pipewireBuffer = pw_stream_dequeue_buffer(stream_);
    if (pipewireBuffer == nullptr) {
        return;
    }
    auto* buffer = pipewireBuffer->buffer;
    if (buffer->n_datas == 0 || buffer->datas[0].data == nullptr
        || buffer->datas[0].chunk == nullptr || format_.size.width == 0) {
        pw_stream_queue_buffer(stream_, pipewireBuffer);
        return;
    }

    const auto& data = buffer->datas[0];
    const auto* chunk = data.chunk;
    const auto width = static_cast<int>(format_.size.width);
    const auto height = static_cast<int>(format_.size.height);
    const int sourceStride = chunk->stride > 0 ? chunk->stride : width * 4;
    const int targetStride = width * 4;
    const auto required = static_cast<std::uint64_t>(chunk->offset)
        + static_cast<std::uint64_t>(sourceStride) * static_cast<std::uint64_t>(height - 1)
        + static_cast<std::uint64_t>(targetStride);
    if (sourceStride < targetStride || required > data.maxsize) {
        pw_stream_queue_buffer(stream_, pipewireBuffer);
        return;
    }
    const auto* source = static_cast<const char*>(data.data) + chunk->offset;

    CapturedFrame frame;
    frame.format = VideoFormat{
        .width = width,
        .height = height,
        .stride = targetStride,
        .fps = format_.framerate.denom > 0
            ? static_cast<int>(format_.framerate.num / format_.framerate.denom)
            : 60,
        .pixelFormat = pixelFormat(format_.format),
    };
    frame.capturedAtMs = wallClockMs();
    frame.sequence = sequence_.fetch_add(1);
    frame.bytes.resize(static_cast<std::size_t>(targetStride * height));
    for (int row = 0; row < height; ++row) {
        std::memcpy(frame.bytes.data() + static_cast<std::size_t>(row * targetStride),
                    source + static_cast<std::size_t>(row * sourceStride),
                    static_cast<std::size_t>(targetStride));
    }
    pw_stream_queue_buffer(stream_, pipewireBuffer);
    if (callback_) {
        callback_(std::move(frame));
    }
}

}  // namespace od
