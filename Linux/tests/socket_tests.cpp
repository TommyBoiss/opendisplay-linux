#include "opendisplay/socket.hpp"

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cassert>
#include <chrono>
#include <string>
#include <thread>

namespace {

void makeNonblocking(const int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    assert(flags >= 0);
    assert(::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0);
}

void readsDelayedDataFromNonblockingSocket() {
    int pair[2]{};
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    makeNonblocking(pair[0]);
    od::Socket reader(pair[0]);
    std::thread writer([fd = pair[1]] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        assert(::send(fd, "hello", 5, 0) == 5);
        ::close(fd);
    });

    std::array<char, 5> bytes{};
    assert(reader.readExact(bytes));
    assert(std::string(bytes.data(), bytes.size()) == "hello");
    writer.join();
}

void writesThroughNonblockingBackpressure() {
    int pair[2]{};
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    makeNonblocking(pair[0]);
    const int sendBuffer = 4096;
    assert(::setsockopt(pair[0], SOL_SOCKET, SO_SNDBUF, &sendBuffer,
                        sizeof(sendBuffer)) == 0);
    od::Socket writer(pair[0]);
    const std::string payload(1024 * 1024, 'x');
    std::thread reader([fd = pair[1], expected = payload.size()] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        std::array<char, 16 * 1024> buffer{};
        std::size_t received = 0;
        while (received < expected) {
            const auto count = ::recv(fd, buffer.data(), buffer.size(), 0);
            assert(count > 0);
            received += static_cast<std::size_t>(count);
        }
        ::close(fd);
    });

    assert(writer.writeAll(payload));
    reader.join();
}

}  // namespace

int main() {
    readsDelayedDataFromNonblockingSocket();
    writesThroughNonblockingBackpressure();
}
