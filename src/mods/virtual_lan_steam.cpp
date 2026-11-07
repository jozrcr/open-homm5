// The virtual LAN's Steam transport: [online] mode=steam (docs/plans/online-steam.md,
// step 3). The core (virtual_lan.cpp) hands it the game's packets; it carries them
// between Steam friends.
//
// In plain words:
// - Our id is our SteamID. Steam starts at the game's first DirectInput call
//   (Boot; steam_runtime.h), long before the game's LAN screen.
// - Messages go with ISteamNetworkingMessages: to a friend's SteamID, on channel =
//   the game's destination port, unreliable and without delay (the game resends
//   what it needs, as on a LAN), the session restarted if it broke. Steam picks a
//   direct path or its relays. A message to ourselves stays in this PC.
// - When our game hosts (the core tells: it answers queries), we make a
//   friends-only Steam lobby. Its data (virtual_lan_logic.h, VLAN_KEY_*) says which
//   H5 Together and which game it is, our game's discovery port, our fake
//   address and those of the players in it. We keep it while the game hosts and
//   for a minute after, then leave it.
// - Known hosts: every 2 s we look at our Steam friends. Steam tells which lobby
//   a friend is in; a friend in a lobby whose data says that friend hosts the same
//   game is a host, and our game's "any games here?" queries go to it. So a
//   friend's game shows up in the game's own LAN list.
// - When our game plays with a host (sends it a join request), we join the host's
//   lobby. Its members are the other players, in the lobby's order: the core
//   learns their addresses before their first packet (games of 3 or more).
// - The first message from somebody comes with a session request: accepted only
//   from our Steam friends and from members of our lobby. Others are ignored.
// - A thread of ours runs Steam's callbacks every 20 ms (manual dispatch: never
//   on the game's threads), under the core's lock, as every other call here.
// - For the online UI (step 4, online_ui.cpp): our rich presence says that we
//   play with H5 Together's online play (so friends can count us); the friends'
//   games we cannot join, the invites (Steam's GameLobbyJoinRequested_t, and
//   "+connect_lobby" on the command line), whether Steam's overlay is enabled,
//   and the friends for the invite picker go into a Status the UI reads. The UI
//   only asks; the invites themselves (Steam's dialog, InviteUserToLobby) are
//   sent by this thread on its next tick.
// Log lines start with "vlan steam:". No SteamID or name goes to the log: ids
// and lobby ids are short hashes ("#3f2a", vlan::FormatShortId).
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "steam_api_flat.h"
#include "steam_runtime.h"
#include "steam_spike_logic.h"
#include "virtual_lan_transport.h"

#include "../hook/game_build.h"
#include "../hook/log.h"
#include "../hook/version.h"

namespace vlan {
namespace {

using namespace steamflat;

const DWORD kTickMs = 20;
const uint32_t kScanEveryMs = 2000;
const int kScanYieldEvery = 8;  // friends looked at between two pauses of the lock (ScanFriendsNow)
const uint32_t kAskDataEveryMs = 10000;  // RequestLobbyData for a friend's lobby we are not in
const uint32_t kForgetLobbyMs = 10000;   // a friend's lobby not seen in the friends list for this long
// A friend's lobby whose data does not come (Steam may not show a friends-only lobby's
// data to somebody outside it: not tested yet) is tried anyway, as a host on the
// game's usual discovery port: its game answers our queries only if it hosts.
const uint32_t kGuessAfterMs = 6000;
const uint16_t kUsualDiscoveryPort = 8889;  // net_lan_match_maker_port's default
const uint32_t kMembersEveryMs = 1000;
const uint32_t kSessionInfoAfterMs = 3000;
const uint32_t kStatsEveryMs = 60000;
const int kLobbyMembers = 8;  // the game's most players
const int kMaxMembers = 16;
const int kMaxWatched = 16;
const int kSelfQueue = 64;
const int kMaxSessions = 32;
// the online UI
const char kPresenceKey[] = "h5t_version";  // our rich presence: friends count who plays with H5 Together
const uint32_t kInviteKeepMs = 10 * 60000;  // an invite's host is looked for this long
const int kMaxInvites = online::Status::kMaxInvites;
const uint32_t kFriendsEveryMs = 2000;      // the picker's list, while it is open
const int kMaxFriends = 200;
const uint32_t kOverlayEveryMs = 5000;      // IsOverlayEnabled, again
const int kMaxAsked = 64;                   // friends whose rich presence we asked for

// The UI's handler of a new invite (online_ui.cpp), called by our thread outside the lock.
void (*g_invite_handler)(const online::Invite &invite) = nullptr;

// A friend's lobby we look at.
struct Watched {
	uint64_t lobby;
	uint64_t friend_id;
	bool asked;
	uint32_t asked_ms;
	uint32_t first_asked_ms;
	bool guessed;  // its data never came: tried at the usual discovery port (kGuessAfterMs)
	uint32_t seen_ms;
	LobbyInfo info;
	HostCheck check;
	bool logged;
	HostCheck logged_check;
	uint16_t logged_port;
	char name[online::kNameSize];  // the friend's Steam name, for the UI only (never logged)
};

// An invite (Steam's GameLobbyJoinRequested_t), or "+connect_lobby" on the command line.
struct InviteEntry {
	uint64_t lobby;
	uint64_t from;      // the friend it came through (0: not known)
	uint32_t at_ms;
	uint32_t asked_ms;  // RequestLobbyData
	bool asked;
	LobbyInfo info;
	HostCheck check;
	bool logged;
	char name[online::kNameSize];
};

// The PC's fine clock, in microseconds.
int64_t QpcMicros()
{
	static LARGE_INTEGER freq = {};
	LARGE_INTEGER now;
	if (!freq.QuadPart)
		QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&now);
	return now.QuadPart * 1000000 / freq.QuadPart;
}

// How long some calls took, for the minute line: under Proton every call into
// Steam crosses to the Linux Steam client, and our thread's work holds the
// core's lock, which the game's own sends and reads wait for.
struct Timing {
	long count;
	int64_t total_us, max_us;
	void Add(int64_t us)
	{
		++count;
		total_us += us;
		max_us = us > max_us ? us : max_us;
	}
};

// Somebody who sent us a message or asked for a session.
struct Session {
	uint64_t id;
	bool asked, accepted, said_ignored, logged_info;
	uint32_t first_ms;  // its first message (0: none yet)
	long messages;
	long messages_at_status;  // `messages` at its last quality line
};

struct SelfMessage {
	bool used;
	uint16_t channel;
	uint32_t at_ms;
	uint64_t order;
	size_t size;
	uint8_t data[kMaxMessage];
};

// The lobby data keys we write, in this order (WriteLobbyData).
const char *const kKeys[] = {VLAN_KEY_PROTOCOL, VLAN_KEY_VERSION, VLAN_KEY_GAME, VLAN_KEY_HOST,
                             VLAN_KEY_PORT,     VLAN_KEY_IP,      VLAN_KEY_IPS};
const int kKeyCount = sizeof(kKeys) / sizeof(kKeys[0]);

void Short(uint64_t id, char *out, size_t size)
{
	FormatShortId(id, out, size);
}

// A relay data centre's code, packed in 32 bits.
void PopName(uint32_t pop, char *out, size_t size)
{
	char code[5] = {};
	int n = 0;
	for (int shift = 24; shift >= 0; shift -= 8) {
		const char c = static_cast<char>((pop >> shift) & 0xff);
		if (c >= 'a' && c <= 'z')
			code[n++] = c;
	}
	_snprintf_s(out, size, _TRUNCATE, "%s", n ? code : "none");
}

class SteamTransport : public Transport {
public:
	SteamTransport(CRITICAL_SECTION *lock, uint32_t app_id, bool reliable)
		: lock_(lock), app_id_(app_id), reliable_(reliable)
	{
	}

