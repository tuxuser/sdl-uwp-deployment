#include "lan_address.h"

#include <winrt/windows.foundation.collections.h>
#include <winrt/windows.networking.connectivity.h>
#include <winrt/windows.networking.h>

namespace onboarding {

std::vector<std::string> LanIPv4Addresses() {
  using winrt::Windows::Networking::HostNameType;
  using winrt::Windows::Networking::Connectivity::NetworkInformation;

  std::vector<std::string> addresses;
  for (const auto& host : NetworkInformation::GetHostNames()) {
    if (host.Type() != HostNameType::Ipv4) continue;
    std::string address = winrt::to_string(host.CanonicalName());
    if (address.rfind("127.", 0) == 0 || address.rfind("169.254.", 0) == 0) continue;
    addresses.push_back(address);
  }
  return addresses;
}

}  // namespace onboarding
