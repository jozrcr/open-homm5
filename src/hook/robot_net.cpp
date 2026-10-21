// Robot network log: what the game does with sockets, for the LAN robot.
//
// The game uses WSOCK32.dll (imported by number). In robot mode with
// net_log=1 the robot replaces a few of those imports with pass-throughs that
// log the socket type, addresses, ports and errors:
//   socket, bind, connect, setsockopt, and per socket the first 100 packets sent
//   and received (only the first 3 of the "02" packets every copy broadcasts
//   once a second).
// Every call goes to the real function, with two optional changes for running
// several copies of the game on ONE PC. LAN games are found through broadcasts
// on UDP port 8889 (net_lan_match_maker_port), which every copy binds; the second
// copy's bind fails ("address in use", 10048) and its LAN screen never opens.
//   net_share_udp_ports=1   SO_REUSEADDR: every copy binds the same port and
//                           gets the broadcasts. But then all copies have the same
//                           address and the game cannot tell them apart.
//   net_lan_relay_ports=P,Q each copy uses its own discovery port instead (the
//                           runner sets net_lan_match_maker_port per copy), and
//                           every broadcast is also sent to 127.0.0.1:P, Q (the
//                           other copies' ports), so they see each other as
//                           separate machines and can answer.
#include <winsock2.h>
#include <windows.h>
#include <intrin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "robot.h"

namespace robot {
namespace {

typedef SOCKET(WINAPI *SocketFn)(int, int, int);
typedef int(WINAPI *BindFn)(SOCKET, const sockaddr *, int);
typedef int(WINAPI *SetSockOptFn)(SOCKET, int, int, const char *, int);
typedef int(WINAPI *SendToFn)(SOCKET, const char *, int, int, const sockaddr *, int);
typedef int(WINAPI *RecvFromFn)(SOCKET, char *, int, int, sockaddr *, int *);
typedef int(WINAPI *LastErrorFn)();
SocketFn g_socket = nullptr;
BindFn g_bind = nullptr;
BindFn g_connect = nullptr;
SetSockOptFn g_setsockopt = nullptr;
SendToFn g_sendto = nullptr;
RecvFromFn g_recvfrom = nullptr;
LastErrorFn g_last_error = nullptr;
bool g_share_udp_ports = false;
const int kMaxRelays = 8;
u_short g_relay_ports[kMaxRelays];  // host byte order
int g_relay_count = 0;

void Address(const sockaddr *a, char *out, size_t size)
{
	if (!a || a->sa_family != AF_INET) {
		_snprintf_s(out, size, _TRUNCATE, "?");
		return;
	}
	const sockaddr_in *in = reinterpret_cast<const sockaddr_in *>(a);
	const BYTE *ip = reinterpret_cast<const BYTE *>(&in->sin_addr);
	_snprintf_s(out, size, _TRUNCATE, "%u.%u.%u.%u:%u", ip[0], ip[1], ip[2], ip[3],
	            (static_cast<unsigned>(reinterpret_cast<const BYTE *>(&in->sin_port)[0]) << 8) |
	                reinterpret_cast<const BYTE *>(&in->sin_port)[1]);
}

int Error()
{
	return g_last_error ? g_last_error() : 0;
}

// The first few sends and receives per socket are logged.
const int kTracked = 64;
int g_logged_packets = 100;  // per socket and direction (net_log_packets)
const int kLoggedHellos = 3;
SOCKET g_udp[kTracked];  // UDP sockets
struct Counter {
	SOCKET socket;
	int sent, received, hellos_sent, hellos_received;
};

// Log this packet? The once-a-second "02" packets only a few times.
bool Wanted(Counter *c, const char *buf, int len, bool sent)
{
	if (!c)
		return false;
	if (buf && len == 5 && buf[0] == 0x02)
		return (sent ? c->hellos_sent++ : c->hellos_received++) < kLoggedHellos;
	return (sent ? c->sent++ : c->received++) < g_logged_packets;
}
Counter g_counters[kTracked];

Counter *CounterOf(SOCKET s)
{
	for (Counter &c : g_counters) {
		if (c.socket == s)
			return &c;
		if (c.socket == 0) {
			c.socket = s;
			return &c;
		}
	}
	return nullptr;
}

// A few bytes of a packet, as hex.
void Bytes(const char *buf, int len, char *out, size_t size)
{
	out[0] = 0;
	for (int i = 0; buf && i < len && i < 12; ++i) {
		char b[4];
		_snprintf_s(b, sizeof(b), _TRUNCATE, "%02x", static_cast<BYTE>(buf[i]));
		strncat_s(out, size, b, _TRUNCATE);
	}
}

SOCKET WINAPI RobotSocket(int af, int type, int protocol)
{
	const SOCKET s = g_socket(af, type, protocol);
	if (type == SOCK_DGRAM && s != INVALID_SOCKET) {
		for (SOCKET &u : g_udp) {
			if (u == 0) {
				u = s;
				break;
			}
		}
	}
	hooklog::Write("robot: net: socket(%s) -> %u%s", type == SOCK_DGRAM ? "udp" : type == SOCK_STREAM ? "tcp" : "?",
	               static_cast<unsigned>(s), s == INVALID_SOCKET ? " FAILED" : "");
	return s;
}

bool IsUdp(SOCKET s)
{
	for (SOCKET u : g_udp) {
		if (u == s)
			return true;
	}
	return false;
}

int WINAPI RobotBind(SOCKET s, const sockaddr *a, int len)
{
	if (g_share_udp_ports && IsUdp(s)) {
		const BOOL on = TRUE;
		g_setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&on), sizeof(on));
	}
	const int r = g_bind(s, a, len);
	const int err = r ? Error() : 0;
	char where[32];
	Address(a, where, sizeof(where));
	hooklog::Write("robot: net: bind(%u, %s) -> %d (error %d)", static_cast<unsigned>(s), where, r, err);
	return r;
}