	const char *Name() const override { return "steam"; }
	uint64_t SelfId() const override { return me_; }

	bool Boot() override
	{
		const steamrt::Steam *steam = steamrt::Start(app_id_, "vlan steam");
		if (!steam)
			return false;
		s_ = *steam;
		if (s_.steam_app_id != app_id_)
			hooklog::Write("vlan steam: note: Steam says we run as app %u, not %u (a steam_appid.txt?)", s_.steam_app_id,
			               app_id_);
		MakeGameName();
		s_.api.init_relay(s_.netu);
		EnterCriticalSection(lock_);
		me_ = s_.me;
		booted_ = true;
		CheckRelay(GetTickCount());
		LeaveCriticalSection(lock_);
		char me[12];
		Short(me_, me, sizeof(me));
		hooklog::Write("vlan steam: Steam started in %u ms as app %u; we are %s; our game is \"%s\"; lobbies are friends "
		               "only; %s",
		               s_.init_ms, s_.steam_app_id, me, game_,
		               reliable_ ? "messages reliable (a lost one is sent again, as on a LAN)"
		                         : "messages unreliable (each sent once, as UDP)");
		// the online UI: friends see that we play with H5 Together's online play
		const bool presence = s_.api.set_rich_presence(s_.friends, kPresenceKey, OPEN_HOMM5_VERSION);
		boot_ms_ = GetTickCount();
		overlay_enabled_ = s_.api.overlay_enabled(s_.utils);
		overlay_checked_ = boot_ms_;
		hooklog::Write("vlan steam: our rich presence %s=%s %s; Steam's overlay %s at Steam's start (%s)", kPresenceKey,
		               OPEN_HOMM5_VERSION, presence ? "set" : "NOT set", overlay_enabled_ ? "enabled" : "not enabled",
		               s_.launched_by_steam ? "Steam started the game" : "the game was started without Steam");
		const uint64_t connect = online::ConnectLobby(GetCommandLineW());
		if (connect) {
			char lobby[12];
			Short(connect, lobby, sizeof(lobby));
			hooklog::Write("vlan steam: Steam started the game for lobby %s (+connect_lobby): its host is looked for", lobby);
			EnterCriticalSection(lock_);
			AddInvite(connect, 0, GetTickCount(), false);
			LeaveCriticalSection(lock_);
		}
		HANDLE thread = CreateThread(nullptr, 0, &ThreadMain, this, 0, nullptr);
		if (!thread) {
			hooklog::Write("vlan steam: cannot start our thread for Steam's callbacks (error %lu)", GetLastError());
			EnterCriticalSection(lock_);
			booted_ = false;
			LeaveCriticalSection(lock_);
			return false;
		}
		CloseHandle(thread);
		return true;
	}

	bool Start() override { return booted_; }

	void Poll(uint32_t now_ms) override { (void)now_ms; }  // (our thread does the work)

	bool Send(uint64_t to, uint16_t channel, const uint8_t *message, size_t size) override
	{
		if (!booted_ || !to || size > kMaxMessage)
			return false;
		if (to == me_)
			return PushSelf(channel, message, size);
		// Reliable by default: the game was made for a LAN, where nothing is lost. A lost
		// "accepted" answer to a join was never sent again by the joined game: the join
		// failed with "Timeout" over 4G (online-steam-test2.md, run 2).
		const int64_t t0 = QpcMicros();
		const int r = s_.api.send(s_.msgs, IdentityOf(to), message, static_cast<uint32_t>(size),
		                          (reliable_ ? kSendReliableNoNagle : kSendUnreliableNoNagle) |
		                              kSendAutoRestartBrokenSession,
		                          channel);
		send_time_.Add(QpcMicros() - t0);
		if (r != kResultOk) {
			++refused_;
			if (refused_ <= 5) {
				char who[12];
				Short(to, who, sizeof(who));
				hooklog::Write("vlan steam: a %u-byte message to %s on channel %u -> %s (EResult %d)%s",
				               static_cast<unsigned>(size), who, channel, steamspike::ResultName(r), r,
				               refused_ == 5 ? " (the last one logged)" : "");
			}
			return false;
		}
		++sent_;
		return true;
	}

	bool Receive(uint16_t channel, Received *out) override
	{
		if (!booted_)
			return false;
		if (PopSelf(channel, out))
			return true;
		for (int tries = 0; tries < 64; ++tries) {
			Message *m = nullptr;
			const int64_t t0 = QpcMicros();
			const int got = s_.api.receive(s_.msgs, channel, &m, 1);
			receive_time_.Add(QpcMicros() - t0);
			if (got <= 0 || !m)
				return false;
			const uint64_t from = SteamIdOf(m->peer);
			const size_t size = m->size > 0 ? static_cast<size_t>(m->size) : 0;
			const bool ok = from && m->data && size <= kMaxMessage;
			const uint32_t now = GetTickCount();
			if (ok) {
				out->from = from;
				out->channel = channel;
				out->size = size;
				memcpy(out->data, m->data, size);
				const Microseconds age = s_.api.local_timestamp(s_.netu) - m->time_received_us;
				out->at_ms = now - (age > 0 ? static_cast<uint32_t>(age / 1000) : 0);
			}
			s_.api.release(m);
			if (!ok) {
				++bad_;
				continue;
			}
			++received_;
			plan_.HeardFrom(from, now);
			if (Session *session = SessionOf(from, true)) {
				++session->messages;
				if (!session->first_ms)
					session->first_ms = now ? now : 1;
			}
			return true;
		}
		return false;
	}

	void SetHosting(uint16_t discovery_port) override
	{
		hosting_port_ = discovery_port;
		plan_.SetHosting(discovery_port, GetTickCount());
		data_dirty_ = true;
	}

	void PlaysWith(uint64_t peer) override
	{
		if (!hosts_.Has(peer))
			return;  // (only a host has a lobby to join)
		uint64_t lobby = 0;
		for (int i = 0; i < watched_count_; ++i)
			if ((watched_[i].check == HostCheck::kOk && watched_[i].info.host == peer) ||
			    (watched_[i].guessed && watched_[i].friend_id == peer))
				lobby = watched_[i].lobby;  // (in it, its data is ours to read)
		plan_.PlaysWith(peer, lobby, GetTickCount());
	}

	void SetOwnAddress(uint32_t address) override
	{
		own_address_ = address;
		data_dirty_ = true;
		member_data_dirty_ = true;
	}

	const KnownHosts &Hosts() const override { return hosts_; }

	int Taken(uint32_t *out, int max) const override
	{
		int n = 0;
		for (int i = 0; i < watched_count_; ++i) {
			const Watched &w = watched_[i];
			if (w.check != HostCheck::kOk || w.lobby == plan_.Lobby())
				continue;  // (the lobby we are in lists us)
			if (w.info.address && n < max)
				out[n++] = w.info.address;
			for (int j = 0; j < w.info.taken_count && n < max; ++j)
				out[n++] = w.info.taken[j];
		}
		return n;
	}

