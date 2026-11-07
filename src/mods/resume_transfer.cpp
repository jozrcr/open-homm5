// Quick resume: sending the host's resume save to a player who lacks it
// (docs/plans/turn-features.md, part 2; the owner's choice of 2026-11-04: over Steam).
//
// In plain words: the game lets a PC join a loaded game only if that PC has its own
// copy of the save. The friend whose game crashed missed the "Reprise" save the others
// made, so without this the friend cannot come back. Here, every PC with the virtual
// LAN on (online play over Steam; the robot's loopback) asks the hosts it knows, every
// few seconds, "do you offer a save?". A host offers the last resume save it made, only
// while it hosts a loaded game (its waiting room; never in a new game's lobby, 0.3.5)
// (quick_resume.cpp keeps its file). If this PC has no save of that size and checksum,
// it fetches the file, a window of pieces at a time (resume_transfer_logic.h), checks
// it, and writes it into its own saves folder as "h5together-reprise.sav". Then the
// player joins the host's game as usual. Nothing of the game itself changes.
//
// Everything runs inside the virtual LAN's polls, under its lock: each poll does a
// little (at most one request sent, the waiting messages taken in); the host reads its
// file once, and this PC looks through its saves folder once per offer.
#include "resume_transfer.h"

#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "online_ui.h"
#include "quick_resume.h"
#include "resume_transfer_logic.h"
#include "virtual_lan_transport.h"

#include "../hook/log.h"

