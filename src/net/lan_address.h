#pragma once

#include <string>
#include <vector>

namespace onboarding {

// IPv4 addresses of this device that other LAN devices can reach. Loopback
// and link-local (169.254.x.x) addresses are left out.
std::vector<std::string> LanIPv4Addresses();

}  // namespace onboarding
