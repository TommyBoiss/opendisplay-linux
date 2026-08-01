#pragma once

#include "opendisplay/display_layout.hpp"

#include <chrono>
#include <string>
#include <vector>

namespace od {

struct OutputTranslation {
    int x = 0;
    int y = 0;
};

/// KWin/KScreen implementation of output discovery and configuration. The
/// layout calculations themselves remain compositor-neutral in display_layout.
class KdeOutputController {
public:
    std::vector<DisplayOutput> outputs() const;
    DisplayOutput waitForAddedOutput(const std::vector<DisplayOutput>& before,
                                     std::chrono::milliseconds timeout) const;
    bool waitForRemovedOutput(const DisplayOutput& output,
                              std::chrono::milliseconds timeout) const;
    OutputTranslation apply(const DisplayOutput& output, const DisplayLayout& layout) const;
    void restorePositions(const std::vector<DisplayOutput>& outputs) const;
};

}  // namespace od
