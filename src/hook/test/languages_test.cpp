// Test of the "languages" mod's work on bytes (src/mods/languages_text.cpp):
// paths, text encodings and a small zip archive made here. No game needed.
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "../../mods/languages.h"

using namespace mods::lang;

namespace {

int g_failures = 0;

void Check(bool ok, const char *what)
{
	printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		++g_failures;
}

std::string Utf16(const wchar_t *w)
{
	return std::string(reinterpret_cast<const char *>(w), wcslen(w) * sizeof(wchar_t));
}

void Put16(std::string *s, unsigned v)
{
	s->push_back(static_cast<char>(v & 0xff));
	s->push_back(static_cast<char>((v >> 8) & 0xff));
}

void Put32(std::string *s, unsigned v)
{
	Put16(s, v & 0xffff);
	Put16(s, v >> 16);
}

struct File {
	std::string name;
	std::string data;
	unsigned date;  // DOS date
	unsigned time;  // DOS time
};

// A zip archive with stored (method 0) files, as a .pak is.
std::string Zip(const std::vector<File> &files)
{
	std::string out, dir;
	for (const File &f : files) {
		const unsigned at = static_cast<unsigned>(out.size());
		Put32(&out, 0x04034b50);
		Put16(&out, 20);
		Put16(&out, 0);
		Put16(&out, 0);  // stored
		Put16(&out, f.time);
		Put16(&out, f.date);
		Put32(&out, 0);  // crc (not checked)
		Put32(&out, static_cast<unsigned>(f.data.size()));
		Put32(&out, static_cast<unsigned>(f.data.size()));
		Put16(&out, static_cast<unsigned>(f.name.size()));
		Put16(&out, 4);  // an extra field, to test DataStart
		out += f.name;
		out += std::string(4, '\0');
		out += f.data;
		Put32(&dir, 0x02014b50);
		Put16(&dir, 20);
		Put16(&dir, 20);
		Put16(&dir, 0);
		Put16(&dir, 0);
		Put16(&dir, f.time);
		Put16(&dir, f.date);
		Put32(&dir, 0);
		Put32(&dir, static_cast<unsigned>(f.data.size()));
		Put32(&dir, static_cast<unsigned>(f.data.size()));
		Put16(&dir, static_cast<unsigned>(f.name.size()));
		Put16(&dir, 0);
		Put16(&dir, 0);
		Put16(&dir, 0);
		Put16(&dir, 0);
		Put32(&dir, 0);
		Put32(&dir, at);
		dir += f.name;
	}
	const unsigned dir_at = static_cast<unsigned>(out.size());
	out += dir;
	Put32(&out, 0x06054b50);
	Put16(&out, 0);
	Put16(&out, 0);
	Put16(&out, static_cast<unsigned>(files.size()));
	Put16(&out, static_cast<unsigned>(files.size()));
	Put32(&out, static_cast<unsigned>(dir.size()));
	Put32(&out, dir_at);
	Put16(&out, 5);  // a comment after the end record
	out += "hello";
	return out;
}

}  // namespace

int main()
{
	// paths
	const char p1[] = "/Text\\Game/Spells/Combat/Haste/Name.TXT";
	Check(NormalizePath(p1, p1 + strlen(p1)) == "text/game/spells/combat/haste/name.txt", "path: no leading slash, '/', lower case");
	Check(IsTextPath(p1, p1 + strlen(p1)), "path: .TXT is a text");
	const char p2[] = "MapObjects/Haven/Nicolai.(AdvMapHeroShared).xdb";
	Check(!IsTextPath(p2, p2 + strlen(p2)), "path: .xdb is not a text");
	const char p3[] = ".txt";
	Check(!IsTextPath(p3, p3 + 4), "path: \".txt\" alone is not a file");

	// texts
	std::string out;
	const std::string game = "\xff\xfe" + Utf16(L"Joueur rouge\r\nligne 2");
	Check(ToGameText(game, &out) && out == game, "text: UTF-16 with its mark stays as it is");
	Check(ToGameText(game + "x", &out) && out == game, "text: an odd last byte is dropped");
	Check(ToGameText(Utf16(L"Joueur rouge\r\nligne 2"), &out) && out == game, "text: UTF-16 without the mark gets it");
	Check(ToGameText("\xef\xbb\xbfJoueur rouge\nligne 2", &out) && out == game, "text: UTF-8 with its mark, LF -> CRLF");
	Check(ToGameText("Joueur rouge\r\nligne 2", &out) && out == game, "text: UTF-8 without the mark, CRLF kept");
	const std::string accents = "\xff\xfe" + Utf16(L"D\x00e9g\x00e2ts : <value=damage>");
	Check(ToGameText("D\xc3\xa9g\xc3\xa2ts : <value=damage>", &out) && out == accents, "text: UTF-8 accents and tags");
	Check(ToGameText("", &out) && out == std::string("\xff\xfe", 2), "text: an empty file is an empty text");
	Check(!ToGameText("\xfe\xff\x00J", &out), "text: UTF-16 big endian is refused");
	Check(!ToGameText("D\xe9g\xe2ts", &out), "text: broken UTF-8 (an ANSI file) is refused");

	// a zip archive
	const std::string zip = Zip({{"Text/Game/A/Name.txt", game, 0x5a4b, 0x1000},
	                             {"UI/", "", 0x5a4b, 0x1000},
	                             {"UI\\B.txt", "\xef\xbb\xbf" "B", 0x5a4c, 0x0800}});
	const unsigned char *z = reinterpret_cast<const unsigned char *>(zip.data());
	DWORD dir_offset = 0, dir_size = 0;
	Check(FindDirectory(z, zip.size(), &dir_offset, &dir_size), "zip: end record found before a comment");
	std::vector<ZipEntry> entries;
	if (dir_offset + dir_size <= zip.size())
		ReadDirectory(z + dir_offset, dir_size, &entries);
	Check(entries.size() == 2, "zip: two files, the folder left out");
	if (entries.size() == 2) {
		Check(entries[0].key == "text/game/a/name.txt" && entries[1].key == "ui/b.txt", "zip: normalized names");
		Check(entries[0].method == 0 && entries[0].size == game.size() && entries[0].packed_size == game.size(), "zip: sizes");
		Check(entries[0].dos_time == (0x5a4bu << 16 | 0x1000u) && entries[1].dos_time > entries[0].dos_time,
		      "zip: dates compare in time order (a later date, an earlier hour)");
		const DWORD start = DataStart(z + entries[0].header);
		Check(start == 30 + 20 + 4 && zip.compare(entries[0].header + start, game.size(), game) == 0,
		      "zip: the data after the local header and its extra field");
	}
	Check(!FindDirectory(z, 21, &dir_offset, &dir_size), "zip: too short");
	Check(DataStart(z + 1) == 0, "zip: not a local header");

	printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
	return g_failures ? 1 : 0;
}