	int Members(Member *out, int max) const override
	{
		int n = 0;
		for (; n < member_count_ && n < max; ++n)
			out[n] = members_[n];
		return n;
	}

	void Describe(char *out, size_t size) const override
	{
		_snprintf_s(out, size, _TRUNCATE, "Steam app %u, friends-only lobbies", app_id_);
	}

	// --- the online UI (the core's lock is held) ---------------------------------------
	void GetStatus(online::Status *out) const override { *out = status_; }

	void WantFriends(bool on) override
	{
		if (on && !want_friends_)
			friends_at_ = GetTickCount() - kFriendsEveryMs;  // (at once)
		want_friends_ = on;
	}

	void GetFriends(std::vector<online::Friend> *out) const override { *out = friends_; }

	bool AskInviteDialog() override
	{
		if (!OwnLobby())
			return false;
		ask_dialog_ = true;
		return true;
	}

	bool AskInvite(uint64_t friend_id) override
	{
		if (!OwnLobby() || !friend_id)
			return false;
		if (asks_.size() < 16)
			asks_.push_back(friend_id);
		return true;
	}

	int OverlayOpened() const override { return overlay_opened_; }

private:
	// Our thread, for as long as the game runs.
	static DWORD WINAPI ThreadMain(LPVOID self)
	{
		SteamTransport *t = static_cast<SteamTransport *>(self);
		for (;;) {
			EnterCriticalSection(t->lock_);
			const int64_t t0 = QpcMicros();
			t->Tick(GetTickCount());
			t->tick_time_.Add(QpcMicros() - t0);
			online::Invite fresh[kMaxInvites];
			int fresh_count = 0;
			for (; fresh_count < t->fresh_count_ && fresh_count < kMaxInvites; ++fresh_count)
				fresh[fresh_count] = t->fresh_[fresh_count];
			t->fresh_count_ = 0;
			LeaveCriticalSection(t->lock_);
			for (int i = 0; i < fresh_count; ++i)  // (outside the lock: the pop-up, the flash)
				if (g_invite_handler)
					g_invite_handler(fresh[i]);
			Sleep(kTickMs);
		}
	}

	bool OwnLobby() const { return plan_.Where() == LobbyPlan::In::kOwn && plan_.Lobby() && hosting_port_; }

	void Tick(uint32_t now)
	{
		PumpCallbacks(now);
		if (now - relay_checked_ >= (relay_ok_ ? 10000u : 1000u))
			CheckRelay(now);
		FollowPlan(now);
		if (plan_.Where() != LobbyPlan::In::kNone) {
			if (members_dirty_ || now - members_at_ >= kMembersEveryMs)
				RefreshMembers(now);
			if (plan_.Where() == LobbyPlan::In::kOwn && data_dirty_)
				WriteLobbyData();
			if (plan_.Where() != LobbyPlan::In::kNone && member_data_dirty_)
				WriteMemberData();
		}
		if (scan_now_ || now - scanned_at_ >= kScanEveryMs) {
			ScanFriends(now);
			FollowInvites(now);
		}
		hosts_.Expire(now);
		LogSessions(now);
		LogStats(now);
		// the online UI
		DoAsks();
		if (want_friends_ && now - friends_at_ >= kFriendsEveryMs)
			ReadFriends(now);
		if (now - overlay_checked_ >= kOverlayEveryMs)
			CheckOverlay(now);
		UpdateStatus();
	}

	// --- Steam's callbacks ---------------------------------------------------------
	void PumpCallbacks(uint32_t now)
	{
		s_.api.run_frame(s_.pipe);
		CallbackMsg msg = {};
		for (int i = 0; i < 256 && s_.api.next_callback(s_.pipe, &msg); ++i) {
			OnCallback(msg, now);
			s_.api.free_last_callback(s_.pipe);
		}
	}

	void OnCallback(const CallbackMsg &msg, uint32_t now)
	{
		const void *p = msg.param;
		switch (msg.id) {
		case kCallApiCallCompleted:
			if (msg.size >= static_cast<int>(sizeof(ApiCallCompleted)))
				OnCallCompleted(*static_cast<const ApiCallCompleted *>(p), now);
			return;
		case kCallRelayNetworkStatus:
			CheckRelay(now);
			return;
		case kCallLobbyDataUpdate:
			if (msg.size >= static_cast<int>(sizeof(LobbyDataUpdate))) {
				const LobbyDataUpdate *u = static_cast<const LobbyDataUpdate *>(p);
				if (u->lobby == plan_.Lobby())
					members_dirty_ = true;  // (a member's data: its address)
				else
					scan_now_ = true;  // (a friend's lobby whose data we asked for)
			}
			return;
		case kCallLobbyChatUpdate:
			if (msg.size >= static_cast<int>(sizeof(LobbyChatUpdate)))
				OnMembersChanged(*static_cast<const LobbyChatUpdate *>(p), now);
			return;
		case kCallSessionRequest:
			if (msg.size >= static_cast<int>(sizeof(SessionRequest)))
				OnSessionRequest(*static_cast<const SessionRequest *>(p));
			return;
		case kCallSessionFailed:
			if (msg.size >= static_cast<int>(sizeof(SessionFailed)))
				OnSessionFailed(static_cast<const SessionFailed *>(p)->info);
			return;
		case kCallGameLobbyJoinRequested:
			if (msg.size >= static_cast<int>(sizeof(GameLobbyJoinRequested))) {
				const GameLobbyJoinRequested *r = static_cast<const GameLobbyJoinRequested *>(p);
				char lobby[12], who[12];
				Short(r->lobby, lobby, sizeof(lobby));
				Short(r->friend_id, who, sizeof(who));
				hooklog::Write("vlan steam: an invite (or \"Join game\" in Steam's friends list): lobby %s, through %s", lobby,
				               r->friend_id ? who : "nobody known");
				AddInvite(r->lobby, r->friend_id, now, true);
			}
			return;
		case kCallGameOverlayActivated:
			if (msg.size >= static_cast<int>(sizeof(GameOverlayActivated))) {
				const GameOverlayActivated *o = static_cast<const GameOverlayActivated *>(p);
				if (o->active)
					++overlay_opened_;
				hooklog::Write("vlan steam: Steam's overlay %s%s", o->active ? "opened" : "closed",
				               o->user_initiated ? " (by the player)" : "");
			}
			return;
		default:
			return;  // (LobbyEnter_t also comes for our own lobby: the call results tell us)
		}
	}

