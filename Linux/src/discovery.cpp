#include "opendisplay/discovery.hpp"

#include "opendisplay/log.hpp"

#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-common/address.h>
#include <avahi-common/error.h>
#include <avahi-common/simple-watch.h>
#include <usbmuxd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace od {
namespace {

struct AvahiContext {
    AvahiSimplePoll* poll = nullptr;
    AvahiClient* client = nullptr;
    AvahiServiceBrowser* browser = nullptr;
    std::vector<Endpoint> endpoints;
    std::string failure;
    bool daemonReady = false;
};

std::string usbmuxdFailure(const std::string_view operation, const int result) {
    std::string message(operation);
    message += " failed: ";
    if (result < 0 && result >= -4095) {
        message += std::strerror(-result);
    } else {
        message += "unknown libusbmuxd error";
    }
    message += " (error " + std::to_string(result) + ", libusbmuxd ";
    const char* version = libusbmuxd_version();
    message += version != nullptr ? version : "unknown";
    message += "). Confirm `/run/usbmuxd` exists and inspect `journalctl -u usbmuxd.service`";
    return message;
}

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
    message += "; use `--host <device-ip>` to bypass discovery";
    return message;
}

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
    } else if (event == AVAHI_RESOLVER_FAILURE) {
        auto* client = avahi_service_resolver_get_client(resolver);
        debug(avahiFailure("Avahi could not resolve a receiver", avahi_client_errno(client)));
    }
    avahi_service_resolver_free(resolver);
}

void browseCallback(AvahiServiceBrowser* browser, AvahiIfIndex interface, AvahiProtocol protocol,
                    AvahiBrowserEvent event, const char* name, const char* type,
                    const char* domain, AvahiLookupResultFlags, void* userdata) {
    auto& context = *static_cast<AvahiContext*>(userdata);
    auto* client = avahi_service_browser_get_client(browser);
    if (event == AVAHI_BROWSER_NEW) {
        if (avahi_service_resolver_new(client, interface, protocol, name, type, domain,
                                       AVAHI_PROTO_UNSPEC, AVAHI_LOOKUP_USE_MULTICAST,
                                       resolveCallback, &context) == nullptr) {
            debug(avahiFailure("Avahi could not create a resolver",
                               avahi_client_errno(client)));
        }
    } else if (event == AVAHI_BROWSER_FAILURE) {
        context.failure = avahiFailure("Avahi browsing failed", avahi_client_errno(client));
        avahi_simple_poll_quit(context.poll);
    }
}

void clientCallback(AvahiClient* client, const AvahiClientState state, void* userdata) {
    auto& context = *static_cast<AvahiContext*>(userdata);
    if (state == AVAHI_CLIENT_S_RUNNING) {
        context.daemonReady = true;
        if (context.browser == nullptr) {
            // avahi_client_new() invokes this callback before it returns, so
            // use the callback argument rather than context.client here.
            context.browser = avahi_service_browser_new(
                client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, "_opensidecar._tcp", nullptr,
                AVAHI_LOOKUP_USE_MULTICAST, browseCallback, &context);
            if (context.browser == nullptr) {
                context.failure = avahiFailure("Avahi could not browse _opensidecar._tcp",
                                               avahi_client_errno(client));
                avahi_simple_poll_quit(context.poll);
            }
        }
    } else if (state == AVAHI_CLIENT_FAILURE) {
        context.failure = avahiFailure("Avahi client failed", avahi_client_errno(client));
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
        throw std::runtime_error(avahiFailure("Cannot connect to Avahi", error));
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (context.failure.empty() && std::chrono::steady_clock::now() < deadline) {
        if (avahi_simple_poll_iterate(context.poll, 100) < 0) {
            context.failure = "Avahi event loop failed; use `--host <device-ip>` to bypass discovery";
        }
    }
    if (!context.daemonReady && context.failure.empty()) {
        context.failure =
            "Avahi daemon did not become ready. Start it with "
            "`sudo systemctl enable --now avahi-daemon.service`; use "
            "`--host <device-ip>` to bypass discovery";
    }
    if (context.browser != nullptr) {
        avahi_service_browser_free(context.browser);
    }
    avahi_client_free(context.client);
    avahi_simple_poll_free(context.poll);
    if (!context.failure.empty()) {
        throw std::runtime_error(context.failure);
    }
    return context.endpoints;
}

std::vector<Endpoint> discoverUsb(const std::chrono::milliseconds timeout) {
    // One native diagnostic is useful in verbose mode; leaving the library's
    // global debug level enabled would print the same line on every retry.
    libusbmuxd_set_debug_level(verboseLogging ? 1 : 0);
    bool firstQuery = true;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    int result = 0;
    do {
        usbmuxd_device_info_t* devices = nullptr;
        result = usbmuxd_get_device_list(&devices);
        if (firstQuery) {
            libusbmuxd_set_debug_level(0);
            firstQuery = false;
        }
        std::vector<Endpoint> endpoints;
        if (result >= 0) {
            for (int index = 0; index < result; ++index) {
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
        }
        if (devices != nullptr) {
            usbmuxd_device_list_free(&devices);
        }
        if (!endpoints.empty() || std::chrono::steady_clock::now() >= deadline) {
            if (result < 0) {
                throw std::runtime_error(usbmuxdFailure("usbmuxd device query", result));
            }
            return endpoints;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    } while (true);
}

Endpoint chooseEndpoint(const Options& options) {
    if (!options.host.empty()) {
        return Endpoint{.kind = TransportKind::Wifi, .name = options.host,
                        .host = options.host, .port = options.port, .udid = {},
                        .usbHandle = -1};
    }

    if (options.transport != TransportKind::Wifi) {
        std::vector<Endpoint> devices;
        try {
            devices = discoverUsb(options.transport == TransportKind::Usb
                                      ? std::chrono::seconds(5)
                                      : std::chrono::milliseconds(500));
        } catch (const std::exception& error) {
            if (options.transport == TransportKind::Usb) {
                throw;
            }
            debug(std::string("USB discovery unavailable: ") + error.what());
        }
        const auto found = std::ranges::find_if(devices, [&](const Endpoint& endpoint) {
            return options.udid.empty() || endpoint.udid == options.udid;
        });
        if (found != devices.end()) {
            auto endpoint = *found;
            endpoint.port = options.port;
            return endpoint;
        }
        if (options.transport == TransportKind::Usb) {
            throw std::runtime_error(
                "no USB iOS device became ready in usbmuxd. Unlock and reconnect the device, "
                "accept the Trust prompt, then verify `idevice_id -l` and `idevicepair pair`");
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
