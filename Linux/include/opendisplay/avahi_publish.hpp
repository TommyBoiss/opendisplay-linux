#pragma once

#include <cstdint>
#include <string>

namespace od {

/// Publishes the receiver as an `_opensidecar._tcp` service so senders can
/// discover it over mDNS. Returns a human-readable error on failure, or an
/// empty string on success.
std::string publishReceiver(const std::string& serviceName, std::uint16_t port,
                            const std::string& installId);

}  // namespace od
