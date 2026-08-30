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
    // The width/height args are informational only (the advertised panel size).
    // Do NOT pre-set width_/height_ here: the Mac streams at quality-scaled
    // resolution (1440x810 at .balanced, 960x540 at .fast), which may differ
    // from the advertised panel size. readError() discovers the ACTUAL stream
    // dimensions from ffmpeg's stderr and sets them; leaving them 0 lets that
    // discovery run. Slicing at the wrong size produces garbage.
    width_ = 0;
    height_ = 0;
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
    return {
        // "info" (not "error") so ffmpeg prints the stream dimensions on
        // stderr, which readError() parses to discover the actual frame size.
        "ffmpeg", "-hide_banner", "-loglevel", "info", "-nostdin",
        "-f", "h264", "-i", "pipe:0", "-an",
        "-f", "rawvideo", "-pixel_format", "bgra", "pipe:1",
    };
}

void FfmpegDecoder::startProcess() {
    int inputPipe[2]{};
    int outputPipe[2]{};
    int errorPipe[2]{};
    if (::pipe2(inputPipe, O_CLOEXEC) != 0 || ::pipe2(outputPipe, O_CLOEXEC) != 0
        || ::pipe2(errorPipe, O_CLOEXEC) != 0) {
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
        ::close(errorPipe[0]); ::close(errorPipe[1]);
        throw std::runtime_error("cannot fork FFmpeg decoder");
    }
    if (pid == 0) {
        ::dup2(inputPipe[0], STDIN_FILENO);
        ::dup2(outputPipe[1], STDOUT_FILENO);
        ::dup2(errorPipe[1], STDERR_FILENO);
        ::close(inputPipe[0]); ::close(inputPipe[1]);
        ::close(outputPipe[0]); ::close(outputPipe[1]);
        ::close(errorPipe[0]); ::close(errorPipe[1]);
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
    ::close(errorPipe[1]);
    inputFd_ = inputPipe[1];
    outputFd_ = outputPipe[0];
    errorFd_ = errorPipe[0];
    childPid_ = static_cast<int>(pid);
    reader_ = std::thread(&FfmpegDecoder::readOutput, this, outputFd_);
    errorReader_ = std::thread(&FfmpegDecoder::readError, this, errorFd_);
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
    if (errorFd_ >= 0) {
        ::close(errorFd_);
        errorFd_ = -1;
    }
    if (errorReader_.joinable()) {
        errorReader_.join();
    }
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

void FfmpegDecoder::readError(const int fd) {
    // ffmpeg reports the decoded stream dimensions on stderr, e.g.
    // "Stream #0:0: Video: rawvideo ... 1440x810 ...". Parse the WxH so we can
    // slice the rawvideo output at the ACTUAL stream size (the Mac streams at
    // quality-scaled resolution, not necessarily the advertised panel size).
    std::string buffer;
    std::array<char, 4096> chunk{};
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
        std::lock_guard lock(dimsMutex_);
        if (width_ == 0 || height_ == 0) {
            // Find a "WxH" token like "1440x810". The digit run must be
            // IMMEDIATELY followed by 'x' (so "4:4:4 ... 64x64" does not match
            // the '4' before the colon). Skip hex fourccs like "0x50343434".
            std::size_t pos = 0;
            while (pos < buffer.size()) {
                const auto start = buffer.find_first_of("0123456789", pos);
                if (start == std::string::npos) break;
                // Skip hex literals like "0x50343434".
                if (start >= 2 && buffer[start - 2] == '0' && buffer[start - 1] == 'x') {
                    pos = start + 1;
                    continue;
                }
                // The char right after the digit run must be 'x'.
                auto end = start;
                while (end < buffer.size() && std::isdigit(static_cast<unsigned char>(buffer[end]))) {
                    ++end;
                }
                if (end >= buffer.size() || buffer[end] != 'x') {
                    pos = end + 1;
                    continue;
                }
                // The char after 'x' must be a digit (start of the height).
                if (end + 1 < buffer.size()
                    && std::isdigit(static_cast<unsigned char>(buffer[end + 1]))) {
                    const auto width = std::atoi(buffer.c_str() + start);
                    const auto height = std::atoi(buffer.c_str() + end + 1);
                    if (width > 0 && height > 0) {
                        width_ = width;
                        height_ = height;
                        debug("FFmpeg decoder stream is " + std::to_string(width) + 'x'
                              + std::to_string(height));
                        break;
                    }
                }
                pos = end + 1;
            }
        }
    }
}

void FfmpegDecoder::readOutput(const int fd) {
    // Read rawvideo frames from ffmpeg's stdout. The frame size depends on the
    // ACTUAL stream dimensions, which readError() discovers from stderr. Wait
    // for them before slicing so we never misalign frames.
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
        int width = 0, height = 0;
        {
            std::lock_guard lock(dimsMutex_);
            width = width_;
            height = height_;
        }
        // Only slice once the ACTUAL stream dimensions are discovered from
        // ffmpeg's stderr. Slicing at the hint (1920x1080) before discovery
        // produces misaligned garbage (black/white scrolling, half-black).
        if (width <= 0 || height <= 0) {
            continue;  // dimensions not known yet; keep buffering
        }
        const auto frameSize = static_cast<std::size_t>(width) * height * 4;
        while (buffer.size() >= frameSize) {
            DecodedFrame frame;
            frame.width = width;
            frame.height = height;
            frame.bgra.assign(buffer.data(), frameSize);
            buffer.erase(0, frameSize);
            if (callback_) {
                callback_(std::move(frame));
            }
        }
    }
}

}  // namespace od