int WINAPI RobotConnect(SOCKET s, const sockaddr *a, int len)
{
	const int r = g_connect(s, a, len);
	const int err = r ? Error() : 0;
	char where[32];
	Address(a, where, sizeof(where));
	hooklog::Write("robot: net: connect(%u, %s) -> %d (error %d)", static_cast<unsigned>(s), where, r, err);
	return r;
}

int WINAPI RobotSetSockOpt(SOCKET s, int level, int name, const char *value, int len)
{
	const int r = g_setsockopt(s, level, name, value, len);
	hooklog::Write("robot: net: setsockopt(%u, level %d, option %d, %d) -> %d", static_cast<unsigned>(s), level, name,
	               value && len >= 4 ? *reinterpret_cast<const int *>(value) : -1, r);
	return r;
}

int WINAPI RobotSendTo(SOCKET s, const char *buf, int len, int flags, const sockaddr *to, int to_len)
{
	const int r = g_sendto(s, buf, len, flags, to, to_len);
	// broadcasts (x.x.x.255) also go to the other copies of the game on this PC
	const sockaddr_in *in = reinterpret_cast<const sockaddr_in *>(to);
	if (g_relay_count && in && in->sin_family == AF_INET && reinterpret_cast<const BYTE *>(&in->sin_addr)[3] == 255) {
		for (int i = 0; i < g_relay_count; ++i) {
			sockaddr_in local = *in;
			const BYTE loopback[4] = {127, 0, 0, 1};
			memcpy(&local.sin_addr, loopback, 4);
			reinterpret_cast<BYTE *>(&local.sin_port)[0] = static_cast<BYTE>(g_relay_ports[i] >> 8);
			reinterpret_cast<BYTE *>(&local.sin_port)[1] = static_cast<BYTE>(g_relay_ports[i] & 0xff);
			g_sendto(s, buf, len, flags, reinterpret_cast<const sockaddr *>(&local), sizeof(local));
		}
	}
	if (Wanted(CounterOf(s), buf, len, true)) {
		char where[32], bytes[32];
		Address(to, where, sizeof(where));
		Bytes(buf, len, bytes, sizeof(bytes));
		hooklog::Write("robot: net: sendto(%u, %d bytes %s, %s) -> %d (error %d) from %p", static_cast<unsigned>(s), len,
		               bytes, where, r, r < 0 ? Error() : 0, _ReturnAddress());
	}
	return r;
}

int WINAPI RobotRecvFrom(SOCKET s, char *buf, int len, int flags, sockaddr *from, int *from_len)
{
	const int r = g_recvfrom(s, buf, len, flags, from, from_len);
	if (r >= 0 && Wanted(CounterOf(s), buf, r, false)) {
		char where[32], bytes[32];
		Address(from, where, sizeof(where));
		Bytes(buf, r, bytes, sizeof(bytes));
		hooklog::Write("robot: net: recvfrom(%u) -> %d bytes %s from %s, called from %p", static_cast<unsigned>(s), r, bytes,
		               where, _ReturnAddress());
	}
	return r;
}

}  // namespace

void InstallNetLog(bool share_udp_ports, const char *relay_ports, int logged_packets)
{
	g_share_udp_ports = share_udp_ports;
	if (logged_packets >= 0)
		g_logged_packets = logged_packets;
	for (const char *p = relay_ports; p && *p && g_relay_count < kMaxRelays;) {
		const int port = atoi(p);
		if (port > 0 && port < 65536)
			g_relay_ports[g_relay_count++] = static_cast<u_short>(port);
		p = strchr(p, ',');
		p = p ? p + 1 : nullptr;
	}
	HMODULE ws = GetModuleHandleA("WSOCK32.dll");  // a static import of the game: already mapped
	g_last_error = ws ? reinterpret_cast<LastErrorFn>(GetProcAddress(ws, "WSAGetLastError")) : nullptr;
	g_socket = reinterpret_cast<SocketFn>(PatchImport("WSOCK32.dll", "socket", reinterpret_cast<void *>(&RobotSocket)));
	g_bind = reinterpret_cast<BindFn>(PatchImport("WSOCK32.dll", "bind", reinterpret_cast<void *>(&RobotBind)));
	g_connect = reinterpret_cast<BindFn>(PatchImport("WSOCK32.dll", "connect", reinterpret_cast<void *>(&RobotConnect)));
	g_setsockopt = reinterpret_cast<SetSockOptFn>(
		PatchImport("WSOCK32.dll", "setsockopt", reinterpret_cast<void *>(&RobotSetSockOpt)));
	g_sendto = reinterpret_cast<SendToFn>(PatchImport("WSOCK32.dll", "sendto", reinterpret_cast<void *>(&RobotSendTo)));
	g_recvfrom = reinterpret_cast<RecvFromFn>(PatchImport("WSOCK32.dll", "recvfrom", reinterpret_cast<void *>(&RobotRecvFrom)));
	hooklog::Write("robot: net log: socket %s, bind %s, connect %s, setsockopt %s, sendto %s, recvfrom %s; "
	               "shared UDP ports %s, broadcast relays %d (%s)",
	               g_socket ? "yes" : "no", g_bind ? "yes" : "no", g_connect ? "yes" : "no", g_setsockopt ? "yes" : "no",
	               g_sendto ? "yes" : "no", g_recvfrom ? "yes" : "no", g_share_udp_ports ? "yes" : "no", g_relay_count,
	               relay_ports ? relay_ports : "");
}

}  // namespace robot
