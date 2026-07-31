#pragma once

#include "opendisplay/types.hpp"

#include <chrono>
#include <string>
#include <vector>

namespace od {

std::vector<Endpoint> discoverWifi(std::chrono::milliseconds timeout);
std::vector<Endpoint> discoverUsb(std::chrono::milliseconds timeout =
                                  std::chrono::milliseconds::zero());
Endpoint chooseEndpoint(const Options& options);

}  // namespace od