	void OnCallCompleted(const ApiCallCompleted &c, uint32_t now)
	{
		alignas(8) uint8_t buf[512] = {};
		bool failed = false;
		char lobby[12];
		if (c.call && c.call == create_call_) {
			create_call_ = 0;
			const bool got = s_.api.call_result(s_.pipe, c.call, buf, sizeof(LobbyCreated), kCallLobbyCreated, &failed);
			const LobbyCreated &r = *reinterpret_cast<const LobbyCreated *>(buf);
			if (got && !failed && r.result == kResultOk && r.lobby) {
				plan_.Created(r.lobby);
				EnteredLobby();
				Short(r.lobby, lobby, sizeof(lobby));
				hooklog::Write("vlan steam: our lobby %s is made (friends only): our friends see our game now", lobby);
			} else {
				hooklog::Write("vlan steam: Steam did not make our lobby: %s (EResult %d)%s; trying again in %u s",
				               steamspike::ResultName(r.result), r.result, failed ? ", call failed" : "",
				               LobbyPlan::kRetryMs / 1000);
				plan_.Failed(now);
			}
		} else if (c.call && c.call == join_call_) {
			join_call_ = 0;
			const bool got = s_.api.call_result(s_.pipe, c.call, buf, sizeof(LobbyEnter), kCallLobbyEnter, &failed);
			const LobbyEnter &r = *reinterpret_cast<const LobbyEnter *>(buf);
			Short(r.lobby ? r.lobby : joining_, lobby, sizeof(lobby));
			if (got && !failed && r.response == kEnterSuccess && r.lobby) {
				plan_.Joined(r.lobby);
				EnteredLobby();
				hooklog::Write("vlan steam: we are in lobby %s now, %d members", lobby,
				               s_.api.lobby_members(s_.mm, r.lobby));
			} else {
				hooklog::Write("vlan steam: Steam did not let us into lobby %s (answer %u%s); trying again in %u s while "
				               "our game plays with its host",
				               lobby, r.response, failed ? ", call failed" : "", LobbyPlan::kRetryMs / 1000);
				plan_.Failed(now);
			}
		}
	}

	void OnMembersChanged(const LobbyChatUpdate &u, uint32_t now)
	{
		if (!u.lobby || u.lobby != plan_.Lobby())
			return;
		members_dirty_ = true;
		const bool gone = (u.state_change & (kMemberLeft | kMemberDisconnected | kMemberKicked | kMemberBanned)) != 0;
		char who[12];
		Short(u.user_changed, who, sizeof(who));
		hooklog::Write("vlan steam: %s %s the lobby", who, gone ? "left" : (u.state_change & kMemberEntered) ? "entered" : "changed in");
		if (gone && u.user_changed == me_) {
			plan_.Left();  // (put out of it)
			member_count_ = 0;
		} else if (gone && plan_.Where() == LobbyPlan::In::kJoined && u.user_changed == plan_.Host()) {
			LeaveLobby(now, "its host left it");
			plan_.Forget();
		}
	}

	void OnSessionRequest(const SessionRequest &r)
	{
		const uint64_t from = SteamIdOf(r.remote);
		Session *session = SessionOf(from, true);
		const bool member = IsMember(from);
		const bool fr = from && IsFriend(from);
		const bool allowed = from && (from == me_ || member || fr);
		char who[12];
		Short(from, who, sizeof(who));
		if (allowed) {
			const bool ok = s_.api.accept_session(s_.msgs, r.remote);
			++accepted_;
			if (session && !session->accepted)
				hooklog::Write("vlan steam: %s (%s) starts talking to us: accepted%s", who,
				               from == me_ ? "ourselves" : member ? "in our lobby" : "a friend",
				               ok ? "" : " (but Steam says no such session)");
			if (session) {
				session->asked = true;
				session->accepted = true;
			}
		} else {
			++ignored_;
			if (session && !session->said_ignored)
				hooklog::Write("vlan steam: %s wants to talk to us: ignored (not a friend, not in our lobby)", who);
			if (session)
				session->said_ignored = true;
		}
	}

	void OnSessionFailed(const ConnectionInfo &info)
	{
		char who[12];
		Short(SteamIdOf(info.remote), who, sizeof(who));
		char debug[72];
		_snprintf_s(debug, sizeof(debug), _TRUNCATE, "%s", info.end_debug);
		hooklog::Write("vlan steam: the session with %s failed: %s, end reason %d \"%s\" (Steam starts it again with the "
		               "next message)",
		               who, steamspike::ConnectionStateName(info.state), info.end_reason, debug);
	}

	// --- the relay network ---------------------------------------------------------
	void CheckRelay(uint32_t now)
	{
		relay_checked_ = now;
		RelayNetworkStatus status = {};
		const int avail = s_.api.relay_status(s_.netu, &status);
		if (!relay_logged_ || avail != relay_avail_) {
			char debug[160];
			_snprintf_s(debug, sizeof(debug), _TRUNCATE, "%s", status.debug_msg);
			hooklog::Write("vlan steam: relay network: %s \"%s\"", steamspike::AvailabilityName(avail), debug);
			relay_logged_ = true;
			relay_avail_ = avail;
		}
		relay_ok_ = avail == kAvailCurrent;
	}

	// --- our lobby -------------------------------------------------------------------
	void FollowPlan(uint32_t now)
	{
		uint64_t lobby = 0;
		char id[12], host[12];
		switch (plan_.Next(now, &lobby)) {
		case LobbyPlan::Action::kCreate:
			create_call_ = s_.api.create_lobby(s_.mm, kLobbyFriendsOnly, kLobbyMembers);
			hooklog::Write("vlan steam: our game hosts: making a friends-only lobby");
			if (!create_call_) {
				hooklog::Write("vlan steam: Steam refused to make a lobby; trying again in %u s", LobbyPlan::kRetryMs / 1000);
				plan_.Failed(now);
			}
			return;
		case LobbyPlan::Action::kJoin:
			joining_ = lobby;
			join_call_ = s_.api.join_lobby(s_.mm, lobby);
			Short(lobby, id, sizeof(id));
			Short(plan_.Host(), host, sizeof(host));
			hooklog::Write("vlan steam: our game plays with %s's game: joining its lobby %s", host, id);
			if (!join_call_)
				plan_.Failed(now);
			return;
		case LobbyPlan::Action::kLeave:
			LeaveLobby(now, plan_.Where() == LobbyPlan::In::kOwn ? "our game has not hosted for a minute"
			                : hosting_port_                       ? "our game hosts now"
			                                                      : "our game no longer plays with its host");
			return;
		default:
			return;
		}
	}

	void LeaveLobby(uint32_t now, const char *why)
	{
		(void)now;
		char id[12];
		Short(plan_.Lobby(), id, sizeof(id));
		s_.api.leave_lobby(s_.mm, plan_.Lobby());
		hooklog::Write("vlan steam: left lobby %s (%s)", id, why);
		plan_.Left();
		member_count_ = 0;
	}

	void EnteredLobby()
	{
		members_dirty_ = true;
		member_data_dirty_ = true;
		data_dirty_ = true;
		for (int i = 0; i < kKeyCount; ++i)
			written_[i][0] = 0;
		written_any_ = false;
	}

