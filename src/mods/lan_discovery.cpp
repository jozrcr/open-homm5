// Mod: LAN games over VPNs such as ZeroTier (docs/plans/install-and-join.md, B1).
//
// A player on the game's LAN screen finds hosted games by broadcasting a
// 5-byte query (first byte 02) to the discovery port about once a second, and
// hosts answer by unicast (docs/subsystems/multiplayer-sync.md, "How LAN games
// are found"). The game sends the query only to 255.255.255.255 and to the
// classful broadcast of one local address. Windows and Linux send
// 255.255.255.255 out of the main network card only, so on a VPN such as
// ZeroTier nobody hears it and the game list stays empty (tested 2026-10-23).
//
// This mod sends each query also
//   - to the broadcast address of every network adapter that is up, ZeroTier's
//     included ([net] lan_all_adapters=1, the default);
//   - to each address of [net] lan_peers=a.b.c.d,... (for VPNs that carry no
//     broadcasts at all, such as Tailscale: put the host's address there).
// The host answers to the address the query came from, through the right
// adapter. The game's own packets are sent unchanged; this only adds copies of
// the query.
//
// It also logs each address games were found at (packets 03 received) and
// each address join requests went to (packets 07 sent; the game sends them to
// every address of the other PC, its ZeroTier one included), so that a join
// that fails can be read in open-homm5.log.
//
// How: the exe imports WSOCK32.dll's sendto and recvfrom (by number); their
// import table entries point to the functions below (data, not code).
#include <winsock2.h>
#include <iphlpapi.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#include "lan_discovery.h"

#include "../hook/log.h"
#include "../hook/patch.h"

