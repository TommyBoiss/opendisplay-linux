#include "opendisplay/ffmpeg_decoder.hpp"

#include "opendisplay/log.hpp"

#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string_view>

namespace od {
namespace {

bool writeAll(const int fd, const std::string_view bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto count = ::write(fd, bytes.data() + offset, bytes.size() - offset);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (count == 0) {
            return false;
        }
        offset += static_cast<std::size_t>(count);
    }
    return true;
}

}  // namespace

FfmpegDecoder::~FfmpegDecoder() { stop(); }

void FfmpegDecoder::start(FrameCallback callback, const int width, const int height) {
    stop();
    std::signal(SIGPIPE, SIG_IGN);
    callback_ = std::move(callback);
    // Fix the OUTPUT size: ffmpeg scales every decoded frame to this exact
    // size, so the rawvideo output is ALWAYS width*height*4 bytes per frame.
    // This makes slicing deterministic — no stderr dimension discovery and no
    // risk of misaligned frames (black/white scrolling, half-black) that
    // happened when the stream and slice sizes differed.
    width_ = std::max(width, 1);
    height_ = std::max(height, 1);
    running_.store(true);
    worker_ = std::thread(&FfmpegDecoder::run, this);
}

void FfmpegDecoder::submit(std::string annexB) {
    {
        std::lock_guard lock(mutex_);
        if (!running_) {
            return;
        }
        pending_.push_back(std::move(annexB));
    }
    condition_.notify_one();
}

void FfmpegDecoder::stop() {
    {
        std::lock_guard lock(mutex_);
        running_ = false;
        pending_.clear();
    }
    condition_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
    stopProcess();
    callback_ = {};
}

std::vector<std::string> FfmpegDecoder::arguments() const {
    // `scale=` forces every decoded frame to the fixed width_/height_, so the
    // rawvideo output is always exactly width_*height_*4 bytes per frame. This
    // removes any dependence on the stream's native size (which varies with the
    // Mac's quality setting) and makes slicing deterministic.
    const std::string size = std::to_string(width_) + ':' + std::to_string(height_);
    return {
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-nostdin",
        "-f", "h264", "-i", "pipe:0", "-an",
        "-vf", "scale=" + size, "-pix_fmt", "bgra",
        "-f", "rawvideo", "pipe:1",
    };
}

void FfmpegDecoder::startProcess() {
    int inputPipe[2]{};
    int outputPipe[2]{};
    if (::pipe2(inputPipe, O_CLOEXEC) != 0 || ::pipe2(outputPipe, O_CLOEXEC) != 0) {
        if (inputPipe[0] > 0) ::close(inputPipe[0]);
        if (inputPipe[1] > 0) ::close(inputPipe[1]);
        if (outputPipe[0] > 0) ::close(outputPipe[0]);
        if (outputPipe[1] > 0) ::close(outputPipe[1]);
        throw std::runtime_error("cannot create FFmpeg decoder pipes");
    }
    const auto args = arguments();
    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(inputPipe[0]); ::close(inputPipe[1]);
        ::close(outputPipe[0]); ::close(outputPipe[1]);
        throw std::runtime_error("cannot fork FFmpeg decoder");
    }
    if (pid == 0) {
        ::dup2(inputPipe[0], STDIN_FILENO);
        ::dup2(outputPipe[1], STDOUT_FILENO);
        ::close(inputPipe[0]); ::close(inputPipe[1]);
        ::close(outputPipe[0]); ::close(outputPipe[1]);
        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const auto& argument : args) {
            argv.push_back(const_cast<char*>(argument.c_str()));
        }
        argv.push_back(nullptr);
        ::execvp(argv.front(), argv.data());
        _exit(127);
    }
    ::close(inputPipe[0]);
    ::close(outputPipe[1]);
    inputFd_ = inputPipe[1];
    outputFd_ = outputPipe[0];
    childPid_ = static_cast<int>(pid);
    reader_ = std::thread(&FfmpegDecoder::readOutput, this, outputFd_);
}

void FfmpegDecoder::stopProcess() {
    if (inputFd_ >= 0) {
        ::close(inputFd_);
        inputFd_ = -1;
    }
    if (reader_.joinable()) {
        reader_.join();
    }
    outputFd_ = -1;
    if (childPid_ > 0) {
        int status = 0;
        while (::waitpid(childPid_, &status, 0) < 0 && errno == EINTR) {}
        childPid_ = -1;
    }
}

void FfmpegDecoder::run() {
    try {
        for (;;) {
            std::string annexB;
            {
                std::unique_lock lock(mutex_);
                condition_.wait(lock, [&] { return !running_ || !pending_.empty(); });
                if (!running_) {
                    break;
                }
                annexB = std::move(pending_.front());
                pending_.pop_front();
            }
            if (inputFd_ < 0) {
                startProcess();
            }
            if (!writeAll(inputFd_, annexB)) {
                log("FFmpeg decoder stopped accepting input; restarting it");
                stopProcess();
                startProcess();
            }
        }
    } catch (const std::exception& error) {
        log(std::string("FFmpeg decoder error: ") + error.what());
    }
}

void FfmpegDecoder::readOutput(const int fd) {
    // ffmpeg scales every frame to width_*height_ (see arguments()), so the
    // rawvideo output is always exactly width_*height_*4 bytes per frame.
    // Slice deterministically at that size — no discovery needed.
    const auto frameSize = static_cast<std::size_t>(width_) * height_ * 4;
    std::string buffer;
    std::array<char, 65536> chunk{};
    for (;;) {
        const auto count = ::read(fd, chunk.data(), chunk.size());
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (count == 0) {
            break;
        }
        buffer.append(chunk.data(), static_cast<std::size_t>(count));
        while (buffer.size() >= frameSize) {
            DecodedFrame frame;
            frame.width = width_;
            frame.height = height_;
            frame.bgra.assign(buffer.data(), frameSize);
            buffer.erase(0, frameSize);
            if (callback_) {
                callback_(std::move(frame));
            }
        }
    }
}

}  // namespace od

