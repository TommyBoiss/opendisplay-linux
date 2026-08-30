// Verifies ServiceAdvertiser keeps the _opensidecar._tcp record registered
// over time (the one-shot publisher used to tear it down immediately).
//
// Requires a running avahi-daemon. In containers without one (e.g. CI), the
// test exits successfully instead of failing: publishing legitimately can't
// work without a daemon, so there is nothing to verify there.
#include "opendisplay/avahi_publish.hpp"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

namespace {

bool daemonAvailable() {
    // avahi-browse must exist and the daemon must be running. systemctl is
    // used for the daemon check (works on the host and in containers).
    return std::system("avahi-browse --version >/dev/null 2>&1") == 0
        && std::system(
               "systemctl is-active --quiet avahi-daemon.service >/dev/null 2>&1") == 0;
}

bool serviceVisible() {
    return std::system(
        "avahi-browse -rt _opensidecar._tcp 2>/dev/null | grep -q 'OpenDisplay Test'") == 0;
}

}  // namespace

int main() {
    if (!daemonAvailable()) {
        // No avahi-daemon (typical CI container): publishing can't work, so
        // just confirm start() reports a problem cleanly instead of hanging
        // or crashing, then stop and skip the persistence assertions.
        od::ServiceAdvertiser advertiser;
        const std::string error = advertiser.start("OpenDisplay Test", 9000, "test-install-id");
        assert(!error.empty());
        advertiser.stop();
        assert(!advertiser.active());
        return 0;
    }

    od::ServiceAdvertiser advertiser;
    const std::string error = advertiser.start("OpenDisplay Test", 9000, "test-install-id");
    assert(error.empty());
    assert(advertiser.active());
    assert(serviceVisible());

    // The record must still be resolvable 3 seconds later — the one-shot
    // publisher used to tear it down immediately.
    std::this_thread::sleep_for(std::chrono::seconds(3));
    const bool stillVisible = serviceVisible();
    advertiser.stop();
    assert(!advertiser.active());
    assert(stillVisible);
    return 0;
}