	void WriteLobbyData()
	{
		data_dirty_ = false;
		const uint64_t lobby = plan_.Lobby();
		char values[kKeyCount][200];
		_snprintf_s(values[0], sizeof(values[0]), _TRUNCATE, "%d", kLobbyProtocol);
		_snprintf_s(values[1], sizeof(values[1]), _TRUNCATE, "%s", OPEN_HOMM5_VERSION);
		_snprintf_s(values[2], sizeof(values[2]), _TRUNCATE, "%s", game_);
		_snprintf_s(values[3], sizeof(values[3]), _TRUNCATE, "%llu", static_cast<unsigned long long>(me_));
		_snprintf_s(values[4], sizeof(values[4]), _TRUNCATE, "%u", hosting_port_);
		FormatIp(own_address_, values[5], sizeof(values[5]));
		uint32_t ips[kMaxTaken];
		int n = 0;
		ips[n++] = own_address_;
		for (int i = 0; i < member_count_ && n < kMaxTaken; ++i)
			if (members_[i].address)
				ips[n++] = members_[i].address;
		FormatAddressList(ips, n, values[6], sizeof(values[6]));
		int changed = 0;
		for (int i = 0; i < kKeyCount; ++i) {
			if (written_any_ && strcmp(values[i], written_[i]) == 0)
				continue;
			if (s_.api.set_lobby_data(s_.mm, lobby, kKeys[i], values[i])) {
				strcpy_s(written_[i], sizeof(written_[i]), values[i]);
				++changed;
			} else {
				data_dirty_ = true;  // (try again next tick)
			}
		}
		const bool first = !written_any_;
		written_any_ = true;
		if (changed)
			hooklog::Write("vlan steam: our lobby's data %s: H5 Together %s, protocol %s, discovery port %s, addresses %s",
			               first ? "set" : "now", values[1], values[0], values[4], values[6]);
	}

	void WriteMemberData()
	{
		member_data_dirty_ = false;
		char ip[20];
		FormatIp(own_address_, ip, sizeof(ip));
		s_.api.set_member_data(s_.mm, plan_.Lobby(), VLAN_KEY_IP, ip);
	}

	void RefreshMembers(uint32_t now)
	{
		members_at_ = now;
		members_dirty_ = false;
		const uint64_t lobby = plan_.Lobby();
		const int n = s_.api.lobby_members(s_.mm, lobby);
		Member found[kMaxMembers];
		int count = 0;
		bool host_here = false;
		for (int i = 0; i < n && count < kMaxMembers; ++i) {
			const uint64_t id = s_.api.lobby_member(s_.mm, lobby, i);
			if (!id)
				continue;
			host_here = host_here || id == plan_.Host();
			if (id == me_)
				continue;
			found[count++] = {id, ParseIp(s_.api.get_member_data(s_.mm, lobby, id, VLAN_KEY_IP))};
		}
		bool changed = count != member_count_;
		for (int i = 0; i < count; ++i) {
			changed = changed || found[i].id != members_[i].id || found[i].address != members_[i].address;
			members_[i] = found[i];
		}
		member_count_ = count;
		if (changed) {
			data_dirty_ = true;  // (h5t_ips)
			char list[200];
			uint32_t ips[kMaxMembers];
			for (int i = 0; i < count; ++i)
				ips[i] = found[i].address;
			FormatAddressList(ips, count, list, sizeof(list));
			hooklog::Write("vlan steam: lobby members besides us: %d%s%s", count, count ? ", at " : "", list);
		}
		if (plan_.Where() == LobbyPlan::In::kJoined && n > 0 && !host_here) {
			LeaveLobby(now, "its host is not in it any more");
			plan_.Forget();
		}
	}

	// --- our friends' games ------------------------------------------------------------
	void ScanFriends(uint32_t now)
	{
		const int64_t t0 = QpcMicros();
		ScanFriendsNow(now);
		scan_time_.Add(QpcMicros() - t0);
	}

	void ScanFriendsNow(uint32_t now)
	{
		scanned_at_ = now;
		scan_now_ = false;
		const int count = s_.api.friend_count(s_.friends, kFriendFlagImmediate);
		int playing = 0, in_lobbies = 0, with_us = 0;
		for (int i = 0; i < count; ++i) {
			if (i && i % kScanYieldEvery == 0) {
				// Let the game's sends and reads in: under Proton a whole scan held the
				// lock 17 ms on average (27 at most), and they waited for it. A real
				// sleep: with SwitchToThread this thread took the lock back at once.
				LeaveCriticalSection(lock_);
				Sleep(1);
				EnterCriticalSection(lock_);
			}
			const uint64_t id = s_.api.friend_by_index(s_.friends, i, kFriendFlagImmediate);
			FriendGameInfo game = {};
			if (!id || !s_.api.friend_game(s_.friends, id, &game) || steamspike::AppIdOfGameId(game.game_id) != app_id_)
				continue;
			++playing;
			if (WithUs(id))
				++with_us;
			const uint64_t lobby = LobbyOf(game);
			if (!lobby)
				continue;
			++in_lobbies;
			Watched *w = Watch(lobby, id, now);
			if (!w)
				continue;
			w->seen_ms = now;
			online::CopyName(s_.api.friend_name(s_.friends, id), w->name, sizeof(w->name));
			const bool member = lobby == plan_.Lobby();
			if (!member && (!w->asked || now - w->asked_ms >= kAskDataEveryMs)) {
				if (!w->asked)
					w->first_asked_ms = now;
				w->asked = true;
				w->asked_ms = now;
				s_.api.request_lobby_data(s_.mm, lobby);
			}
			ReadLobby(lobby, &w->info);
			w->check = CheckHost(w->info, id, game_);
			if (w->info.host == me_)
				continue;  // (our own lobby, which a friend joined)
			LogWatched(*w);
			if (w->check == HostCheck::kOk) {
				hosts_.Update(w->info.host, w->info.port, now, w->info.address);
			} else if (w->check == HostCheck::kNoData && w->asked && now - w->first_asked_ms >= kGuessAfterMs) {
				if (!w->guessed) {
					char who[12], id_text[12];
					Short(id, who, sizeof(who));
					Short(lobby, id_text, sizeof(id_text));
					hooklog::Write("vlan steam: friend %s's lobby %s: its data does not come; our queries go to its game "
					               "at the usual port %u anyway (it answers only if it hosts)",
					               who, id_text, kUsualDiscoveryPort);
				}
				w->guessed = true;
				hosts_.Update(id, kUsualDiscoveryPort, now, 0);
			}
		}
		for (int i = 0; i < watched_count_;) {  // lobbies no friend is in any more
			if (now - watched_[i].seen_ms >= kForgetLobbyMs)
				watched_[i] = watched_[--watched_count_];
			else
				++i;
		}
		if (!scanned_once_ || playing != friends_playing_ || in_lobbies != friends_in_lobbies_ || with_us != friends_with_us_)
			hooklog::Write("vlan steam: %d of our %d Steam friends play app %u now, %d of them with H5 Together's online "
			               "play, %d in a lobby",
			               playing, count, app_id_, with_us, in_lobbies);
		scanned_once_ = true;
		friends_playing_ = playing;
		friends_in_lobbies_ = in_lobbies;
		friends_with_us_ = with_us;
	}

	// Does this friend (playing the game) play with H5 Together's online play? Their rich
	// presence says so; Steam sends it for friends in the same game, and we ask once more.
	bool WithUs(uint64_t id)
	{
		const char *v = s_.api.friend_rich_presence(s_.friends, id, kPresenceKey);
		if (v && v[0])
			return true;
		for (int i = 0; i < asked_count_; ++i)
			if (asked_[i] == id)
				return false;
		if (asked_count_ < kMaxAsked) {
			asked_[asked_count_++] = id;
			s_.api.request_rich_presence(s_.friends, id);
		}
		return false;
	}

