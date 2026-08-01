#pragma once

#include "opendisplay/types.hpp"

#include <optional>
#include <string>
#include <vector>

namespace od {

struct DisplayMode {
    std::string id;
    Size size;
    double refreshRate = 0;
};

/// Compositor-neutral description of an output. Platform controllers translate
/// their native output API into this representation before layout is planned.
struct DisplayOutput {
    std::string id;
    std::string name;
    bool connected = false;
    bool enabled = false;
    Size resolution;
    Rect logicalGeometry;
    double scale = 1.0;
    std::optional<PhysicalSize> physicalSize;
    std::vector<DisplayMode> modes;
};

struct DisplayLayout {
    DisplayOutput reference;
    Size resolution;
    double scale = 1.0;
    Rect logicalGeometry;
    int refreshRate = 60;
    bool usedPhysicalSizing = false;
    bool adjustedResolution = false;
};

DisplayOutput selectReferenceOutput(const std::vector<DisplayOutput>& outputs,
                                    const std::string& requested);
DisplayLayout planDisplayLayout(const DisplayOutput& detectedReference,
                                const PhoneInfo& receiver,
                                const DisplayOptions& options,
                                int defaultRefreshRate);

}  // namespace od
