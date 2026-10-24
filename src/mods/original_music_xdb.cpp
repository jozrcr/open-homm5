// Mod "original music", part 2: setting the music of one game data file back
// to the original game's (see original_music.cpp for the whole picture).
//
// The game's data files (.xdb) are small XML files. Music is chosen through
// two kinds of them:
//   - music definitions, Sounds/_(Music)/<folder>/<name>.xdb: one track
//     (<FileName>..\Music\...\x.ogg</FileName>) and its fade times; and music
//     tables in the same folder (TableRaceMusic.xdb: town, tavern and battle
//     music per faction);
//   - files that point at them, e.g. a map tile type
//     (MapObjects/_(AdvMapTile)/Grass/Flowers.xdb) has
//     <AdventureMusic href="/Sounds/_(Music)/AdvMap-Terrain/Grass.xdb#xpointer(/Music)"/>,
//     a hero (MapObjects/Haven/Nicolai.(AdvMapHeroShared).xdb) may have one too
//     (a hero theme), or <AdventureMusic/> for none.
// Plain text work only: no game code here, so it is tested on its own
// (src/hook/test/original_music_test.cpp).
#include "original_music.h"

#include <string.h>

#include <algorithm>
#include <initializer_list>

namespace mods {
namespace music {
namespace {

const char kMusicFolder[] = "sounds/_(music)/";

bool IsNameChar(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
	       c == '.' || c == ':';
}

char Lower(char c)
{
	return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool ContainsNoCase(const std::string &text, const char *what)
{
	const size_t n = strlen(what);
	if (text.size() < n)
		return false;
	for (size_t i = 0; i + n <= text.size(); ++i) {
		size_t k = 0;
		while (k < n && Lower(text[i + k]) == what[k])
			++k;
		if (k == n)
			return true;
	}
	return false;
}

// A reference points at music when it goes into the music folder.
bool IsMusicHref(const std::string &href)
{
	return ContainsNoCase(href, "_(music)/");
}

// The game's file names do not care about case.
bool SameHref(const std::string &a, const std::string &b)
{
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); ++i)
		if (Lower(a[i]) != Lower(b[i]))
			return false;
	return true;
}

// "/Sounds/_(Music)/AdvMap-Terrain/Grass.xdb#xpointer(/Music)" -> "AdvMap-Terrain/Grass.xdb"
std::string Short(const std::string &href)
{
	std::string s = href.substr(0, href.find('#'));
	const char prefix[] = "/sounds/_(music)/";
	const size_t n = sizeof(prefix) - 1;
	bool has = s.size() >= n;
	for (size_t i = 0; has && i < n; ++i)
		has = Lower(s[i]) == prefix[i];
	return has ? s.substr(n) : s;
}

// One start tag of the XML: <Tag ...> or <Tag .../>.
struct Element {
	size_t start;      // position of '<'
	size_t length;     // up to and with '>'
	std::string tag;
	bool simple;       // <Tag/> or <Tag href="..."/>, nothing else
	bool has_href;
	std::string href;
};

std::vector<Element> Scan(const std::string &xml)
{
	std::vector<Element> out;
	size_t i = 0;
	while ((i = xml.find('<', i)) != std::string::npos) {
		size_t p = i + 1;
		if (p >= xml.size() || !IsNameChar(xml[p]) || xml[p] == '-' || xml[p] == '.') {
			++i;  // </end>, <?xml ...?>, <!-- -->
			continue;
		}
		Element e{i, 0, std::string(), false, false, std::string()};
		while (p < xml.size() && IsNameChar(xml[p]))
			e.tag += xml[p++];
		const size_t close = xml.find('>', p);
		if (close == std::string::npos)
			break;
		e.length = close + 1 - i;
		// the attributes: nothing, or a single href="..."
		size_t q = p;
		while (q < close && (xml[q] == ' ' || xml[q] == '\t' || xml[q] == '\r' || xml[q] == '\n'))
			++q;
		if (xml.compare(q, 6, "href=\"") == 0) {
			const size_t end = xml.find('"', q + 6);
			if (end != std::string::npos && end < close) {
				e.has_href = true;
				e.href = xml.substr(q + 6, end - (q + 6));
				q = end + 1;
				while (q < close && (xml[q] == ' ' || xml[q] == '\t'))
					++q;
			}
		}
		e.simple = q + 1 == close && xml[q] == '/';
		out.push_back(e);
		i = close + 1;
	}
	return out;
}

bool IsMusic(const Element &e)
{
	return e.has_href && IsMusicHref(e.href);
}

std::string Text(const std::string &xml, const Element &e)
{
	return xml.substr(e.start, e.length);
}

// The track of a music definition: <FileName>..\Music\x.ogg</FileName>.
std::string TrackOf(const std::string &xml)
{
	const size_t a = xml.find("<FileName>");
	const size_t b = a == std::string::npos ? a : xml.find("</FileName>", a);
	return b == std::string::npos ? std::string() : xml.substr(a + 10, b - (a + 10));
}

bool IsMusicDefinition(const std::string &xml)
{
	const size_t root = xml.find("<Music");
	return root != std::string::npos && root + 6 < xml.size() && !IsNameChar(xml[root + 6]);
}

int CountMusicRefs(const std::string &xml)
{
	int n = 0;
	for (const Element &e : Scan(xml))
		n += e.has_href && ContainsNoCase(e.href, "(/music)") ? 1 : 0;
	return n;
}

}  // namespace

Result MakeOriginal(const std::string &path, const std::string &mod, const std::string *original, std::string *out,
                    std::vector<Change> *changes, std::string *note)
{
	changes->clear();
	note->clear();

	// Music definitions and tables: the original file as a whole.
	if (path.compare(0, sizeof(kMusicFolder) - 1, kMusicFolder) == 0) {
		if (!original || *original == mod)
			return Result::Unchanged;  // no original: the mod's own music, only played where the mod points at it
		*out = *original;
		if (IsMusicDefinition(mod) && IsMusicDefinition(*original))
			changes->push_back({"track", TrackOf(mod), TrackOf(*original)});
		else
			changes->push_back({"whole file", std::to_string(CountMusicRefs(mod)) + " music references",
			                    std::to_string(CountMusicRefs(*original))});
		return Result::Patched;
	}

	// Other files: element by element.
	const std::vector<Element> mine = Scan(mod);
	const std::vector<Element> theirs = original ? Scan(*original) : std::vector<Element>();
	std::vector<std::string> tags;  // elements that point at music in either file
	for (const std::vector<Element> *list : {&mine, &theirs})
		for (const Element &e : *list)
			if (IsMusic(e) && std::find(tags.begin(), tags.end(), e.tag) == tags.end())
				tags.push_back(e.tag);
	if (tags.empty())
		return Result::Unchanged;

	struct Edit {
		size_t start, length;
		std::string text;
	};
	std::vector<Edit> edits;
	for (const std::string &tag : tags) {
		std::vector<const Element *> a, b;
		for (const Element &e : mine)
			if (e.tag == tag)
				a.push_back(&e);
		for (const Element &e : theirs)
			if (e.tag == tag)
				b.push_back(&e);
		// the original game has no such file, or no such element in it: no music there
		const bool none = !original || b.empty();
		if (!none && a.size() != b.size()) {
			*note += (note->empty() ? "" : "; ") + tag + ": " + std::to_string(a.size()) + " in the mod's file, " +
			         std::to_string(b.size()) + " in the original";
			continue;
		}
		for (size_t k = 0; k < a.size(); ++k) {
			const Element &m = *a[k];
			if (none) {
				if (IsMusic(m) && m.simple) {
					edits.push_back({m.start, m.length, "<" + tag + "/>"});
					changes->push_back({tag, Short(m.href), std::string()});
				}
				continue;
			}
			const Element &o = *b[k];
			if (!(IsMusic(m) || IsMusic(o)) || !m.simple || !o.simple)
				continue;  // not music, or not a plain reference: left alone
			if (m.has_href == o.has_href && (!m.has_href || SameHref(m.href, o.href)))
				continue;  // the same music (perhaps written a bit differently)
			const std::string to = Text(*original, o);
			edits.push_back({m.start, m.length, to});
			changes->push_back({tag, m.has_href ? Short(m.href) : std::string(), o.has_href ? Short(o.href) : std::string()});
		}
	}
	if (edits.empty())
		return note->empty() ? Result::Unchanged : Result::Skipped;
	std::sort(edits.begin(), edits.end(), [](const Edit &x, const Edit &y) { return x.start > y.start; });
	*out = mod;
	for (const Edit &e : edits)
		out->replace(e.start, e.length, e.text);
	return Result::Patched;
}

}  // namespace music
}  // namespace mods