	// --- the online UI: invites ----------------------------------------------------------
	void AddInvite(uint64_t lobby, uint64_t from, uint32_t now, bool notify)
	{
		if (!lobby)
			return;
		InviteEntry *e = nullptr;
		for (int i = 0; i < invite_count_ && !e; ++i)
			if (invites_[i].lobby == lobby)
				e = &invites_[i];
		if (!e) {
			if (invite_count_ == kMaxInvites) {  // the oldest goes
				int oldest = 0;
				for (int i = 1; i < invite_count_; ++i)
					if (now - invites_[i].at_ms > now - invites_[oldest].at_ms)
						oldest = i;
				invites_[oldest] = invites_[--invite_count_];
			}
			e = &invites_[invite_count_++];
			*e = InviteEntry();
			e->lobby = lobby;
			e->check = HostCheck::kNoData;
		}
		e->from = from ? from : e->from;
		e->at_ms = now ? now : 1;
		e->asked = false;
		if (e->from)
			online::CopyName(s_.api.friend_name(s_.friends, e->from), e->name, sizeof(e->name));
		++invite_serial_;
		scan_now_ = true;
		if (notify && fresh_count_ < kMaxInvites) {
			online::Invite &v = fresh_[fresh_count_++];
			v = online::Invite();
			memcpy(v.name, e->name, sizeof(v.name));
		}
	}

	// Each invite's lobby: its data (without joining), and its host as a known host.
	void FollowInvites(uint32_t now)
	{
		for (int i = 0; i < invite_count_;) {
			InviteEntry &e = invites_[i];
			if (now - e.at_ms >= kInviteKeepMs) {
				invites_[i] = invites_[--invite_count_];
				continue;
			}
			++i;
			const bool member = e.lobby == plan_.Lobby();
			if (!member && (!e.asked || now - e.asked_ms >= kAskDataEveryMs)) {
				e.asked = true;
				e.asked_ms = now;
				s_.api.request_lobby_data(s_.mm, e.lobby);
			}
			ReadLobby(e.lobby, &e.info);
			const uint64_t host = e.info.host ? e.info.host : e.from;
			e.check = CheckHost(e.info, host, game_);
			if (!e.name[0] && e.info.host && e.info.host != me_)
				online::CopyName(s_.api.friend_name(s_.friends, e.info.host), e.name, sizeof(e.name));
			if (e.check == HostCheck::kOk && e.info.host != me_)
				hosts_.Update(e.info.host, e.info.port, now, e.info.address);
			if (!e.logged && e.check != HostCheck::kNoData) {
				e.logged = true;
				char lobby[12];
				Short(e.lobby, lobby, sizeof(lobby));
				hooklog::Write("vlan steam: the invite's lobby %s: %s%s", lobby, HostCheckName(e.check),
				               e.check == HostCheck::kOk ? ": its host gets our queries (its game shows in the LAN list)" : "");
			}
		}
	}

	// --- the online UI: the picker, Steam's dialog, the overlay -----------------------------
	void DoAsks()
	{
		if (!ask_dialog_ && asks_.empty())
			return;
		char lobby[12];
		Short(plan_.Lobby(), lobby, sizeof(lobby));
		if (ask_dialog_) {
			ask_dialog_ = false;
			if (OwnLobby()) {
				overlay_enabled_ = s_.api.overlay_enabled(s_.utils);
				s_.api.invite_dialog(s_.friends, plan_.Lobby());
				hooklog::Write("vlan steam: Steam's invite dialog asked for, lobby %s (overlay %s)", lobby,
				               overlay_enabled_ ? "enabled" : "NOT enabled");
			}
		}
		for (const uint64_t id : asks_) {
			if (!OwnLobby())
				break;
			const bool ok = s_.api.invite_user(s_.mm, plan_.Lobby(), id);
			bool found = false;
			for (auto &r : invited_)
				if (r.first == id) {
					r.second = ok ? 1 : -1;
					found = true;
				}
			if (!found)
				invited_.push_back({id, ok ? 1 : -1});
			for (online::Friend &f : friends_)
				if (f.id == id)
					f.invited = ok ? 1 : -1;
			char who[12];
			Short(id, who, sizeof(who));
			hooklog::Write("vlan steam: invited friend %s to our lobby %s -> %s", who, lobby, ok ? "sent" : "REFUSED by Steam");
		}
		asks_.clear();
	}

	void ReadFriends(uint32_t now)
	{
		friends_at_ = now;
		std::vector<online::Friend> list;
		const int count = s_.api.friend_count(s_.friends, kFriendFlagImmediate);
		for (int i = 0; i < count && static_cast<int>(list.size()) < kMaxFriends; ++i) {
			const uint64_t id = s_.api.friend_by_index(s_.friends, i, kFriendFlagImmediate);
			const int persona = id ? s_.api.friend_state(s_.friends, id) : 0;
			if (!online::KeepFriend(persona))
				continue;
			online::Friend f = {};
			f.id = id;
			f.persona = persona;
			online::CopyName(s_.api.friend_name(s_.friends, id), f.name, sizeof(f.name));
			FriendGameInfo game = {};
			f.playing = s_.api.friend_game(s_.friends, id, &game) && steamspike::AppIdOfGameId(game.game_id) == app_id_;
			if (f.playing) {
				const char *v = s_.api.friend_rich_presence(s_.friends, id, kPresenceKey);
				f.with_us = v && v[0];
			}
			for (const auto &r : invited_)
				if (r.first == id)
					f.invited = r.second;
			list.push_back(f);
		}
		online::SortFriends(&list);
		const bool changed = list.size() != friends_.size();
		friends_.swap(list);
		if (changed || !friends_logged_) {
			friends_logged_ = true;
			hooklog::Write("vlan steam: the invite list: %d of our %d Steam friends are online", static_cast<int>(friends_.size()),
			               count);
		}
	}

	void CheckOverlay(uint32_t now)
	{
		overlay_checked_ = now;
		const bool on = s_.api.overlay_enabled(s_.utils);
		if (on != overlay_enabled_)
			hooklog::Write("vlan steam: Steam's overlay is %s now, %u s after Steam's start (%s)", on ? "enabled" : "not enabled",
			               (now - boot_ms_) / 1000, s_.launched_by_steam ? "Steam started the game" : "started without Steam");
		overlay_enabled_ = on;
	}

	void UpdateStatus()
	{
		online::Status s;
		s.state = online::State::kOn;
		s.relay_ok = relay_ok_;
		s.friends_playing = friends_playing_;
		s.friends_with_us = friends_with_us_;
		for (int i = 0; i < hosts_.Count(); ++i)
			s.hosts += hosts_.At(i).id != me_;
		for (int i = 0; i < watched_count_ && s.problem_count < online::Status::kMaxProblems; ++i) {
			const Watched &w = watched_[i];
			if ((w.check != HostCheck::kOtherGame && w.check != HostCheck::kOtherProtocol) || w.info.host == me_)
				continue;
			online::Problem &p = s.problems[s.problem_count++];
			memcpy(p.name, w.name, sizeof(p.name));
			p.check = w.check;
			strcpy_s(p.version, sizeof(p.version), w.info.version);
		}
		// the invites, the newest first
		int order[kMaxInvites];
		for (int i = 0; i < invite_count_; ++i)
			order[i] = i;
		const uint32_t now = GetTickCount();
		for (int i = 1; i < invite_count_; ++i)
			for (int j = i; j > 0 && now - invites_[order[j]].at_ms < now - invites_[order[j - 1]].at_ms; --j) {
				const int t = order[j];
				order[j] = order[j - 1];
				order[j - 1] = t;
			}
		for (int i = 0; i < invite_count_; ++i) {
			const InviteEntry &e = invites_[order[i]];
			online::Invite &v = s.invites[s.invite_count++];
			memcpy(v.name, e.name, sizeof(v.name));
			v.visible = e.check == HostCheck::kOk && hosts_.Has(e.info.host);
			v.cannot = e.check == HostCheck::kOtherGame || e.check == HostCheck::kOtherProtocol;
			v.lobby = e.lobby;
			v.host = e.check == HostCheck::kOk ? e.info.host : 0;
		}
		s.invite_serial = invite_serial_;
		s.own_lobby = OwnLobby();
		s.overlay = overlay_enabled_;
		status_ = s;
	}

