#pragma once

#include "opendisplay/display_layout.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace od {

std::vector<DisplayOutput> parseHyprlandOutputs(std::string_view json);
bool hyprlandCommandResponseAccepted(bool processSucceeded, std::string_view output);
std::string hyprlandMonitorExpression(const std::string& outputName,
                                      const DisplayLayout& layout);
std::string hyprlandFocusExpression(const std::string& outputName);

class HyprlandOutputController {
public:
    std::vector<DisplayOutput> outputs() const;
    DisplayOutput create(const std::string& outputName,
                         const DisplayLayout& layout) const;
    void focus(const std::string& outputName) const;
    void remove(const std::string& outputName) const;
};

}  // namespace od
