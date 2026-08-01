#pragma once

#include "opendisplay/display_layout.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace od {

std::vector<DisplayOutput> parseHyprlandOutputs(std::string_view json);
std::string hyprlandMonitorExpression(const std::string& outputName,
                                      const DisplayLayout& layout);

class HyprlandOutputController {
public:
    std::vector<DisplayOutput> outputs() const;
    void create(const std::string& outputName) const;
    DisplayOutput configure(const std::string& outputName,
                            const DisplayLayout& layout) const;
    void remove(const std::string& outputName) const;
};

}  // namespace od
