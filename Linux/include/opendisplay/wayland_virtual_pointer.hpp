#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct wl_display;
struct wl_output;
struct wl_registry;
struct zwlr_virtual_pointer_manager_v1;
struct zwlr_virtual_pointer_v1;

namespace od {

class WaylandVirtualPointer {
public:
    WaylandVirtualPointer();
    ~WaylandVirtualPointer();
    WaylandVirtualPointer(const WaylandVirtualPointer&) = delete;
    WaylandVirtualPointer& operator=(const WaylandVirtualPointer&) = delete;

    void start(const std::string& outputName);
    void stop();
    void pointer(std::string_view phase, double normalizedX, double normalizedY);
    void scroll(double dx, double dy);

private:
    struct Output;
    static void registryGlobal(void* data, wl_registry* registry, unsigned name,
                               const char* interface, unsigned version);
    static void registryGlobalRemove(void* data, wl_registry* registry, unsigned name);
    static void outputGeometry(void*, wl_output*, int, int, int, int, int,
                               const char*, const char*, int);
    static void outputMode(void*, wl_output*, unsigned, int, int, int);
    static void outputDone(void*, wl_output*);
    static void outputScale(void*, wl_output*, int);
    static void outputName(void* data, wl_output*, const char* name);
    static void outputDescription(void*, wl_output*, const char*);
    void flush();

    wl_display* display_ = nullptr;
    wl_registry* registry_ = nullptr;
    zwlr_virtual_pointer_manager_v1* manager_ = nullptr;
    zwlr_virtual_pointer_v1* pointer_ = nullptr;
    std::vector<std::unique_ptr<Output>> outputs_;
    bool managerSupportsOutput_ = false;
    bool pointerDown_ = false;
};

}  // namespace od
