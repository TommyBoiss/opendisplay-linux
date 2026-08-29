#pragma once

#include "opendisplay/types.hpp"

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace od {

class Socket {
public:
    explicit Socket(int fd = -1);
    ~Socket();
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    [[nodiscard]] int fd() const { return fd_; }
    [[nodiscard]] bool valid() const { return fd_ >= 0; }
    int release();
    void close();
    bool readExact(std::span<char> destination);
    bool writeAll(std::string_view bytes);

private:
    int fd_ = -1;
};

Socket connectTcp(const std::string& host, std::uint16_t port);
Socket connectUsb(int deviceHandle, std::uint16_t port);
/// Bind and listen on all interfaces (0.0.0.0) at `port`. Returns a listening
/// socket; call accept() on it to obtain a connected peer.
Socket listenTcp(std::uint16_t port);
/// Accept a single pending connection from a listening socket. Returns an
/// invalid socket if interrupted or the listener is closed.
Socket acceptConnection(const Socket& listener);

}  // namespace od
