#include "opendisplay/desktop_backend_factory.hpp"
#include "opendisplay/hyprland_output_controller.hpp"

#include <cassert>
#include <cmath>
#include <string>

namespace {

void detectsDesktopWithoutChangingLegacyFallback() {
    using od::CompositorKind;
    assert(od::detectCompositor(CompositorKind::Auto, "Hyprland", "", "")
           == CompositorKind::Hyprland);
    assert(od::detectCompositor(CompositorKind::Auto, "", "instance", "")
           == CompositorKind::Hyprland);
    assert(od::detectCompositor(CompositorKind::Auto, "KDE", "", "true")
           == CompositorKind::Kde);
    assert(od::detectCompositor(CompositorKind::Auto, "unknown", "", "")
           == CompositorKind::Kde);
    assert(od::detectCompositor(CompositorKind::Kde, "Hyprland", "instance", "")
           == CompositorKind::Kde);
}

void parsesHyprctlMonitorJson() {
    const std::string json = R"([{
        "id": 1, "name": "eDP-1", "width": 2560, "height": 1600,
        "x": -1707, "y": 0, "scale": 1.5, "transform": 0,
        "physicalWidth": 380, "physicalHeight": 240,
        "disabled": false,
        "availableModes": ["2560x1600@60.00100Hz", "1920x1200@60.00000Hz"]
    }])";
    const auto outputs = od::parseHyprlandOutputs(json);
    assert(outputs.size() == 1);
    assert(outputs.front().name == "eDP-1");
    assert(outputs.front().logicalGeometry.x == -1707);
    assert(outputs.front().logicalGeometry.width == 1707);
    assert(outputs.front().physicalSize.has_value());
    assert(outputs.front().physicalSize->widthMm == 380);
    assert(outputs.front().modes.size() == 2);
    assert(std::abs(outputs.front().modes.front().refreshRate - 60.001) < 0.001);
}

void producesCurrentHyprlandMonitorExpression() {
    od::DisplayLayout layout;
    layout.resolution = {.width = 2420, .height = 1668};
    layout.logicalGeometry = {.x = 1707, .y = 233, .width = 1210, .height = 834};
    layout.scale = 2.0;
    layout.refreshRate = 60;
    const auto expression = od::hyprlandMonitorExpression("OpenDisplay-42", layout);
    assert(expression == "hl.monitor({ output = \"OpenDisplay-42\", mode = "
                         "\"2420x1668@60\", position = \"1707x233\", scale = 2 })");
}

}  // namespace

int main() {
    detectsDesktopWithoutChangingLegacyFallback();
    parsesHyprctlMonitorJson();
    producesCurrentHyprlandMonitorExpression();
}
