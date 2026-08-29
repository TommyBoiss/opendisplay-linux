#include "opendisplay/socket.hpp"
#include "opendisplay/usbmux_receiver.hpp"
#include "opendisplay/wire.hpp"

#include <QJsonDocument>
#include <QJsonObject>

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>

namespace {

// Header layout: u32 length, u32 version, u32 message, u32 tag (little-endian).
std::string header(const std::uint32_t length, const std::uint32_t version,
                   const std::uint32_t message, const std::uint32_t tag) {
    std::string out(16, '\0');
    std::memcpy(out.data() + 0, &length, 4);
    std::memcpy(out.data() + 4, &version, 4);
    std::memcpy(out.data() + 8, &message, 4);
    std::memcpy(out.data() + 12, &tag, 4);
    return out;
}

std::uint32_t readU32(const std::string& bytes, const std::size_t offset) {
    std::uint32_t value = 0;
    std::memcpy(&value, bytes.data() + offset, 4);
    return value;
}

bool readExact(od::Socket& socket, std::string& destination, const std::size_t size) {
    destination.assign(size, '\0');
    return socket.readExact(std::span<char>(destination.data(), size));
}

void runBinaryClient(const std::uint16_t port) {
    od::Socket client = od::connectTcp("127.0.0.1", port);
    assert(client.valid());

    // Send Listen (message 3, version 0, 16-byte header only).
    assert(client.writeAll(header(16, 0, 3, 1)));

    // Expect RESULT(1) with tag echo, then DEVICE_ADD(4).
    std::string bytes;
    assert(readExact(client, bytes, 20));  // result msg = 20 bytes
    assert(readU32(bytes, 8) == 1);        // message == RESULT
    assert(readU32(bytes, 12) == 1);       // tag echoed
    assert(readU32(bytes, 16) == 0);       // result == OK

    assert(readExact(client, bytes, 284));  // device record = 284 bytes
    assert(readU32(bytes, 8) == 4);        // message == DEVICE_ADD
    assert(readU32(bytes, 16) == 1);       // device id 1
    assert(readU32(bytes, 0) == 284);     // declared length matches

    // Send Connect (message 2) for device 1, port 9000.
    std::string connect = header(24, 0, 2, 2);
    std::uint32_t deviceId = 1;
    std::uint16_t portNumber = 9000;
    std::uint16_t reserved = 0;
    std::string payload(8, '\0');
    std::memcpy(payload.data() + 0, &deviceId, 4);
    std::memcpy(payload.data() + 4, &portNumber, 2);
    std::memcpy(payload.data() + 6, &reserved, 2);
    connect += payload;
    assert(client.writeAll(connect));

    // Expect RESULT OK, then the socket becomes the OpenDisplay wire stream.
    assert(readExact(client, bytes, 20));
    assert(readU32(bytes, 8) == 1);
    assert(readU32(bytes, 12) == 2);
    assert(readU32(bytes, 16) == 0);

    // The receiver should have sent its wire hello over the muxed socket.
    std::array<char, 4> wireHeader{};
    assert(client.readExact(wireHeader));
    const auto helloLength = od::wire::decodeLength(wireHeader);
    assert(helloLength);
    std::string hello(static_cast<std::size_t>(*helloLength), '\0');
    assert(client.readExact(hello));
    const auto helloJson = od::wire::parseJson(hello);
    assert(helloJson);
    const auto parsed = od::wire::parseHello(*helloJson);
    assert(parsed);
    assert(parsed->pixelsWide == 1920);
    assert(parsed->pixelsHigh == 1080);

    // Send welcome and one video frame over the muxed stream.
    assert(client.writeAll(od::wire::frame(od::wire::welcome())));
    od::EncodedFrame encoded{.capturedAtMs = 100, .annexB = std::string("\0\0\0\1x", 5)};
    assert(client.writeAll(od::wire::frame(od::wire::videoPayload(encoded, 120))));
}

std::string plistListen() {
    return "<plist version=\"1.0\"><dict>"
           "<key>MessageType</key><string>Listen</string>"
           "<key>ClientVersionString</key><string>usbmux receiver test</string>"
           "</dict></plist>";
}

std::string plistConnect(const std::int64_t deviceId) {
    return "<plist version=\"1.0\"><dict>"
           "<key>MessageType</key><string>Connect</string>"
           "<key>DeviceID</key><integer>" + std::to_string(deviceId) + "</integer>"
           "<key>PortNumber</key><integer>9000</integer>"
           "<key>ProtoVersion</key><integer>1</integer>"
           "</dict></plist>";
}

void runPlistClient(const std::uint16_t port) {
    od::Socket client = od::connectTcp("127.0.0.1", port);
    assert(client.valid());

    // Send Listen as a MESSAGE_PLIST(8) frame with version 1.
    {
        const std::string payload = plistListen();
        std::string frame = header(static_cast<std::uint32_t>(payload.size() + 16), 1, 8, 7);
        frame += payload;
        assert(client.writeAll(frame));
    }

    // Expect a plist Result OK, then an Attached record advertising the
    // virtual USB device.
    std::string bytes;
    for (int reply = 0; reply < 2; ++reply) {
        assert(readExact(client, bytes, 16));
        const auto length = readU32(bytes, 0);
        assert(length > 16 && length < 65536);
        std::string payload;
        assert(readExact(client, payload, length - 16));
        if (reply == 0) {
            assert(payload.find("<string>Result</string>") != std::string::npos);
            assert(payload.find("<integer>0</integer>") != std::string::npos);
        } else {
            assert(payload.find("<string>Attached</string>") != std::string::npos);
            assert(payload.find("<string>USB</string>") != std::string::npos);
        }
    }

    // Send Connect; expect Result OK, then the OpenDisplay wire stream.
    {
        const std::string payload = plistConnect(1);
        std::string frame = header(static_cast<std::uint32_t>(payload.size() + 16), 1, 8, 8);
        frame += payload;
        assert(client.writeAll(frame));
    }
    assert(readExact(client, bytes, 16));
    const auto resultLength = readU32(bytes, 0);
    assert(resultLength > 16 && resultLength < 65536);
    std::string resultPayload;
    assert(readExact(client, resultPayload, resultLength - 16));
    assert(resultPayload.find("<integer>0</integer>") != std::string::npos);

    // The receiver's wire hello arrives over the muxed socket.
    std::array<char, 4> wireHeader{};
    assert(client.readExact(wireHeader));
    const auto helloLength = od::wire::decodeLength(wireHeader);
    assert(helloLength);
    std::string hello(static_cast<std::size_t>(*helloLength), '\0');
    assert(client.readExact(hello));
    const auto helloJson = od::wire::parseJson(hello);
    assert(helloJson);
    const auto parsed = od::wire::parseHello(*helloJson);
    assert(parsed);
    assert(parsed->device == "Linux");

    // Complete the handshake so the receiver callback isn't left waiting.
    assert(client.writeAll(od::wire::frame(od::wire::welcome())));
    od::EncodedFrame encoded{.capturedAtMs = 100, .annexB = std::string("\0\0\0\1x", 5)};
    assert(client.writeAll(od::wire::frame(od::wire::videoPayload(encoded, 120))));
}

}  // namespace

