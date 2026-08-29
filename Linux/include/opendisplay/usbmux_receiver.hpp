#pragma once

#include "opendisplay/socket.hpp"
#include "opendisplay/types.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace od {

/// Implements the usbmuxd server protocol (the open protocol spoken by
/// libimobiledevice's usbmuxd) over TCP, so a sender using libusbmuxd can
/// reach this receiver with its existing USB code path by pointing
/// USBMUXD_SOCKET_ADDRESS at us. Advertises a single virtual iOS-style USB
/// device and, on Connect, hands the socket to the receiver session.
class UsbmuxReceiver {
public:
    /// Called with an accepted, mux-negotiated socket carrying the raw
    /// OpenDisplay wire stream.
    using ConnectionCallback = std::function<void(Socket)>;

    UsbmuxReceiver() = default;
    ~UsbmuxReceiver();
    UsbmuxReceiver(const UsbmuxReceiver&) = delete;
    UsbmuxReceiver& operator=(const UsbmuxReceiver&) = delete;

    void start(std::uint16_t port, ConnectionCallback onConnection);
    void stop();
    [[nodiscard]] std::uint16_t boundPort() const { return boundPort_.load(); }

private:
    void acceptLoop();
    void clientLoop(Socket client);

    Socket listener_;
    std::thread acceptThread_;
    std::atomic_bool running_ = false;
    std::atomic<std::uint16_t> boundPort_ = 0;
    ConnectionCallback onConnection_;
    std::string serial_;
    static constexpr std::uint32_t kVirtualDeviceId = 1;
};

}  // namespace od