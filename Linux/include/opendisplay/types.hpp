#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace od {

enum class TransportKind { Auto, Wifi, Usb };
enum class CaptureMode { Extend, Mirror };
enum class EncoderKind { Auto, Vaapi, Nvenc, Software };

struct PhoneInfo {
    int pixelsWide = 0;
    int pixelsHigh = 0;
    double scale = 2.0;
    std::string device = "device";
    std::string installId;
    int protocolVersion = 1;
};

struct Endpoint {
    TransportKind kind = TransportKind::Wifi;
    std::string name;
    std::string host;
    std::uint16_t port = 9000;
    std::string udid;
    int usbHandle = -1;
};

struct VideoFormat {
    enum class PixelFormat { Bgra, Bgrx, Rgba, Rgbx };

    int width = 0;
    int height = 0;
    int stride = 0;
    int fps = 60;
    PixelFormat pixelFormat = PixelFormat::Bgra;
};

struct CapturedFrame {
    VideoFormat format;
    std::int64_t capturedAtMs = 0;
    std::uint64_t sequence = 0;
    std::string bytes;
};

struct EncodedFrame {
    std::int64_t capturedAtMs = 0;
    bool keyframe = false;
    std::string annexB;
};

struct Options {
    TransportKind transport = TransportKind::Auto;
    CaptureMode mode = CaptureMode::Extend;
    EncoderKind encoder = EncoderKind::Auto;
    std::string host;
    std::uint16_t port = 9000;
    std::string serviceName;
    std::string udid;
    std::string vaapiDevice = "/dev/dri/renderD128";
    int fps = 60;
    int bitrate = 18'000'000;
    double scale = 1.0;
    bool input = true;
    bool listDevices = false;
    bool verbose = false;
};

}  // namespace od