int main() {
    bool gotConnection = false;
    bool gotFrame = false;
    od::UsbmuxReceiver receiver;
    receiver.start(0, [&](od::Socket peer) {
        gotConnection = true;
        // Speak the OpenDisplay handshake over the adopted socket, mirroring
        // what ReceiverSession::adoptSocket does in production.
        od::PhoneInfo panel{.pixelsWide = 1920, .pixelsHigh = 1080, .scale = 1.0,
                            .device = "Linux", .installId = "test", .protocolVersion = 2};
        peer.writeAll(od::wire::frame(od::wire::hello(panel)));
        // Read the sender's welcome, then read one video frame.
        std::array<char, 4> wireHeader{};
        assert(peer.readExact(wireHeader));
        const auto length = od::wire::decodeLength(wireHeader);
        assert(length);
        std::string payload(static_cast<std::size_t>(*length), '\0');
        assert(peer.readExact(payload));
        const auto parsed = od::wire::parseJson(payload);
        assert(parsed);
        assert(parsed->value("type").toString() == "welcome");
        // Video frame arrives next.
        assert(peer.readExact(wireHeader));
        const auto videoLength = od::wire::decodeLength(wireHeader);
        assert(videoLength);
        std::string video(static_cast<std::size_t>(*videoLength), '\0');
        assert(peer.readExact(video));
        gotFrame = video.find("\0\0\0\1", 0, 4) != std::string::npos;
    });
    const auto port = receiver.boundPort();
    assert(port != 0);

    runBinaryClient(port);
    runPlistClient(port);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!gotFrame && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    receiver.stop();
    assert(gotConnection);
    assert(gotFrame);
    return 0;
}