#pragma once

#include "opendisplay/types.hpp"

#include <chrono>
#include <string>
#include <vector>

namespace od {

std::vector<Endpoint> discoverWifi(std::chrono::milliseconds timeout);
std::vector<Endpoint> discoverUsb();
Endpoint chooseEndpoint(const Options& options);

}  // namespace od

