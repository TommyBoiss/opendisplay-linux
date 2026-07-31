#include "opendisplay/discovery.hpp"

#include "opendisplay/log.hpp"

#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-common/address.h>
#include <avahi-common/error.h>
#include <avahi-common/simple-watch.h>
#include <usbmuxd.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace od {
namespace {

struct AvahiContext {
    AvahiSimplePoll* poll = nullptr;
    AvahiClient* client = nullptr;
    AvahiServiceBrowser* browser = nullptr;
    std::vector<Endpoint> endpoints;
};

void resolveCallback(AvahiServiceResolver* resolver, AvahiIfIndex, AvahiProtocol,
                     AvahiResolverEvent event, const char* name, const char*,
                     const char*, const char*, const AvahiAddress* address,
                     std::uint16_t port, AvahiStringList*, AvahiLookupResultFlags,
                     void* userdata) {
    auto& context = *static_cast<AvahiContext*>(userdata);
    if (event == AVAHI_RESOLVER_FOUND && address != nullptr) {
        char host[AVAHI_ADDRESS_STR_MAX]{};
        avahi_address_snprint(host, sizeof(host), address);
        const auto duplicate = std::ranges::any_of(context.endpoints, [&](const Endpoint& item) {
            return item.name == name && item.host == host && item.port == port;
        });
        if (!duplicate) {
            context.endpoints.push_back(Endpoint{
                .kind = TransportKind::Wifi,
                .name = name != nullptr ? name : "OpenDisplay",
                .host = host,
                .port = port,
                .udid = {},
                .usbHandle = -1,
            });
        }
    }
    avahi_service_resolver_free(resolver);
}

void browseCallback(AvahiServiceBrowser*, AvahiIfIndex interface, AvahiProtocol protocol,
                    AvahiBrowserEvent event, const char* name, const char* type,
                    const char* domain, AvahiLookupResultFlags, void* userdata) {
    auto& context = *static_cast<AvahiContext*>(userdata);
    if (event == AVAHI_BROWSER_NEW) {
        avahi_service_resolver_new(context.client, interface, protocol, name, type, domain,
                                   AVAHI_PROTO_UNSPEC, AVAHI_LOOKUP_USE_MULTICAST,
                                   resolveCallback, &context);
    }
}

void clientCallback(AvahiClient* client, const AvahiClientState state, void* userdata) {
    auto& context = *static_cast<AvahiContext*>(userdata);
    if (state == AVAHI_CLIENT_FAILURE) {
        debug(std::string("Avahi failure: ") + avahi_strerror(avahi_client_errno(client)));
        avahi_simple_poll_quit(context.poll);
    }
}

}  // namespace

std::vector<Endpoint> discoverWifi(const std::chrono::milliseconds timeout) {
    AvahiContext context;
    context.poll = avahi_simple_poll_new();
    if (context.poll == nullptr) {
        throw std::runtime_error("cannot create Avahi poll");
    }
    int error = 0;
    context.client = avahi_client_new(avahi_simple_poll_get(context.poll), AVAHI_CLIENT_NO_FAIL,
                                      clientCallback, &context, &error);
    if (context.client == nullptr) {
        avahi_simple_poll_free(context.poll);
        throw std::runtime_error(std::string("cannot connect to Avahi: ") + avahi_strerror(error));
    }
    context.browser = avahi_service_browser_new(
        context.client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, "_opensidecar._tcp", nullptr,
        AVAHI_LOOKUP_USE_MULTICAST, browseCallback, &context);
    if (context.browser == nullptr) {
        avahi_client_free(context.client);
        avahi_simple_poll_free(context.poll);
        throw std::runtime_error("cannot browse for _opensidecar._tcp");
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        avahi_simple_poll_iterate(context.poll, 100);
    }
    avahi_service_browser_free(context.browser);
    avahi_client_free(context.client);
    avahi_simple_poll_free(context.poll);
    return context.endpoints;
}

std::vector<Endpoint> discoverUsb() {
    usbmuxd_device_info_t* devices = nullptr;
    const int count = usbmuxd_get_device_list(&devices);
    if (count < 0) {
        throw std::runtime_error("cannot query usbmuxd; is usbmuxd.service running?");
    }
    std::vector<Endpoint> endpoints;
    for (int index = 0; index < count; ++index) {
        const auto& device = devices[index];
        if (device.conn_type != CONNECTION_TYPE_USB) {
            continue;
        }
        endpoints.push_back(Endpoint{
            .kind = TransportKind::Usb,
            .name = "iPhone / iPad",
            .host = {},
            .port = 9000,
            .udid = device.udid,
            .usbHandle = static_cast<int>(device.handle),
        });
    }
    usbmuxd_device_list_free(&devices);
    return endpoints;
}

Endpoint chooseEndpoint(const Options& options) {
    if (!options.host.empty()) {
        return Endpoint{.kind = TransportKind::Wifi, .name = options.host,
                        .host = options.host, .port = options.port, .udid = {},
                        .usbHandle = -1};
    }

    if (options.transport != TransportKind::Wifi) {
        const auto devices = discoverUsb();
        const auto found = std::ranges::find_if(devices, [&](const Endpoint& endpoint) {
            return options.udid.empty() || endpoint.udid == options.udid;
        });
        if (found != devices.end()) {
            auto endpoint = *found;
            endpoint.port = options.port;
            return endpoint;
        }
        if (options.transport == TransportKind::Usb) {
            throw std::runtime_error("no matching USB iOS device found");
        }
    }

    log("Searching for OpenDisplay receivers on the local network…");
    const auto services = discoverWifi(std::chrono::seconds(3));
    const auto found = std::ranges::find_if(services, [&](const Endpoint& endpoint) {
        return options.serviceName.empty() || endpoint.name == options.serviceName;
    });
    if (found == services.end()) {
        throw std::runtime_error("no matching _opensidecar._tcp receiver found");
    }
    return *found;
}

}  // namespace od
