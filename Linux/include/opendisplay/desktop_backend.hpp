#pragma once

#include "opendisplay/types.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace od {

struct PortalStream {
    std::uint32_t nodeId = 0;
    int logicalWidth = 0;
    int logicalHeight = 0;
};

struct PortalCapture {
    std::string sessionPath;
    PortalStream stream;
    int pipewireFd = -1;
};

/// Compositor-specific output/capture authorization and input injection.
/// Transport, protocol, capture, and encoding remain platform-neutral.
class DesktopBackend {
public:
    virtual ~DesktopBackend() = default;
    virtual PortalCapture start(CaptureMode mode, int requestedWidth, int requestedHeight,
                                bool requestInput) = 0;
    virtual void stop() = 0;
    virtual void pointer(std::string_view phase, double normalizedX, double normalizedY) = 0;
    virtual void scroll(double dx, double dy) = 0;
};

}  // namespace od
