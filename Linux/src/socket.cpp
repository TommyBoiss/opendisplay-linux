#include "opendisplay/socket.hpp"

#include <usbmuxd.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace od {
namespace {

bool waitForSocket(const int fd, const short events) {
    pollfd descriptor{.fd = fd, .events = events, .revents = 0};
    for (;;) {
        const int result = ::poll(&descriptor, 1, -1);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            return false;
        }
        if ((descriptor.revents & events) != 0) {
            return true;
        }
        return false;
    }
}

}  // namespace

Socket::Socket(const int fd) : fd_(fd) {}

Socket::~Socket() { close(); }

Socket::Socket(Socket&& other) noexcept : fd_(other.release()) {}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.release();
    }
    return *this;
}

int Socket::release() {
    const int result = fd_;
    fd_ = -1;
    return result;
}

void Socket::close() {
    if (fd_ >= 0) {
        ::shutdown(fd_, SHUT_RDWR);
        ::close(fd_);
        fd_ = -1;
    }
}

bool Socket::readExact(const std::span<char> destination) {
    std::size_t offset = 0;
    while (offset < destination.size()) {
        const auto count = ::recv(fd_, destination.data() + offset, destination.size() - offset, 0);
        if (count == 0) {
            return false;
        }
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (waitForSocket(fd_, POLLIN)) {
                    continue;
                }
            }
            return false;
        }
        offset += static_cast<std::size_t>(count);
    }
    return true;
}

bool Socket::writeAll(const std::string_view bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto count = ::send(fd_, bytes.data() + offset, bytes.size() - offset, MSG_NOSIGNAL);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (waitForSocket(fd_, POLLOUT)) {
                    continue;
                }
            }
            return false;
        }
        if (count == 0) {
            return false;
        }
        offset += static_cast<std::size_t>(count);
    }
    return true;
}

Socket connectTcp(const std::string& host, const std::uint16_t port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* addresses = nullptr;
    const auto service = std::to_string(port);
    const int result = getaddrinfo(host.c_str(), service.c_str(), &hints, &addresses);
    if (result != 0) {
        throw std::runtime_error("cannot resolve " + host + ": " + gai_strerror(result));
    }

    Socket connected;
    for (auto* address = addresses; address != nullptr; address = address->ai_next) {
        Socket candidate(::socket(address->ai_family, address->ai_socktype, address->ai_protocol));
        if (!candidate.valid()) {
            continue;
        }
        const int enabled = 1;
        setsockopt(candidate.fd(), IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled));
        if (::connect(candidate.fd(), address->ai_addr, address->ai_addrlen) == 0) {
            connected = std::move(candidate);
            break;
        }
    }
    freeaddrinfo(addresses);
    if (!connected.valid()) {
        throw std::runtime_error("cannot connect to " + host + ":" + service);
    }
    return connected;
}

Socket connectUsb(const int deviceHandle, const std::uint16_t port) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    int result = -ENODEV;
    do {
        result = usbmuxd_connect(static_cast<std::uint32_t>(deviceHandle), port);
        if (result >= 0) {
            return Socket(result);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    } while (std::chrono::steady_clock::now() < deadline);

    std::string detail = "unknown libusbmuxd error";
    if (result < 0 && result >= -4095) {
        detail = std::strerror(-result);
    }
    throw std::runtime_error("usbmuxd could not connect to device port "
                             + std::to_string(port) + ": " + detail + " (error "
                             + std::to_string(result)
                             + "). Keep the receiver app open and verify `idevicepair validate`");
}

Socket listenTcp(const std::uint16_t port) {
    // Dual-stack: bind an IPv6 socket with IPV6_V6ONLY disabled so a single
    // listener accepts both IPv4 and IPv6 peers. Senders resolve receivers
    // over mDNS and may get an IPv6 address (e.g. fdc8::… link-local ULA),
    // which an IPv4-only bind could never accept.
    Socket listener(::socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP));
    if (!listener.valid()) {
        throw std::runtime_error("cannot create listening socket: "
                                 + std::string(std::strerror(errno)));
    }
    const int enabled = 1;
    setsockopt(listener.fd(), SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
    // IPV6_V6ONLY=0 makes the single IPv6 socket accept IPv4 peers too.
    const int ipv6Only = 0;
    setsockopt(listener.fd(), IPPROTO_IPV6, IPV6_V6ONLY, &ipv6Only, sizeof(ipv6Only));
    sockaddr_in6 address{};
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_any;
    address.sin6_port = htons(port);
    if (::bind(listener.fd(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        throw std::runtime_error("cannot bind port " + std::to_string(port) + ": "
                                 + std::string(std::strerror(errno)));
    }
    if (::listen(listener.fd(), 1) != 0) {
        throw std::runtime_error("cannot listen on port " + std::to_string(port) + ": "
                                 + std::string(std::strerror(errno)));
    }
    return listener;
}

Socket acceptConnection(const Socket& listener) {
    sockaddr_storage peer{};
    socklen_t peerLength = sizeof(peer);
    const int fd = ::accept(listener.fd(), reinterpret_cast<sockaddr*>(&peer), &peerLength);
    if (fd < 0) {
        return Socket();
    }
    const int enabled = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled));
    return Socket(fd);
}

}  // namespace od
