#include "opendisplay/avahi_publish.hpp"

#include "opendisplay/log.hpp"

#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/error.h>
#include <avahi-common/simple-watch.h>

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace od {
namespace {

std::string avahiFailure(const std::string_view operation, const int error) {
    std::string message(operation);
    message += ": ";
    message += avahi_strerror(error);
    if (error == AVAHI_ERR_NO_DAEMON || error == AVAHI_ERR_DISCONNECTED
        || error == AVAHI_ERR_BAD_STATE) {
        message += ". Start Avahi with `sudo systemctl enable --now avahi-daemon.service`";
    } else if (error == AVAHI_ERR_ACCESS_DENIED) {
        message += ". Check the system D-Bus and Avahi access policy";
    }
    return message;
}

}  // namespace

ServiceAdvertiser::~ServiceAdvertiser() { stop(); }

std::string ServiceAdvertiser::start(const std::string& serviceName,
                                     const std::uint16_t port,
                                     const std::string& installId) {
    stop();
    serviceName_ = serviceName.empty() ? "OpenDisplay" : serviceName;
    installId_ = installId;
    port_ = port;
    established_ = false;

    auto* poll = avahi_simple_poll_new();
    if (poll == nullptr) {
        return "cannot create Avahi poll";
    }
    poll_ = poll;

    int error = 0;
    auto* client = avahi_client_new(avahi_simple_poll_get(poll), AVAHI_CLIENT_NO_FAIL,
                                    &ServiceAdvertiser::clientState, this, &error);
    if (client == nullptr) {
        avahi_simple_poll_free(poll);
        poll_ = nullptr;
        return avahiFailure("Cannot connect to Avahi", error);
    }
    client_ = client;

    // Pump the Avahi event loop on a background thread so the service stays
    // registered for this object's lifetime. Stopping calls avahi_simple_poll_quit.
    loop_ = std::thread([poll] {
        avahi_simple_poll_loop(poll);
    });

    // Wait briefly for the entry group to be established so we can report a
    // meaningful error at connect time (the loop keeps running afterwards).
    {
        std::unique_lock lock(mutex_);
        establishedCondition_.wait_for(lock, std::chrono::seconds(5), [&] {
            return established_ || !failure_.empty();
        });
        if (!established_ && !failure_.empty()) {
            const std::string result = failure_;
            stop();
            return result;
        }
    }
    if (!established_) {
        // Avahi never reached S_RUNNING within the timeout.
        stop();
        return "Avahi daemon did not become ready. Start it with "
               "`sudo systemctl enable --now avahi-daemon.service`";
    }
    return {};
}

void ServiceAdvertiser::stop() {
    if (poll_ != nullptr) {
        avahi_simple_poll_quit(static_cast<AvahiSimplePoll*>(poll_));
    }
    if (loop_.joinable()) {
        loop_.join();
    }
    if (group_ != nullptr) {
        avahi_entry_group_free(static_cast<AvahiEntryGroup*>(group_));
        group_ = nullptr;
    }
    if (client_ != nullptr) {
        avahi_client_free(static_cast<AvahiClient*>(client_));
        client_ = nullptr;
    }
    if (poll_ != nullptr) {
        avahi_simple_poll_free(static_cast<AvahiSimplePoll*>(poll_));
        poll_ = nullptr;
    }
    established_ = false;
    failure_.clear();
}

void ServiceAdvertiser::clientState(AvahiClient* client, const AvahiClientState state,
                                    void* userdata) {
    auto& self = *static_cast<ServiceAdvertiser*>(userdata);
    switch (state) {
    case AVAHI_CLIENT_S_RUNNING:
        self.registerService(client);
        break;
    case AVAHI_CLIENT_FAILURE:
        {
            std::lock_guard lock(self.mutex_);
            self.failure_ = avahiFailure("Avahi client failed", avahi_client_errno(client));
        }
        self.establishedCondition_.notify_all();
        avahi_simple_poll_quit(static_cast<AvahiSimplePoll*>(self.poll_));
        break;
    default:
        break;
    }
}

void ServiceAdvertiser::entryGroupState(AvahiEntryGroup* group,
                                        const AvahiEntryGroupState state, void* userdata) {
    auto& self = *static_cast<ServiceAdvertiser*>(userdata);
    switch (state) {
    case AVAHI_ENTRY_GROUP_ESTABLISHED:
        {
            std::lock_guard lock(self.mutex_);
            self.established_ = true;
        }
        self.establishedCondition_.notify_all();
        debug("Avahi service established: " + self.serviceName_ + " on port "
              + std::to_string(self.port_));
        break;
    case AVAHI_ENTRY_GROUP_COLLISION:
        {
            std::lock_guard lock(self.mutex_);
            self.failure_ = "Avahi service name collision; choose a different device name";
        }
        self.establishedCondition_.notify_all();
        avahi_simple_poll_quit(static_cast<AvahiSimplePoll*>(self.poll_));
        break;
    case AVAHI_ENTRY_GROUP_FAILURE:
        {
            std::lock_guard lock(self.mutex_);
            self.failure_ = avahiFailure(
                "Avahi could not publish the service",
                avahi_client_errno(avahi_entry_group_get_client(group)));
        }
        self.establishedCondition_.notify_all();
        avahi_simple_poll_quit(static_cast<AvahiSimplePoll*>(self.poll_));
        break;
    default:
        break;
    }
}

void ServiceAdvertiser::registerService(AvahiClient* client) {
    if (group_ == nullptr) {
        auto* group = avahi_entry_group_new(client, &ServiceAdvertiser::entryGroupState, this);
        if (group == nullptr) {
            std::lock_guard lock(mutex_);
            failure_ = avahiFailure("Avahi could not create an entry group",
                                    avahi_client_errno(client));
            establishedCondition_.notify_all();
            avahi_simple_poll_quit(static_cast<AvahiSimplePoll*>(poll_));
            return;
        }
        group_ = group;
    }
    if (avahi_entry_group_is_empty(static_cast<AvahiEntryGroup*>(group_))) {
        const int result = avahi_entry_group_add_service(
            static_cast<AvahiEntryGroup*>(group_), AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
            static_cast<AvahiPublishFlags>(0), serviceName_.c_str(), "_opensidecar._tcp",
            nullptr, nullptr, port_,
            // TXT records: senders match receivers by `id` and require `pv`.
            ("id=" + installId_).c_str(), "pv=2", nullptr);
        if (result < 0) {
            std::lock_guard lock(mutex_);
            failure_ = avahiFailure("Avahi could not add the service", result);
            establishedCondition_.notify_all();
            avahi_simple_poll_quit(static_cast<AvahiSimplePoll*>(poll_));
            return;
        }
        const int committed = avahi_entry_group_commit(static_cast<AvahiEntryGroup*>(group_));
        if (committed < 0) {
            std::lock_guard lock(mutex_);
            failure_ = avahiFailure("Avahi could not commit the service", committed);
            establishedCondition_.notify_all();
            avahi_simple_poll_quit(static_cast<AvahiSimplePoll*>(poll_));
        }
    }
}

}  // namespace od