namespace mods {
namespace {

const uint32_t kAskEveryMs = 4000;     // how often this PC asks each host it knows
const uint32_t kRetryMs = 1500;        // a window of pieces that did not all come: asked again
const uint32_t kGiveUpMs = 120000;     // no progress for this long: the transfer stops
const char kReceivedName[] = "h5together-reprise.sav";

// --- the host's side ----------------------------------------------------------------------------
struct Offer {
	std::string path;           // the resume save's file
	FILETIME written = {};      // when it was written (a new save: read it again)
	std::vector<uint8_t> file;  // its bytes
	uint32_t crc = 0;
	std::string name;           // its name, for the log
	bool ok = false;
};
Offer g_offer;

// The resume save this PC offers, read (again) if it is new. False: none.
bool LoadOffer()
{
	char path[MAX_PATH];
	if (!ResumeOfferFile(path, sizeof(path)))
		return false;
	WIN32_FILE_ATTRIBUTE_DATA info;
	if (!GetFileAttributesExA(path, GetFileExInfoStandard, &info) || info.nFileSizeHigh || !info.nFileSizeLow ||
	    info.nFileSizeLow > transfer::kMaxFile)
		return false;
	// (read again when the file changed: its time or its size)
	if (g_offer.ok && g_offer.path == path && CompareFileTime(&g_offer.written, &info.ftLastWriteTime) == 0 &&
	    g_offer.file.size() == info.nFileSizeLow)
		return true;
	g_offer = Offer();
	HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (f == INVALID_HANDLE_VALUE)
		return false;
	g_offer.file.resize(info.nFileSizeLow);
	DWORD got = 0;
	const bool read = ReadFile(f, g_offer.file.data(), info.nFileSizeLow, &got, nullptr) && got == info.nFileSizeLow;
	CloseHandle(f);
	if (!read) {
		g_offer = Offer();
		return false;
	}
	g_offer.path = path;
	g_offer.written = info.ftLastWriteTime;
	g_offer.crc = transfer::Crc32(g_offer.file.data(), g_offer.file.size());
	const char *slash = strrchr(path, '\\');
	g_offer.name = slash ? slash + 1 : path;
	g_offer.ok = true;
	hooklog::Write("resume transfer: this PC offers its resume save (%s, %u bytes, CRC %08x) to players who lack it",
	               g_offer.name.c_str(), static_cast<unsigned>(g_offer.file.size()), g_offer.crc);
	return true;
}

void Send(vlan::Transport *t, uint64_t to, const std::vector<uint8_t> &message)
{
	t->Send(to, transfer::kChannel, message.data(), message.size());
}

void HostAnswer(vlan::Transport *t, uint64_t from, const transfer::Message &m)
{
	if (m.kind == transfer::Kind::kAsk) {
		// Only while this PC hosts a loaded game (its waiting room): in a new game's lobby a
		// friend's PC fetched last night's resume save for nothing, with its "your friend's
		// resume save is coming" line (the owner, 2026-11-06).
		const bool loaded = OnlineInLoadedGameRoom();
		static int told = -1;
		if (told != (loaded ? 1 : 0)) {
			told = loaded ? 1 : 0;
			hooklog::Write(loaded ? "resume transfer: this PC hosts a loaded game: it offers its resume save, if it has one"
			                      : "resume transfer: a player asks, but this PC hosts no loaded game: no save offered");
		}
		if (loaded && LoadOffer())
			Send(t, from, transfer::EncodeOffer(static_cast<uint32_t>(g_offer.file.size()), g_offer.crc, g_offer.name));
		else
			Send(t, from, transfer::EncodeNoOffer());
		return;
	}
	if (m.kind != transfer::Kind::kGet || !g_offer.ok || m.crc != g_offer.crc)
		return;
	static uint32_t told_crc = 0;
	if (told_crc != m.crc && m.offset == 0) {
		told_crc = m.crc;
		hooklog::Write("resume transfer: a player asks for this PC's resume save: sending it");
	}
	for (uint32_t i = 0; i < m.count; ++i) {
		const uint64_t at = m.offset + static_cast<uint64_t>(i) * transfer::kPiece;
		if (at >= g_offer.file.size())
			break;
		const size_t n = g_offer.file.size() - at < transfer::kPiece ? g_offer.file.size() - static_cast<size_t>(at) : transfer::kPiece;
		Send(t, from, transfer::EncodeData(g_offer.crc, static_cast<uint32_t>(at), &g_offer.file[static_cast<size_t>(at)], n));
	}
}

// --- this PC's side: asking, fetching ------------------------------------------------------------
struct Asked {
	uint64_t host = 0;
	uint32_t at = 0;
};
const int kMaxAsked = 16;
Asked g_asked[kMaxAsked];
uint32_t g_known_crc = 0;      // an offer this PC already has (or got): not fetched again
uint32_t g_known_size = 0;
transfer::Receiver g_rx;
uint64_t g_rx_host = 0;
uint32_t g_rx_progress_at = 0; // when a piece last came
unsigned g_rx_percent_told = 0;
int g_state = 0;               // ResumeTransferState
unsigned g_percent = 0;

uint32_t FileCrc(const char *path, uint32_t size)
{
	HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (f == INVALID_HANDLE_VALUE)
		return 0;
	std::vector<uint8_t> bytes(size);
	DWORD got = 0;
	const bool ok = ReadFile(f, bytes.data(), size, &got, nullptr) && got == size;
	CloseHandle(f);
	return ok ? transfer::Crc32(bytes.data(), bytes.size()) : 0;
}

// Does this PC's saves folder hold a file of this size and CRC?
bool HaveSave(const char *folder, uint32_t size, uint32_t crc)
{
	char pattern[MAX_PATH];
	_snprintf_s(pattern, sizeof(pattern), _TRUNCATE, "%s*.sav", folder);
	WIN32_FIND_DATAA found;
	HANDLE h = FindFirstFileA(pattern, &found);
	if (h == INVALID_HANDLE_VALUE)
		return false;
	bool have = false;
	do {
		if (found.nFileSizeHigh || found.nFileSizeLow != size)
			continue;
		char path[MAX_PATH];
		_snprintf_s(path, sizeof(path), _TRUNCATE, "%s%s", folder, found.cFileName);
		have = FileCrc(path, size) == crc;
	} while (!have && FindNextFileA(h, &found));
	FindClose(h);
	return have;
}

void TakeOffer(uint64_t host, const transfer::Message &m)
{
	if (m.kind == transfer::Kind::kNoOffer || g_rx.Active())
		return;
	if (m.crc == g_known_crc && m.size == g_known_size)
		return;  // (this PC has it, or got it)
	char folder[MAX_PATH];
	if (!ResumeSavesFolder(folder, sizeof(folder))) {
		static bool told = false;
		if (!told) {
			told = true;
			hooklog::Write("resume transfer: a host offers a resume save, but this PC's saves folder is not known: not fetched");
		}
		return;
	}
	if (HaveSave(folder, m.size, m.crc)) {
		g_known_crc = m.crc;
		g_known_size = m.size;
		hooklog::Write("resume transfer: a host offers its resume save (%u bytes): this PC has it already", m.size);
		return;
	}
	g_rx.Start(m.size, m.crc);
	g_rx_host = host;
	g_rx_progress_at = GetTickCount();
	g_rx_percent_told = 0;
	g_state = 1;
	g_percent = 0;
	hooklog::Write("resume transfer: a host offers its resume save (%s on the host, %u bytes): this PC lacks it, fetching it",
	               m.name.c_str(), m.size);
}

void WriteReceived()
{
	char folder[MAX_PATH], path[MAX_PATH];
	if (!ResumeSavesFolder(folder, sizeof(folder))) {
		g_state = 3;
		return;
	}
	_snprintf_s(path, sizeof(path), _TRUNCATE, "%s%s", folder, kReceivedName);
	{
		// (a profile that never saved has no Saves folder yet: the original game's own profile,
		// mmh55_files.cpp, at its first game; robot 2026-11-06: error 3, the path not found)
		std::string dir = folder;
		while (!dir.empty() && (dir.back() == '\\' || dir.back() == '/'))
			dir.pop_back();
		SHCreateDirectoryExA(nullptr, dir.c_str(), nullptr);
	}
	HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	DWORD written = 0;
	const std::vector<uint8_t> &file = g_rx.File();
	const bool ok = f != INVALID_HANDLE_VALUE && WriteFile(f, file.data(), static_cast<DWORD>(file.size()), &written, nullptr) &&
	                written == file.size();
	if (f != INVALID_HANDLE_VALUE)
		CloseHandle(f);
	if (ok) {
		g_known_crc = g_rx.Crc();
		g_known_size = static_cast<uint32_t>(file.size());
		g_state = 2;
		hooklog::Write("resume transfer: the host's resume save is here (%u bytes, checked), written as %s: this PC can join "
		               "the host's loaded game",
		               static_cast<unsigned>(file.size()), kReceivedName);
	}
	else {
		g_state = 3;
		hooklog::Write("resume transfer: writing the received save into %s failed (error %lu)", folder, GetLastError());
	}
	g_rx.Finish();
}

void Receive(vlan::Transport *t, uint32_t now)
{
	vlan::Received in;
	for (int i = 0; i < 256 && t->Receive(transfer::kChannel, &in); ++i) {
		transfer::Message m;
		if (!transfer::Decode(in.data, in.size, &m))
			continue;
		switch (m.kind) {
		case transfer::Kind::kAsk:
		case transfer::Kind::kGet:
			HostAnswer(t, in.from, m);
			break;
		case transfer::Kind::kOffer:
		case transfer::Kind::kNoOffer:
			TakeOffer(in.from, m);
			break;
		case transfer::Kind::kData:
			if (g_rx.Active() && in.from == g_rx_host && g_rx.Take(m.crc, m.offset, m.data, m.data_size))
				g_rx_progress_at = now;
			break;
		default:
			break;
		}
	}
}

void Ask(vlan::Transport *t, uint64_t self, uint32_t now)
{
	const vlan::KnownHosts &hosts = t->Hosts();
	for (int i = 0; i < hosts.Count(); ++i) {
		const uint64_t host = hosts.At(i).id;
		if (host == self)
			continue;
		Asked *slot = nullptr;
		for (Asked &a : g_asked)
			if (a.host == host)
				slot = &a;
		if (!slot)
			for (Asked &a : g_asked)
				if (!slot && (!a.host || now - a.at > 60000))
					slot = &a;
		if (!slot || (slot->host == host && now - slot->at < kAskEveryMs))
			continue;
		slot->host = host;
		slot->at = now;
		Send(t, host, transfer::EncodeAsk());
	}
}

void Fetch(vlan::Transport *t, uint32_t now)
{
	if (!g_rx.Active())
		return;
	if (g_rx.Complete()) {
		WriteReceived();
		return;
	}
	if (now - g_rx_progress_at > kGiveUpMs) {
		hooklog::Write("resume transfer: no piece of the save for %lu s: stopped at %u%%", kGiveUpMs / 1000, g_rx.Percent());
		g_rx.Finish();
		g_state = 3;
		return;
	}
	g_percent = g_rx.Percent();
	if (g_percent >= g_rx_percent_told + 25) {
		g_rx_percent_told = g_percent - g_percent % 25;
		hooklog::Write("resume transfer: %u%% of the host's resume save", g_percent);
	}
	uint32_t offset, count;
	if (g_rx.Next(now, kRetryMs, &offset, &count))
		Send(t, g_rx_host, transfer::EncodeGet(g_rx.Crc(), offset, count));
}

}  // namespace

void ResumeTransferPoll(vlan::Transport *transport, uint64_t self, uint32_t now_ms)
{
	if (!transport || !self)
		return;
	Receive(transport, now_ms);
	Ask(transport, self, now_ms);
	Fetch(transport, now_ms);
}

int ResumeTransferState(unsigned *percent)
{
	if (percent)
		*percent = g_percent;
	return g_state;
}

}  // namespace mods
