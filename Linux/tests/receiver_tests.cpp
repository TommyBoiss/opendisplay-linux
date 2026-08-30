#include "opendisplay/receiver_session.hpp"
#include "opendisplay/socket.hpp"
#include "opendisplay/wire.hpp"

#include <QJsonDocument>
#include <QJsonObject>

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

namespace {

// A minimal fake sender: connects, reads the receiver's hello, sends welcome,
// then streams a framed video payload.
void runFakeSender(const std::uint16_t port, std::string& receivedHello,
                   std::string& receivedPong) {
    od::Socket client = od::connectTcp("127.0.0.1", port);
    assert(client.valid());

    // Read the receiver's hello (4-byte length + JSON).
    std::array<char, 4> header{};
    assert(client.readExact(header));
    const auto helloLen = od::wire::decodeLength(header);
    assert(helloLen);
    std::string hello(static_cast<std::size_t>(*helloLen), '\0');
    assert(client.readExact(hello));
    receivedHello = hello;

    // Send welcome.
    assert(client.writeAll(od::wire::frame(od::wire::welcome())));

    // Send a ping; expect a pong back.
    assert(client.writeAll(od::wire::frame("{\"type\":\"ping\",\"t\":123}")));
    std::array<char, 4> pongHeader{};
    assert(client.readExact(pongHeader));
    const auto pongLen = od::wire::decodeLength(pongHeader);
    assert(pongLen);
    std::string pong(static_cast<std::size_t>(*pongLen), '\0');
    assert(client.readExact(pong));
    receivedPong = pong;

    // Stream one video frame: telemetry prefix + Annex B.
    od::EncodedFrame encoded{.capturedAtMs = 100, .annexB = std::string("\0\0\0\1x", 5)};
    assert(client.writeAll(od::wire::frame(od::wire::videoPayload(encoded, 120))));

    // Stream a LARGE video frame (> 1 MiB) — a 2560x1440 keyframe with SPS/PPS
    // exceeds the control-message cap, and the receiver must not treat it as
    // an invalid length and drop the connection.
    std::string bigAnnexB("\0\0\0\1", 4);
    bigAnnexB.append(2 * 1024 * 1024, 'x');  // ~2 MiB of NAL data
    od::EncodedFrame big{.capturedAtMs = 200, .annexB = bigAnnexB};
    assert(client.writeAll(od::wire::frame(od::wire::videoPayload(big, 220))));

    // Keep the connection open briefly so the receiver can process the frames.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

}  // namespace

int main() {
    od::PhoneInfo panel{.pixelsWide = 1920, .pixelsHigh = 1080, .scale = 1.0,
                        .device = "Linux", .installId = "test-id", .protocolVersion = 2};
    od::ReceiverSession receiver;
    int frames = 0;
    bool gotBigFrame = false;
    std::string receivedHello;
    std::string receivedPong;
    receiver.start(0, panel,
                   [&](const od::ReceivedFrame& frame) {
                       ++frames;
                       if (frame.bgra.size() > 1024 * 1024) {
                           gotBigFrame = true;
                       }
                   },
                   [&](const std::string&) {});

    const auto port = receiver.boundPort();
    assert(port != 0);

    std::thread sender(runFakeSender, port, std::ref(receivedHello), std::ref(receivedPong));

    // Drive the receiver's tick loop until the big frame arrives or timeout.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!gotBigFrame && std::chrono::steady_clock::now() < deadline) {
        receiver.tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    sender.join();

    // Both the small and the >1 MiB frame must have been delivered. The big
    // frame proves the receiver no longer treats large video frames as an
    // invalid length (the old code dropped the connection on them).
    assert(frames >= 2);
    assert(gotBigFrame);
    // The hello the sender received must advertise our panel.
    const auto helloJson = od::wire::parseJson(receivedHello);
    assert(helloJson);
    const auto parsed = od::wire::parseHello(*helloJson);
    assert(parsed);
    assert(parsed->pixelsWide == 1920);
    assert(parsed->pixelsHigh == 1080);
    assert(parsed->installId == "test-id");
    // The pong must echo the ping's t value.
    const auto pongJson = od::wire::parseJson(receivedPong);
    assert(pongJson);
    assert(pongJson->value("type").toString() == "pong");
    assert(pongJson->value("t").toDouble() == 123.0);

    receiver.stop();
    return 0;
}
