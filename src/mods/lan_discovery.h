// Mod: LAN games over VPNs such as ZeroTier (see lan_discovery.cpp).
#pragma once

namespace mods {

// With the settings of open-homm5.ini: [net] lan_all_adapters=1 (default) and
// lan_peers=a.b.c.d,... (default none).
void ApplyLanDiscoveryFromSettings();

}  // namespace mods