	Watched *Watch(uint64_t lobby, uint64_t friend_id, uint32_t now)
	{
		for (int i = 0; i < watched_count_; ++i)
			if (watched_[i].lobby == lobby && watched_[i].friend_id == friend_id)
				return &watched_[i];
		if (watched_count_ >= kMaxWatched)
			return nullptr;
		Watched &w = watched_[watched_count_++];
		w = Watched();
		w.lobby = lobby;
		w.friend_id = friend_id;
		w.seen_ms = now;
		w.check = HostCheck::kNoData;
		return &w;
	}

	void ReadLobby(uint64_t lobby, LobbyInfo *out)
	{
		const char *v[kKeyCount];
		for (int i = 0; i < kKeyCount; ++i)
			v[i] = s_.api.get_lobby_data(s_.mm, lobby, kKeys[i]);
		*out = ParseLobbyInfo(v[0], v[1], v[2], v[3], v[4], v[5], v[6]);
	}

	void LogWatched(Watched &w)
	{
		if (w.logged && w.check == w.logged_check && w.info.port == w.logged_port)
			return;
		if (w.check == HostCheck::kNoData && !w.logged && DataOnTheWay(w))
			return;
		w.logged = true;
		w.logged_check = w.check;
		w.logged_port = w.info.port;
		char who[12], lobby[12], ip[20];
		Short(w.friend_id, who, sizeof(who));
		Short(w.lobby, lobby, sizeof(lobby));
		FormatIp(w.info.address, ip, sizeof(ip));
		switch (w.check) {
		case HostCheck::kOk:
			hooklog::Write("vlan steam: friend %s hosts a game: lobby %s, H5 Together %s%s, discovery port %u, address %s, "
			               "%d player address(es) in it",
			               who, lobby, w.info.version,
			               strcmp(w.info.version, OPEN_HOMM5_VERSION) == 0 ? "" : " (not ours: the co-op guard compares)",
			               w.info.port, ip, w.info.taken_count);
			return;
		case HostCheck::kOtherGame:
			hooklog::Write("vlan steam: friend %s hosts a game we cannot join (lobby %s): its game is \"%s\", ours \"%s\"",
			               who, lobby, w.info.game, game_);
			return;
		case HostCheck::kOtherProtocol:
			hooklog::Write("vlan steam: friend %s hosts a game we cannot join (lobby %s): its H5 Together %s talks "
			               "protocol %d, ours (%s) %d",
			               who, lobby, w.info.version, w.info.protocol, OPEN_HOMM5_VERSION, kLobbyProtocol);
			return;
		default:
			hooklog::Write("vlan steam: friend %s is in lobby %s: %s", who, lobby, HostCheckName(w.check));
			return;
		}
	}

	// A friend's lobby whose data we asked Steam for only now: its answer may still come.
	bool DataOnTheWay(const Watched &w) const { return GetTickCount() - w.asked_ms < kScanEveryMs * 2; }

	// --- sessions ---------------------------------------------------------------------
	Session *SessionOf(uint64_t id, bool add)
	{
		if (!id)
			return nullptr;
		for (int i = 0; i < session_count_; ++i)
			if (sessions_[i].id == id)
				return &sessions_[i];
		if (!add || session_count_ >= kMaxSessions)
			return nullptr;
		Session &s = sessions_[session_count_++];
		s = Session();
		s.id = id;
		return &s;
	}

	void LogSessions(uint32_t now)
	{
		for (int i = 0; i < session_count_; ++i) {
			Session &s = sessions_[i];
			if (s.logged_info || !s.first_ms || now - s.first_ms < kSessionInfoAfterMs || s.id == me_)
				continue;
			s.logged_info = true;
			ConnectionInfo info = {};
			RealTimeStatus status = {};
			const int state = s_.api.session_info(s_.msgs, IdentityOf(s.id), &info, &status);
			char who[12], relay[8];
			Short(s.id, who, sizeof(who));
			PopName(info.pop_relay, relay, sizeof(relay));
			const bool relayed = (info.flags & kConnectionFlagRelayed) != 0;
			hooklog::Write("vlan steam: the session with %s: %s, ping %d ms, %s%s", who,
			               steamspike::ConnectionStateName(state), status.ping_ms,
			               relayed ? "through Steam's relay in " : "direct (not relayed)", relayed ? relay : "");
		}
	}

	void LogStats(uint32_t now)
	{
		if (now - stats_at_ < kStatsEveryMs)
			return;
		stats_at_ = now;
		const long activity = sent_ + received_ + refused_ + accepted_ + ignored_;
		if (activity == stats_logged_)
			return;
		stats_logged_ = activity;
		hooklog::Write("vlan steam: so far: %ld messages sent (%ld refused), %ld received (%ld unreadable), %ld to "
		               "ourselves; sessions: %ld accepted, %ld ignored; %d known host(s)",
		               sent_, refused_, received_, bad_, to_self_, accepted_, ignored_, hosts_.Count());
		LogQuality();
		LogTimings();
	}

	// The minute's call times (see Timing), then a fresh count.
	void LogTimings()
	{
		const Timing *all[] = {&send_time_, &receive_time_, &tick_time_, &scan_time_};
		char text[4][64];
		for (int i = 0; i < 4; ++i)
			_snprintf_s(text[i], sizeof(text[i]), _TRUNCATE, "%ld, avg %lld us, max %lld us", all[i]->count,
			            all[i]->count ? static_cast<long long>(all[i]->total_us / all[i]->count) : 0LL,
			            static_cast<long long>(all[i]->max_us));
		hooklog::Write("vlan steam: call times this minute: sends %s; reads %s; our thread's rounds (holding the lock "
		               "the game's sends and reads wait for) %s, of which friends scans %s",
		               text[0], text[1], text[2], text[3]);
		send_time_ = receive_time_ = tick_time_ = scan_time_ = Timing();
	}

