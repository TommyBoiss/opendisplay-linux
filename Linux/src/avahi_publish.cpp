#include "opendisplay/avahi_publish.hpp"

#include "opendisplay/log.hpp"

#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/error.h>
#include <avahi-common/simple-watch.h>

#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace od {
namespace {

struct PublishContext {
    AvahiSimplePoll* poll = nullptr;
    AvahiClient* client = nullptr;
    AvahiEntryGroup* group = nullptr;
    std::string serviceName;
    std::string installId;
    std::uint16_t port = 9000;
    std::string failure;
    bool committed = false;
};

void entryGroupCallback(AvahiEntryGroup* group, AvahiEntryGroupState state, void* userdata) {
    auto& context = *static_cast<PublishContext*>(userdata);
    if (state == AVAHI_ENTRY_GROUP_ESTABLISHED) {
        context.committed = true;
    } else if (state == AVAHI_ENTRY_GROUP_COLLISION) {
        context.failure = "Avahi service name collision; choose a different device name";
        avahi_simple_poll_quit(context.poll);
    } else if (state == AVAHI_ENTRY_GROUP_FAILURE) {
        context.failure = "Avahi could not publish the service: "
            + std::string(avahi_strerror(avahi_client_errno(
                  avahi_entry_group_get_client(group))));
        avahi_simple_poll_quit(context.poll);
    }
}

void clientCallback(AvahiClient* client, const AvahiClientState state, void* userdata) {
    auto& context = *static_cast<PublishContext*>(userdata);
    if (state == AVAHI_CLIENT_S_RUNNING) {
        if (context.group == nullptr) {
            context.group = avahi_entry_group_new(client, entryGroupCallback, &context);
            if (context.group == nullptr) {
                context.failure = "Avahi could not create an entry group: "
                    + std::string(avahi_strerror(avahi_client_errno(client)));
                avahi_simple_poll_quit(context.poll);
                return;
            }
        }
        if (avahi_entry_group_is_empty(context.group)) {
            const int result = avahi_entry_group_add_service(
                context.group, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, static_cast<AvahiPublishFlags>(0),
                context.serviceName.c_str(), "_opensidecar._tcp", nullptr, nullptr, context.port,
                "id=" + context.installId, "pv=2", nullptr);
            if (result < 0) {
                context.failure = "Avahi could not add the service: "
                    + std::string(avahi_strerror(result));
                avahi_simple_poll_quit(context.poll);
                return;
            }
            avahi_entry_group_commit(context.group);
        }
    } else if (state == AVAHI_CLIENT_FAILURE) {
        context.failure = "Avahi client failed: "
            + std::string(avahi_strerror(avahi_client_errno(client)));
        avahi_simple_poll_quit(context.poll);
    }
}

}  // namespace

std::string publishReceiver(const std::string& serviceName, const std::uint16_t port,
                            const std::string& installId) {
    PublishContext context;
    context.serviceName = serviceName.empty() ? "OpenDisplay" : serviceName;
    context.installId = installId;
    context.port = port;
    context.poll = avahi_simple_poll_new();
    if (context.poll == nullptr) {
        return "cannot create Avahi poll";
    }
    int error = 0;
    context.client = avahi_client_new(avahi_simple_poll_get(context.poll), AVAHI_CLIENT_NO_FAIL,
                                      clientCallback, &context, &error);
    if (context.client == nullptr) {
        avahi_simple_poll_free(context.poll);
        return "cannot connect to Avahi: " + std::string(avahi_strerror(error));
    }

    // Run the poll loop until the service is committed or a failure occurs.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (context.failure.empty() && !context.committed
           && std::chrono::steady_clock::now() < deadline) {
        avahi_simple_poll_iterate(context.poll, 100);
    }
    if (context.group != nullptr) {
        avahi_entry_group_free(context.group);
    }
    avahi_client_free(context.client);
    avahi_simple_poll_free(context.poll);
    return context.failure;
}

}  // namespace od
