#include "opendisplay/wayland_virtual_pointer.hpp"

#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"

#include <linux/input-event-codes.h>
#include <wayland-client.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace od {
namespace {

constexpr std::uint32_t absoluteExtent = 1'000'000;

std::uint32_t eventTime() {
    const auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

}  // namespace

struct WaylandVirtualPointer::Output {
    unsigned globalName = 0;
    wl_output* native = nullptr;
    std::string name;
};

WaylandVirtualPointer::WaylandVirtualPointer() = default;

WaylandVirtualPointer::~WaylandVirtualPointer() { stop(); }

void WaylandVirtualPointer::registryGlobal(void* data, wl_registry* registry,
                                           const unsigned name, const char* interface,
                                           const unsigned version) {
    auto& self = *static_cast<WaylandVirtualPointer*>(data);
    if (std::strcmp(interface, zwlr_virtual_pointer_manager_v1_interface.name) == 0) {
        self.managerSupportsOutput_ = version >= 2;
        self.manager_ = static_cast<zwlr_virtual_pointer_manager_v1*>(wl_registry_bind(
            registry, name, &zwlr_virtual_pointer_manager_v1_interface,
            std::min(version, 2U)));
    } else if (std::strcmp(interface, wl_output_interface.name) == 0) {
        auto output = std::make_unique<Output>();
        output->globalName = name;
        output->native = static_cast<wl_output*>(wl_registry_bind(
            registry, name, &wl_output_interface, std::min(version, 4U)));
        static const wl_output_listener listener{
            .geometry = outputGeometry,
            .mode = outputMode,
            .done = outputDone,
            .scale = outputScale,
            .name = outputName,
            .description = outputDescription,
        };
        wl_output_add_listener(output->native, &listener, output.get());
        self.outputs_.push_back(std::move(output));
    }
}

void WaylandVirtualPointer::registryGlobalRemove(void* data, wl_registry*,
                                                 const unsigned name) {
    auto& outputs = static_cast<WaylandVirtualPointer*>(data)->outputs_;
    const auto found = std::find_if(outputs.begin(), outputs.end(), [&](const auto& output) {
        return output->globalName == name;
    });
    if (found != outputs.end()) {
        wl_output_destroy((*found)->native);
        outputs.erase(found);
    }
}

void WaylandVirtualPointer::outputGeometry(void*, wl_output*, int, int, int, int, int,
                                           const char*, const char*, int) {}
void WaylandVirtualPointer::outputMode(void*, wl_output*, unsigned, int, int, int) {}
void WaylandVirtualPointer::outputDone(void*, wl_output*) {}
void WaylandVirtualPointer::outputScale(void*, wl_output*, int) {}
void WaylandVirtualPointer::outputName(void* data, wl_output*, const char* name) {
    static_cast<Output*>(data)->name = name ? name : "";
}
void WaylandVirtualPointer::outputDescription(void*, wl_output*, const char*) {}

void WaylandVirtualPointer::start(const std::string& outputName) {
    stop();
    display_ = wl_display_connect(nullptr);
    if (!display_) {
        throw std::runtime_error("cannot connect to the Hyprland Wayland display");
    }
    registry_ = wl_display_get_registry(display_);
    static const wl_registry_listener registryListener{
        .global = registryGlobal,
        .global_remove = registryGlobalRemove,
    };
    wl_registry_add_listener(registry_, &registryListener, this);
    if (wl_display_roundtrip(display_) < 0 || wl_display_roundtrip(display_) < 0) {
        stop();
        throw std::runtime_error("Wayland connection failed while discovering input protocols");
    }
    if (!manager_) {
        stop();
        throw std::runtime_error(
            "Hyprland does not advertise zwlr_virtual_pointer_manager_v1; use --no-input");
    }
    if (!managerSupportsOutput_) {
        stop();
        throw std::runtime_error(
            "Hyprland only advertises virtual-pointer v1, which cannot target an output; "
            "use --no-input");
    }
    const auto output = std::find_if(outputs_.begin(), outputs_.end(), [&](const auto& item) {
        return item->name == outputName;
    });
    if (output == outputs_.end()) {
        std::ostringstream names;
        bool first = true;
        for (const auto& item : outputs_) {
            if (!first) names << ", ";
            names << (item->name.empty() ? "<unnamed>" : item->name);
            first = false;
        }
        stop();
        throw std::runtime_error("Wayland does not advertise output '" + outputName
                                 + "' for pointer input; available outputs: " + names.str());
    }
    pointer_ = zwlr_virtual_pointer_manager_v1_create_virtual_pointer_with_output(
        manager_, nullptr, (*output)->native);
    if (!pointer_) {
        stop();
        throw std::runtime_error("Hyprland refused to create a virtual pointer; use --no-input");
    }
    flush();
}

void WaylandVirtualPointer::stop() {
    if (pointer_) {
        zwlr_virtual_pointer_v1_destroy(pointer_);
        pointer_ = nullptr;
    }
    if (manager_) {
        zwlr_virtual_pointer_manager_v1_destroy(manager_);
        manager_ = nullptr;
    }
    for (const auto& output : outputs_) {
        if (output->native) wl_output_destroy(output->native);
    }
    outputs_.clear();
    if (registry_) {
        wl_registry_destroy(registry_);
        registry_ = nullptr;
    }
    if (display_) {
        wl_display_flush(display_);
        wl_display_disconnect(display_);
        display_ = nullptr;
    }
    managerSupportsOutput_ = false;
    pointerDown_ = false;
}

void WaylandVirtualPointer::flush() {
    if (display_ && wl_display_flush(display_) < 0) {
        throw std::runtime_error("Wayland connection failed while sending pointer input");
    }
}

void WaylandVirtualPointer::pointer(const std::string_view phase, const double normalizedX,
                                    const double normalizedY) {
    if (!pointer_) return;
    const auto x = static_cast<std::uint32_t>(std::lround(
        std::clamp(normalizedX, 0.0, 1.0) * absoluteExtent));
    const auto y = static_cast<std::uint32_t>(std::lround(
        std::clamp(normalizedY, 0.0, 1.0) * absoluteExtent));
    const auto time = eventTime();
    zwlr_virtual_pointer_v1_motion_absolute(pointer_, time, x, y,
                                            absoluteExtent, absoluteExtent);
    if (phase == "began") {
        pointerDown_ = true;
        zwlr_virtual_pointer_v1_button(pointer_, time, BTN_LEFT,
                                       WL_POINTER_BUTTON_STATE_PRESSED);
    } else if ((phase == "ended" || phase == "cancelled") && pointerDown_) {
        pointerDown_ = false;
        zwlr_virtual_pointer_v1_button(pointer_, time, BTN_LEFT,
                                       WL_POINTER_BUTTON_STATE_RELEASED);
    }
    zwlr_virtual_pointer_v1_frame(pointer_);
    flush();
}

void WaylandVirtualPointer::scroll(const double dx, const double dy) {
    if (!pointer_) return;
    const auto time = eventTime();
    zwlr_virtual_pointer_v1_axis_source(pointer_, WL_POINTER_AXIS_SOURCE_FINGER);
    if (dx != 0) {
        zwlr_virtual_pointer_v1_axis(pointer_, time, WL_POINTER_AXIS_HORIZONTAL_SCROLL,
                                     wl_fixed_from_double(dx));
    }
    if (dy != 0) {
        zwlr_virtual_pointer_v1_axis(pointer_, time, WL_POINTER_AXIS_VERTICAL_SCROLL,
                                     wl_fixed_from_double(dy));
    }
    zwlr_virtual_pointer_v1_frame(pointer_);
    flush();
}

}  // namespace od