	// How each session that brought messages since the last line does: its route (it can
	// change), Steam's own share of the packets that arrive, and what waits to be sent.
	void LogQuality()
	{
		for (int i = 0; i < session_count_; ++i) {
			Session &s = sessions_[i];
			if (s.id == me_ || s.messages == s.messages_at_status)
				continue;
			s.messages_at_status = s.messages;
			ConnectionInfo info = {};
			RealTimeStatus status = {};
			const int state = s_.api.session_info(s_.msgs, IdentityOf(s.id), &info, &status);
			char who[12], relay[8], here[16], there[16];
			Short(s.id, who, sizeof(who));
			PopName(info.pop_relay, relay, sizeof(relay));
			Share(status.quality_local, here, sizeof(here));
			Share(status.quality_remote, there, sizeof(there));
			const bool relayed = (info.flags & kConnectionFlagRelayed) != 0;
			hooklog::Write("vlan steam: the session with %s: %s, ping %d ms, %s%s; packets that arrive: %s of theirs "
			               "here, %s of ours there; waiting to go: %d bytes reliable (%d sent, not acknowledged), %d "
			               "unreliable",
			               who, steamspike::ConnectionStateName(state), status.ping_ms,
			               relayed ? "through Steam's relay in " : "direct", relayed ? relay : "", here, there,
			               status.pending_reliable, status.sent_unacked_reliable, status.pending_unreliable);
		}
	}

	static void Share(float quality, char *out, size_t size)
	{
		if (quality < 0.0f)
			_snprintf_s(out, size, _TRUNCATE, "?");  // (Steam does not know yet)
		else
			_snprintf_s(out, size, _TRUNCATE, "%.1f%%", quality * 100.0f);
	}

	bool IsMember(uint64_t id) const
	{
		for (int i = 0; i < member_count_; ++i)
			if (members_[i].id == id)
				return true;
		return false;
	}

	bool IsFriend(uint64_t id) const { return s_.api.friend_relationship(s_.friends, id) == kFriendRelationshipFriend; }

	// --- messages to ourselves -----------------------------------------------------------
	bool PushSelf(uint16_t channel, const uint8_t *message, size_t size)
	{
		int slot = -1;
		for (int i = 0; i < kSelfQueue && slot < 0; ++i)
			if (!self_[i].used)
				slot = i;
		if (slot < 0) {  // full: the oldest goes, as a full UDP buffer drops
			slot = 0;
			for (int i = 1; i < kSelfQueue; ++i)
				if (self_[i].order < self_[slot].order)
					slot = i;
		}
		SelfMessage &m = self_[slot];
		m.used = true;
		m.channel = channel;
		m.at_ms = GetTickCount();
		m.order = ++self_order_;
		m.size = size;
		memcpy(m.data, message, size);
		++to_self_;
		return true;
	}

	bool PopSelf(uint16_t channel, Received *out)
	{
		int oldest = -1;
		for (int i = 0; i < kSelfQueue; ++i)
			if (self_[i].used && self_[i].channel == channel && (oldest < 0 || self_[i].order < self_[oldest].order))
				oldest = i;
		if (oldest < 0)
			return false;
		SelfMessage &m = self_[oldest];
		out->from = me_;
		out->channel = channel;
		out->at_ms = m.at_ms;
		out->size = m.size;
		memcpy(out->data, m.data, m.size);
		m.used = false;
		return true;
	}

	// "mmh55/a1b2c3d4": what only the same game can play with (virtual_lan_logic.h, FormatGame).
	void MakeGameName()
	{
		const GameBuild build = DetectGameBuild();
		const char *name = build == GameBuild::Mmh55Utility ? "mmh55" : build == GameBuild::Vanilla31 ? "vanilla" : "unknown";
		wchar_t path[MAX_PATH];
		DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
		while (n > 0 && path[n - 1] != L'\\')
			--n;
		lstrcpynW(path + n, L"55u.dll", MAX_PATH - n);
		bool have = false;
		uint32_t hash = 0;
		// (only MMH5.5's exe loads 55u.dll: the 2008 game in a folder where MMH5.5 is installed
		// is the same game as without it, mmh55_files.cpp)
		HANDLE file = build == GameBuild::Mmh55Utility
		                  ? CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr)
		                  : INVALID_HANDLE_VALUE;
		if (file != INVALID_HANDLE_VALUE) {
			const DWORD size = GetFileSize(file, nullptr);
			if (size != INVALID_FILE_SIZE && size < (64u << 20)) {
				uint8_t *bytes = new uint8_t[size ? size : 1];
				DWORD got = 0;
				if (ReadFile(file, bytes, size, &got, nullptr) && got == size) {
					hash = HashBytes(bytes, size);
					have = true;
				}
				delete[] bytes;
			}
			CloseHandle(file);
		}
		FormatGame(name, have, hash, game_, sizeof(game_));
	}

	CRITICAL_SECTION *lock_;
	uint32_t app_id_;
	bool reliable_;
	steamrt::Steam s_ = {};
	volatile uint64_t me_ = 0;
	volatile bool booted_ = false;
	char game_[48] = "";
	uint32_t own_address_ = 0;
	uint16_t hosting_port_ = 0;

	LobbyPlan plan_;
	ApiCall create_call_ = 0, join_call_ = 0;
	uint64_t joining_ = 0;
	bool data_dirty_ = false, member_data_dirty_ = false, members_dirty_ = false;
	char written_[kKeyCount][200] = {};
	bool written_any_ = false;
	Member members_[kMaxMembers] = {};
	int member_count_ = 0;
	uint32_t members_at_ = 0;

	KnownHosts hosts_;
	Watched watched_[kMaxWatched] = {};
	int watched_count_ = 0;
	uint32_t scanned_at_ = 0;
	bool scan_now_ = true, scanned_once_ = false;
	int friends_playing_ = 0, friends_in_lobbies_ = 0;

	Session sessions_[kMaxSessions] = {};
	int session_count_ = 0;

	bool relay_logged_ = false, relay_ok_ = false;
	int relay_avail_ = 0;
	uint32_t relay_checked_ = 0;

	SelfMessage self_[kSelfQueue] = {};
	uint64_t self_order_ = 0;

	long sent_ = 0, refused_ = 0, received_ = 0, bad_ = 0, to_self_ = 0, accepted_ = 0, ignored_ = 0;
	Timing send_time_ = {}, receive_time_ = {}, tick_time_ = {}, scan_time_ = {};
	uint32_t stats_at_ = 0;
	long stats_logged_ = -1;

	// the online UI
	int friends_with_us_ = 0;
	uint64_t asked_[kMaxAsked] = {};
	int asked_count_ = 0;
	InviteEntry invites_[kMaxInvites] = {};
	int invite_count_ = 0;
	uint32_t invite_serial_ = 0;
	online::Invite fresh_[kMaxInvites] = {};  // new invites for the UI's handler (pop-up, flash)
	int fresh_count_ = 0;
	bool want_friends_ = false, friends_logged_ = false;
	uint32_t friends_at_ = 0;
	std::vector<online::Friend> friends_;
	std::vector<std::pair<uint64_t, int>> invited_;  // friend -> 1 sent, -1 refused
	bool ask_dialog_ = false;
	std::vector<uint64_t> asks_;
	uint32_t boot_ms_ = 0, overlay_checked_ = 0;
	bool overlay_enabled_ = false;
	int overlay_opened_ = 0;
	online::Status status_;
};

}  // namespace

Transport *MakeSteamTransport(CRITICAL_SECTION *lock, uint32_t app_id, bool reliable)
{
	return new SteamTransport(lock, app_id, reliable);
}

void SetSteamInviteHandler(void (*handler)(const online::Invite &invite))
{
	g_invite_handler = handler;
}

}  // namespace vlan
