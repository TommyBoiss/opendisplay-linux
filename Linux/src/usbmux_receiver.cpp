#include "opendisplay/usbmux_receiver.hpp"

#include "opendisplay/log.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <stdexcept>

namespace od {
namespace {

// usbmuxd wire protocol constants (from usbmuxd-proto.h).
constexpr std::uint32_t kResultOk = 0;
constexpr std::uint32_t kResultBadCommand = 1;
constexpr std::uint32_t kResultBadDev = 2;
constexpr std::uint32_t kResultConnRefused = 3;

enum class MsgType : std::uint32_t {
    Result = 1,
    Connect = 2,
    Listen = 3,
    DeviceAdd = 4,
    DeviceRemove = 5,
    Plist = 8,
};

std::string plistEscape(const std::string& value) {
    std::string out;
    for (const char c : value) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&apos;"; break;
        default: out += c; break;
        }
    }
    return out;
}

/// A minimal XML plist writer for the few message shapes usbmuxd needs.
std::string makeResultPlist(const std::uint32_t number) {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<plist version=\"1.0\"><dict>"
           "<key>MessageType</key><string>Result</string>"
           "<key>Number</key><integer>" + std::to_string(number) + "</integer>"
           "</dict></plist>";
}

std::string makeAttachedPlist(const std::uint32_t deviceId, const std::string& serial) {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<plist version=\"1.0\"><dict>"
           "<key>MessageType</key><string>Attached</string>"
           "<key>DeviceID</key><integer>" + std::to_string(deviceId) + "</integer>"
           "<key>Properties</key><dict>"
           "<key>SerialNumber</key><string>" + plistEscape(serial) + "</string>"
           "<key>ConnectionType</key><string>USB</string>"
           "<key>LocationID</key><integer>0</integer>"
           "<key>ProductID</key><integer>0</integer>"
           "</dict></dict></plist>";
}

/// Extract a string value for `key` from a flat XML plist dict. usbmuxd
/// messages are small and shallow, so a simple scan suffices.
std::string plistValue(const std::string& xml, const std::string& key) {
    const auto keyTag = "<key>" + key + "</key>";
    auto position = xml.find(keyTag);
    if (position == std::string::npos) return {};
    position = xml.find('>', position + keyTag.size());
    if (position == std::string::npos) return {};
    position += 1;
    auto end = xml.find('<', position);
    if (end == std::string::npos) return {};
    return xml.substr(position, end - position);
}

std::int64_t plistInt(const std::string& xml, const std::string& key,
                      const std::int64_t fallback) {
    const auto value = plistValue(xml, key);
    if (value.empty()) return fallback;
    try {
        return std::stoll(value);
    } catch (...) {
        return fallback;
    }
}

/// Frame a payload with the 16-byte usbmuxd header (plist protocol).
std::string wireFramePlist(const std::string& payload) {
    const auto length = static_cast<std::uint32_t>(payload.size() + 16);
    std::string out(16, '\0');
    std::memcpy(out.data() + 0, &length, 4);
    const std::uint32_t version = 1;
    const std::uint32_t message = static_cast<std::uint32_t>(MsgType::Plist);
    const std::uint32_t tag = 0;
    std::memcpy(out.data() + 4, &version, 4);
    std::memcpy(out.data() + 8, &message, 4);
    std::memcpy(out.data() + 12, &tag, 4);
    out += payload;
    return out;
}

/// Binary RESULT message (usbmuxd_result_msg).
std::string makeResultBinary(const std::uint32_t tag, const std::uint32_t result) {
    struct {
        std::uint32_t length, version, message, tag, result;
    } msg{};
    msg.length = sizeof(msg);
    msg.version = 0;
    msg.message = static_cast<std::uint32_t>(MsgType::Result);
    msg.tag = tag;
    msg.result = result;
    std::string out(reinterpret_cast<const char*>(&msg), sizeof(msg));
    return out;
}

/// Binary DEVICE_ADD message (usbmuxd_device_record). sizeof == 284 bytes:
/// 16 header + 4 device_id + 2 product_id + 256 serial + 2 padding + 4 location.
std::string makeDeviceRecordBinary(const std::uint32_t tag, const std::uint32_t deviceId) {
    struct {
        std::uint32_t length, version, message, tag;
        std::uint32_t device_id;
        std::uint16_t product_id;
        char serial[256];
        std::uint16_t padding;
        std::uint32_t location;
    } record{};
    record.length = sizeof(record);
    record.version = 0;
    record.message = static_cast<std::uint32_t>(MsgType::DeviceAdd);
    record.tag = tag;
    record.device_id = deviceId;
    record.product_id = 0;
    std::strncpy(record.serial, "opendisplay-linux-receiver", sizeof(record.serial) - 1);
    record.padding = 0;
    record.location = 0;
    std::string out(reinterpret_cast<const char*>(&record), sizeof(record));
    return out;
}

}  // namespace