namespace mods {
namespace {

typedef int(WINAPI *SendToFn)(SOCKET, const char *, int, int, const sockaddr *, int);
typedef int(WINAPI *RecvFromFn)(SOCKET, char *, int, int, sockaddr *, int *);
SendToFn g_sendto = nullptr;
RecvFromFn g_recvfrom = nullptr;

bool g_all_adapters = true;
const int kMaxTargets = 16;
DWORD g_peers[kMaxTargets];  // IPv4 addresses in network byte order
int g_peer_count = 0;

// The adapters' broadcast addresses, refreshed every few seconds (adapters
// come and go: a VPN connects after the game started).
CRITICAL_SECTION g_lock;
DWORD g_broadcasts[kMaxTargets];
int g_broadcast_count = 0;
DWORD g_refreshed = 0;
char g_last_list[512] = "";

// The addresses already logged (games found, join requests), each logged once.
struct Seen {
	DWORD ip[kMaxTargets];
	int count;
};
Seen g_found = {};
Seen g_joins = {};

bool FirstTime(Seen &seen, DWORD ip)
{
	EnterCriticalSection(&g_lock);
	bool first = seen.count < kMaxTargets;
	for (int i = 0; first && i < seen.count; ++i)
		first = seen.ip[i] != ip;
	if (first)
		seen.ip[seen.count++] = ip;
	LeaveCriticalSection(&g_lock);
	return first;
}

void FormatIp(DWORD ip, char *out, size_t size)
{
	const BYTE *b = reinterpret_cast<const BYTE *>(&ip);
	_snprintf_s(out, size, _TRUNCATE, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
}

bool ParseIp(const wchar_t *s, DWORD *ip)
{
	unsigned a, b, c, d;
	wchar_t extra;
	if (swscanf_s(s, L"%u.%u.%u.%u%c", &a, &b, &c, &d, &extra, 1) != 4 || a > 255 || b > 255 || c > 255 || d > 255)
		return false;
	*ip = a | b << 8 | c << 16 | d << 24;
	return true;
}

// Broadcast addresses of the IPv4 adapters that are up (not loopback, not
// self-assigned 169.254.x.x); logs the list when it changes.
void RefreshBroadcasts()
{
	ULONG size = 16 * 1024;
	IP_ADAPTER_ADDRESSES *list = static_cast<IP_ADAPTER_ADDRESSES *>(malloc(size));
	if (!list)
		return;
	const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
	ULONG r = GetAdaptersAddresses(AF_INET, flags, nullptr, list, &size);
	if (r == ERROR_BUFFER_OVERFLOW) {
		free(list);
		list = static_cast<IP_ADAPTER_ADDRESSES *>(malloc(size));
		r = list ? GetAdaptersAddresses(AF_INET, flags, nullptr, list, &size) : ERROR_NOT_ENOUGH_MEMORY;
	}
	int count = 0;
	char text[512] = "";
	size_t used = 0;
	for (IP_ADAPTER_ADDRESSES *a = r == NO_ERROR ? list : nullptr; a && count < kMaxTargets; a = a->Next) {
		if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
			continue;
		for (IP_ADAPTER_UNICAST_ADDRESS *u = a->FirstUnicastAddress; u && count < kMaxTargets; u = u->Next) {
			const sockaddr_in *in = reinterpret_cast<const sockaddr_in *>(u->Address.lpSockaddr);
			const UINT8 prefix = u->OnLinkPrefixLength;
			if (!in || in->sin_family != AF_INET || prefix == 0 || prefix > 30)
				continue;
			const DWORD ip = _byteswap_ulong(in->sin_addr.s_addr);  // host order
			if ((ip >> 16) == 0xa9fe)                                // 169.254.x.x
				continue;
			const DWORD broadcast = _byteswap_ulong(ip | (0xffffffffu >> prefix));
			bool seen = false;
			for (int i = 0; i < count; ++i)
				seen = seen || g_broadcasts[i] == broadcast;
			if (seen)
				continue;
			g_broadcasts[count++] = broadcast;
			char address[20], name[64];
			FormatIp(broadcast, address, sizeof(address));
			WideCharToMultiByte(CP_UTF8, 0, a->FriendlyName, -1, name, sizeof(name), nullptr, nullptr);
			if (used + 100 < sizeof(text))
				used += static_cast<size_t>(_snprintf_s(text + used, sizeof(text) - used, _TRUNCATE, "%s%s (%s)",
				                                        count > 1 ? ", " : "", address, name));
		}
	}
	free(list);
	g_broadcast_count = count;
	if (strcmp(text, g_last_list) != 0) {
		lstrcpynA(g_last_list, text, sizeof(g_last_list));
		hooklog::Write("lan: game queries also go to %s", count ? text : "no adapter");
	}
}

void SendCopy(SOCKET s, const char *buf, int len, int flags, DWORD ip, u_short port_network_order)
{
	sockaddr_in to = {};
	to.sin_family = AF_INET;
	to.sin_port = port_network_order;
	to.sin_addr.s_addr = ip;
	g_sendto(s, buf, len, flags, reinterpret_cast<const sockaddr *>(&to), sizeof(to));
}

int WINAPI LanSendTo(SOCKET s, const char *buf, int len, int flags, const sockaddr *to, int to_len)
{
	const int r = g_sendto(s, buf, len, flags, to, to_len);
	const sockaddr_in *in = reinterpret_cast<const sockaddr_in *>(to);
	if (!buf || !in || to_len < static_cast<int>(sizeof(sockaddr_in)) || in->sin_family != AF_INET)
		return r;
	const BYTE *dest = reinterpret_cast<const BYTE *>(&in->sin_addr);
	if (len == 5 && buf[0] == 0x02 && dest[3] == 255) {
		// a game query to a broadcast address: copies to the other adapters and the peers
		EnterCriticalSection(&g_lock);
		if (g_all_adapters && (g_refreshed == 0 || GetTickCount() - g_refreshed > 10000)) {
			g_refreshed = GetTickCount();
			RefreshBroadcasts();
		}
		for (int i = 0; g_all_adapters && i < g_broadcast_count; ++i)
			if (g_broadcasts[i] != in->sin_addr.s_addr)
				SendCopy(s, buf, len, flags, g_broadcasts[i], in->sin_port);
		LeaveCriticalSection(&g_lock);
		for (int i = 0; i < g_peer_count; ++i)
			SendCopy(s, buf, len, flags, g_peers[i], in->sin_port);
	} else if (len == 9 && buf[0] == 0x07 && FirstTime(g_joins, in->sin_addr.s_addr)) {
		char address[20];
		FormatIp(in->sin_addr.s_addr, address, sizeof(address));
		hooklog::Write("lan: join request to %s:%u", address, _byteswap_ushort(in->sin_port));
	}
	return r;
}

int WINAPI LanRecvFrom(SOCKET s, char *buf, int len, int flags, sockaddr *from, int *from_len)
{
	const int r = g_recvfrom(s, buf, len, flags, from, from_len);
	const sockaddr_in *in = reinterpret_cast<const sockaddr_in *>(from);
	if (r > 0 && buf && buf[0] == 0x03 && in && in->sin_family == AF_INET && FirstTime(g_found, in->sin_addr.s_addr)) {
		char address[20];
		FormatIp(in->sin_addr.s_addr, address, sizeof(address));
		hooklog::Write("lan: game found at %s:%u (%d bytes)", address, _byteswap_ushort(in->sin_port), r);
	}
	return r;
}

}  // namespace

void ApplyLanDiscoveryFromSettings()
{
	wchar_t path[MAX_PATH];
	hooklog::PathNextToExe(L"open-homm5", L".ini", path, MAX_PATH);
	g_all_adapters = GetPrivateProfileIntW(L"net", L"lan_all_adapters", 1, path) != 0;
	wchar_t peers[512];
	GetPrivateProfileStringW(L"net", L"lan_peers", L"", peers, 512, path);
	char peer_text[256] = "";
	size_t used = 0;
	wchar_t *next = nullptr;
	for (wchar_t *token = wcstok_s(peers, L", ;", &next); token && g_peer_count < kMaxTargets;
	     token = wcstok_s(nullptr, L", ;", &next)) {
		DWORD ip;
		if (!ParseIp(token, &ip)) {
			hooklog::Write("lan: lan_peers: \"%ls\" is not an IPv4 address, left out", token);
			continue;
		}
		g_peers[g_peer_count++] = ip;
		char address[20];
		FormatIp(ip, address, sizeof(address));
		if (used + 24 < sizeof(peer_text))
			used += static_cast<size_t>(_snprintf_s(peer_text + used, sizeof(peer_text) - used, _TRUNCATE, "%s%s",
			                                        g_peer_count > 1 ? ", " : "", address));
	}
	InitializeCriticalSection(&g_lock);
	g_sendto = reinterpret_cast<SendToFn>(hookpatch::PatchImport("WSOCK32.dll", "sendto", reinterpret_cast<void *>(&LanSendTo)));
	g_recvfrom = reinterpret_cast<RecvFromFn>(
		hookpatch::PatchImport("WSOCK32.dll", "recvfrom", reinterpret_cast<void *>(&LanRecvFrom)));
	if (!g_sendto || !g_recvfrom) {
		hooklog::Write("lan: the game's sendto/recvfrom imports were not found: LAN over VPNs off");
		return;
	}
	hooklog::Write("lan: game queries also go to %s%s%s", g_all_adapters ? "every network adapter" : "no other adapter",
	               g_peer_count ? " and to the peers " : "", peer_text);
}

}  // namespace mods
