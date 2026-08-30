#include "opendisplay/ffmpeg_decoder.hpp"
#include "opendisplay/ffmpeg_encoder.hpp"

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

int main() {
    // Encode a few frames with the real encoder, then decode them back.
    std::vector<od::EncodedFrame> encoded;
    std::mutex encodeMutex;
    std::condition_variable encodeCondition;
    od::FfmpegEncoder encoder;
    encoder.start(od::EncoderConfig{
                      .kind = od::EncoderKind::Software,
                      .outputWidth = 64,
                      .outputHeight = 64,
                      .fps = 30,
                      .bitrate = 300'000,
                  },
                  [&](od::EncodedFrame frame) {
                      {
                          std::lock_guard lock(encodeMutex);
                          encoded.push_back(std::move(frame));
                      }
                      encodeCondition.notify_all();
                  });

    for (int index = 0; index < 8; ++index) {
        od::CapturedFrame frame;
        frame.format = od::VideoFormat{.width = 64, .height = 64, .stride = 256, .fps = 30};
        frame.capturedAtMs = 1000 + index;
        frame.bytes.assign(64 * 64 * 4, static_cast<char>(index * 16));
        encoder.submit(std::move(frame));
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }

    // Wait for at least one encoded frame.
    {
        std::unique_lock lock(encodeMutex);
        encodeCondition.wait_for(lock, std::chrono::seconds(5),
                                 [&] { return !encoded.empty(); });
    }
    assert(!encoded.empty());
    encoder.stop();

    // Feed the encoded Annex B into the decoder. Pass a WRONG hint (32x32) to
    // prove the decoder discovers the real 64x64 stream dimensions from
    // ffmpeg's stderr rather than trusting the hint (the Mac streams at
    // quality-scaled resolution, which may differ from the advertised panel).
    std::vector<od::DecodedFrame> decoded;
    std::mutex decodeMutex;
    std::condition_variable decodeCondition;
    od::FfmpegDecoder decoder;
    decoder.start([&](od::DecodedFrame frame) {
        {
            std::lock_guard lock(decodeMutex);
            decoded.push_back(std::move(frame));
        }
        decodeCondition.notify_all();
    }, 32, 32);
    for (const auto& frame : encoded) {
        decoder.submit(frame.annexB);
    }

    // Wait for at least one decoded frame.
    {
        std::unique_lock lock(decodeMutex);
        decodeCondition.wait_for(lock, std::chrono::seconds(5),
                                 [&] { return !decoded.empty(); });
    }
    decoder.stop();

    assert(!decoded.empty());
    // The decoder must have discovered the real 64x64 size, not the 32x32 hint.
    assert(decoded.front().width == 64);
    assert(decoded.front().height == 64);
    assert(decoded.front().bgra.size() == 64 * 64 * 4);
    return 0;
}