UsbmuxReceiver::~UsbmuxReceiver() { stop(); }

void UsbmuxReceiver::start(const std::uint16_t port, ConnectionCallback onConnection) {
    stop();
    onConnection_ = std::move(onConnection);
    serial_ = "opendisplay-linux-receiver";
    listener_ = listenTcp(port);
    sockaddr_in bound{};
    socklen_t boundLength = sizeof(bound);
    if (::getsockname(listener_.fd(), reinterpret_cast<sockaddr*>(&bound),
                      &boundLength) == 0) {
        boundPort_.store(ntohs(bound.sin_port));
    }
    running_.store(true);
    acceptThread_ = std::thread(&UsbmuxReceiver::acceptLoop, this);
}

void UsbmuxReceiver::stop() {
    running_.store(false);
    if (listener_.valid()) {
        listener_.close();
    }
    if (acceptThread_.joinable()) {
        acceptThread_.join();
    }
}

void UsbmuxReceiver::acceptLoop() {
    while (running_.load()) {
        Socket peer = acceptConnection(listener_);
        if (!peer.valid()) {
            if (running_.load()) continue;
            break;
        }
        try {
            clientLoop(std::move(peer));
        } catch (const std::exception& error) {
            debug(std::string("usbmux receiver client error: ") + error.what());
        }
    }
}

void UsbmuxReceiver::clientLoop(Socket client) {
    // Single message loop: each iteration reads exactly one 16-byte header.
    // The first header's `version` decides the flavor — 1 (or MESSAGE_PLIST)
    // means XML plist payload; 0 means the legacy binary structs. Both loops
    // are equivalent; only the payload encoding differs.
    bool flavorKnown = false;
    bool plist = false;
    for (;;) {
        std::array<char, 16> header{};
        if (!client.readExact(header)) return;

        std::uint32_t length = 0, version = 0, message = 0, tag = 0;
        std::memcpy(&length, header.data() + 0, 4);
        std::memcpy(&version, header.data() + 4, 4);
        std::memcpy(&message, header.data() + 8, 4);
        std::memcpy(&tag, header.data() + 12, 4);
        if (length < 16 || length > 65536) return;
        std::string payload(length - 16, '\0');
        if (!payload.empty() && !client.readExact(payload)) return;

        if (!flavorKnown) {
            plist = version >= 1 || static_cast<MsgType>(message) == MsgType::Plist;
            flavorKnown = true;
        }

        if (plist) {
            debug("usbmux receiver: plist message " + payload.substr(0, 120));
            const auto type = plistValue(payload, "MessageType");
            if (type == "Listen") {
                if (!client.writeAll(wireFramePlist(makeResultPlist(kResultOk)))) return;
                if (!client.writeAll(
                        wireFramePlist(makeAttachedPlist(kVirtualDeviceId, serial_)))) {
                    return;
                }
                continue;
            }
            if (type == "Connect") {
                const auto deviceId = plistInt(payload, "DeviceID", -1);
                if (deviceId != static_cast<std::int64_t>(kVirtualDeviceId)) {
                    client.writeAll(wireFramePlist(makeResultPlist(kResultBadDev)));
                    return;
                }
                if (!client.writeAll(wireFramePlist(makeResultPlist(kResultOk)))) return;
                debug("usbmux receiver: plist Connect accepted");
                if (onConnection_) onConnection_(std::move(client));
                return;
            }
            client.writeAll(wireFramePlist(makeResultPlist(kResultBadCommand)));
            return;
        }

        switch (static_cast<MsgType>(message)) {
        case MsgType::Listen:
            if (!client.writeAll(makeResultBinary(tag, kResultOk))) return;
            if (!client.writeAll(makeDeviceRecordBinary(tag, kVirtualDeviceId))) return;
            break;
        case MsgType::Connect: {
            std::uint32_t deviceId = 0;
            std::uint16_t port = 0;
            if (payload.size() < 8) return;
            std::memcpy(&deviceId, payload.data(), 4);
            std::memcpy(&port, payload.data() + 4, 2);
            if (deviceId != kVirtualDeviceId) {
                client.writeAll(makeResultBinary(tag, kResultBadDev));
                return;
            }
            if (!client.writeAll(makeResultBinary(tag, kResultOk))) return;
            debug("usbmux receiver: binary Connect accepted on port "
                  + std::to_string(port));
            if (onConnection_) onConnection_(std::move(client));
            return;
        }
        default:
            client.writeAll(makeResultBinary(tag, kResultBadCommand));
            return;
        }
    }
}

}  // namespace od