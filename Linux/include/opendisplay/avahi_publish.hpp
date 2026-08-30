#pragma once

#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/simple-watch.h>

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace od {

/// A persistent `_opensidecar._tcp` advertiser. The service stays registered
/// for the object's lifetime (unlike a one-shot publish, which tears down the
/// Avahi client and makes the record vanish immediately). Senders browse for
/// this service and match receivers by the `id` TXT record.
class ServiceAdvertiser {
public:
    ServiceAdvertiser() = default;
    ~ServiceAdvertiser();
    ServiceAdvertiser(const ServiceAdvertiser&) = delete;
    ServiceAdvertiser& operator=(const ServiceAdvertiser&) = delete;

    /// Register `serviceName` on `port`. Returns an empty string on success,
    /// a human-readable error otherwise.
    std::string start(const std::string& serviceName, std::uint16_t port,
                      const std::string& installId);
    /// Withdraw the service and disconnect from Avahi.
    void stop();
    [[nodiscard]] bool active() const { return poll_ != nullptr; }

private:
    static void clientState(AvahiClient* client, AvahiClientState state, void* userdata);
    static void entryGroupState(AvahiEntryGroup* group, AvahiEntryGroupState state,
                                void* userdata);
    void registerService(AvahiClient* client);

    AvahiSimplePoll* poll_ = nullptr;
    AvahiClient* client_ = nullptr;
    AvahiEntryGroup* group_ = nullptr;
    std::thread loop_;
    std::mutex mutex_;
    std::condition_variable establishedCondition_;
    std::string failure_;
    std::string serviceName_;
    std::string installId_;
    std::uint16_t port_ = 9000;
    bool established_ = false;
};

}  // namespace od
