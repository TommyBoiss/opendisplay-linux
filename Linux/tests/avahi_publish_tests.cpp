// Verifies ServiceAdvertiser keeps the _opensidecar._tcp record registered
// over time (the one-shot publisher used to tear it down immediately).
#include "opendisplay/avahi_publish.hpp"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <thread>

int main() {
    od::ServiceAdvertiser advertiser;
    const std::string error = advertiser.start("OpenDisplay Test", 9000, "test-install-id");
    assert(error.empty());
    assert(advertiser.active());

    // Query Avahi: the service must be resolvable now.
    const auto query = std::system(
        "avahi-browse -rt _opensidecar._tcp 2>/dev/null | grep -q 'OpenDisplay Test'");

    // And still resolvable 3 seconds later — the record must persist.
    std::this_thread::sleep_for(std::chrono::seconds(3));
    const auto queryLater = std::system(
        "avahi-browse -rt _opensidecar._tcp 2>/dev/null | grep -q 'OpenDisplay Test'");
    advertiser.stop();
    assert(!advertiser.active());
    assert(query == 0);
    assert(queryLater == 0);
    return 0;
}