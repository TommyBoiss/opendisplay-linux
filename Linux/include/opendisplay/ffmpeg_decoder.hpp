#pragma once

#include "opendisplay/types.hpp"

#include <QMetaType>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace od {

/// A decoded BGRA frame ready for display.
struct DecodedFrame {
    int width = 0;
    int height = 0;
    std::int64_t capturedAtMs = 0;
    std::string bgra;  // width*height*4 bytes
};

/// Low-latency FFmpeg subprocess decoder. Feed Annex B H.264 bytes via
/// submit(); decoded BGRA frames arrive on the callback. Mirrors the
/// FfmpegEncoder subprocess design so no libav* is linked.
class FfmpegDecoder {
public:
    using FrameCallback = std::function<void(DecodedFrame)>;

    FfmpegDecoder() = default;
    ~FfmpegDecoder();
    FfmpegDecoder(const FfmpegDecoder&) = delete;
    FfmpegDecoder& operator=(const FfmpegDecoder&) = delete;

    void start(FrameCallback callback, int width, int height);
    void submit(std::string annexB);
    void stop();
    [[nodiscard]] bool running() const { return running_.load(); }

private:
    void run();
    void startProcess();
    void stopProcess();
    void readOutput(int fd);
    void readError(int fd);
    std::vector<std::string> arguments() const;

    FrameCallback callback_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<std::string> pending_;
    std::thread worker_;
    std::thread reader_;
    std::thread errorReader_;
    int inputFd_ = -1;
    int outputFd_ = -1;
    int errorFd_ = -1;
    int childPid_ = -1;
    std::atomic_bool running_ = false;
    std::mutex dimsMutex_;
    int width_ = 0;
    int height_ = 0;
    int hintWidth_ = 0;
    int hintHeight_ = 0;
};

}  // namespace od

Q_DECLARE_METATYPE(od::DecodedFrame)
