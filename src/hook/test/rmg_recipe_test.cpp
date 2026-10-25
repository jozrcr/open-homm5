// Test of the recipe names (src/mods/rmg_recipe.cpp): choices -> OH-X1-... ->
// the same choices, every bad name refused, and the ready templates built from
// their recipe names. No game needed.
//   rmg_recipe_test.exe build/rmg_parity src/mods/rmg/templates
// (the first folder is written by test_template_maker.js --expected; the second
// holds the ready templates the install package ships).
#include <stdio.h>

#include <string>
#include <vector>

#include "../../mods/rmg_join.h"
#include "../../mods/rmg_maker.h"
#include "../../mods/rmg_recipe.h"
#include "rmg_cases.h"

using mods::rmg::BuildRecipe;
using mods::rmg::CanonicalRecipe;
using mods::rmg::Choices;
using mods::rmg::DecodeRecipe;
using mods::rmg::EncodeRecipe;
using mods::rmg::MakeTemplate;
using mods::rmg::Template;
using rmgtest::Case;

namespace {

int g_failures = 0;
int g_checks = 0;

bool Check(bool ok, const std::string &what)
{
	++g_checks;
	if (!ok && ++g_failures <= 60)
		printf("FAIL: %s\n", what.c_str());
	return ok;
}

bool Same(const Choices &a, const Choices &b)
{
	bool same = a.teams == b.teams && a.per_team == b.per_team && a.team_zones == b.team_zones &&
	            a.border_zones == b.border_zones && a.centre_zones == b.centre_zones && a.border_towns == b.border_towns &&
	            a.team_towns == b.team_towns && a.ally_link == b.ally_link && a.start_mines == b.start_mines &&
	            a.loot == b.loot && a.guards == b.guards && a.size == b.size && a.underground == b.underground &&
	            a.name == b.name;
	for (int i = 0; i < 4; i++)
		same = same && a.loot_by_distance[i] == b.loot_by_distance[i] && a.guards_by_distance[i] == b.guards_by_distance[i];
	return same;
}

std::string Lower(std::string s)
{
	for (char &c : s)
		c = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
	return s;
}

std::string Upper(std::string s)
{
	for (char &c : s)
		c = c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
	return s;
}

std::string Encoded(const Choices &c)
{
	std::string name;
	return EncodeRecipe(c, &name) ? name : "(refused)";
}

// The file a recipe serves: the maker's file with "*" after the name.
std::string Starred(const std::string &xml, const std::string &name)
{
	const std::string from = "<Name>" + name + "</Name>", to = "<Name>" + name + "*</Name>";
	const size_t at = xml.find(from);
	return at == std::string::npos ? std::string() : xml.substr(0, at) + to + xml.substr(at + from.size());
}

// The letters each place of the name takes, from the plan's table
// (docs/plans/in-game-generator.md), written apart from rmg_recipe.cpp's own.
const char *const kAllowed[16] = {
	"2345678", "1234", "0123", "01234", "0123", "0123456789abcdefghijklmnopqrstuv", "pnrv", "wns",
	"xpnrv", "xpnrv", "xpnrv", "xpnrv", "xwns", "xwns", "xwns", "atsmlxhi",
};

// The fingerprint in hex, as x1_fingerprints.txt has it.
std::string Fnv1a64(const std::string &s)
{
	char text[17];
	snprintf(text, sizeof(text), "%016llx", mods::rmg::Fingerprint(s));
	return text;
}

// "<recipe> <bytes> <fingerprint>": what an X1 name builds, as recorded in
// tools/rmg/x1_fingerprints.txt.
std::string FingerprintLine(const std::string &recipe)
{
	Template t;
	std::string error;
	if (!BuildRecipe(recipe, &t, &error))
		return recipe + " (not built: " + error + ")";
	return recipe + " " + std::to_string(t.xml.size()) + " " + Fnv1a64(t.xml);
}

// The names recorded once in tools/rmg/x1_fingerprints.txt: the ready
// templates, every letter at every place of the 2v2v2 name, and 200 random
// names.
std::vector<std::string> NamesToRecord(const std::vector<Case> &cases)
{
	std::vector<std::string> names;
	auto add = [&names](const std::string &n) {
		std::string canonical;
		if (!CanonicalRecipe(n, &canonical))
			return;
		for (const std::string &m : names)
			if (m == canonical)
				return;
		names.push_back(canonical);
	};
	for (const Case &k : cases) {
		std::string n;
		if (k.id.compare(0, 6, "ready-") == 0 && EncodeRecipe(k.choices, &n))
			add(n);
	}
	const std::string base = "OH-X1-321110nnxxxxxxxa";
	for (int pos = 0; pos < 16; pos++)
		for (const char *p = kAllowed[pos]; *p; ++p) {
			std::string n = base;
			n[6 + pos] = *p;
			add(n);
		}
	unsigned s = 20261025;
	for (int made = 0; made < 200;) {
		std::string n = "OH-X1-";
		for (int pos = 0; pos < 16; pos++) {
			s ^= s << 13;
			s ^= s >> 17;
			s ^= s << 5;
			n += kAllowed[pos][s % strlen(kAllowed[pos])];
		}
		std::string canonical;
		if (CanonicalRecipe(n, &canonical)) {
			add(n);
			++made;
		}
	}
	return names;
}

}  // namespace

int main(int argc, char **argv)
{
	if (argc < 3) {
		printf("usage: rmg_recipe_test <folder written by test_template_maker.js --expected> <ready templates folder>"
		       " [<x1_fingerprints.txt> | --write-fingerprints <new file>]\n");
		return 2;
	}
	const std::string dir = argv[1], ready_dir = argv[2];
	const std::string fingerprints = argc > 3 ? argv[3] : "";

	// --- Examples ------------------------------------------------------------
	{
		Choices c;  // the maker's defaults: 2v2v2, Normal, best size, underground
		Check(Encoded(c) == "OH-X1-321110nnxxxxxxxa", "2v2v2 Normal with underground is OH-X1-321110nnxxxxxxxa (the plan's example), not " + Encoded(c));
		c.border_towns = true;
		Check(Encoded(c) == "OH-X1-321111nnxxxxxxxa", "with towns between teams: " + Encoded(c));
		Choices d;
		d.teams = 2;
		d.per_team = 4;
		d.team_zones = 2;
		d.border_zones = 2;
		d.centre_zones = 2;
		d.loot = "rich";
		Check(Encoded(d) == "OH-X1-242220rnxxxxxxxa", "4v4 rich: " + Encoded(d));
		Choices e;
		e.teams = 8;
		e.per_team = 1;
		e.team_zones = 3;
		e.border_zones = 4;
		e.centre_zones = 3;
		e.border_towns = e.team_towns = true;
		e.ally_link = "open";
		e.start_mines = "woodore";
		e.underground = false;
		e.loot = "veryrich";
		e.guards = "strong";
		e.loot_by_distance[0] = "poor";
		e.loot_by_distance[1] = "normal";
		e.loot_by_distance[2] = "rich";
		e.loot_by_distance[3] = "veryrich";
		e.guards_by_distance[1] = "weak";
		e.guards_by_distance[2] = "normal";
		e.guards_by_distance[3] = "strong";
		e.size = "I";
		Check(Encoded(e) == "OH-X1-81343vvspnrvwnsI", "every choice at its last value: " + Encoded(e));
		e.size = "XL";
		e.underground = true;
		e.ally_link = "weak";
		Check(Encoded(e) == "OH-X1-81343bvspnrvwnsX", "XL is X, flags 1+2+8 = b: " + Encoded(e));
		Choices back;
		Check(DecodeRecipe("OH-X1-81343bvspnrvwnsX.xdb", &back) && Same(back, e), "and back");
		std::string problem;
		Check(mods::rmg::SelfCheck(&problem), "the self-check the DLL runs at start passes " + problem);
	}

	// --- Bad names are refused ----------------------------------------------
	{
		const std::string bad[] = {
			"", "OH-X1-", "OH-X1-321110nnxxxxxxx", "OH-X1-321110nnxxxxxxxaa", "OH-X2-321110nnxxxxxxxa",
			"OH-X1_321110nnxxxxxxxa", "OX-X1-321110nnxxxxxxxa", "OH-X1-321110nnxxxxxxxa.xd", "OH-X1-321110nnxxxxxxxa.xdbx",
			"OH-X1-321110nnxxxxxxxa.txt", " OH-X1-321110nnxxxxxxxa", "OH-X1-321110nnxxxxxxxa ", "OH-X1-321110nnxxxxxxxa.xdb.xdb",
			"OH-X1-331110nnxxxxxxxa" /* 9 players */, "OH-X1-341110nnxxxxxxxa" /* 12 */, "OH-X1-521110nnxxxxxxxa" /* 10 */,
			"OH-X1-921110nnxxxxxxxa", "OH-X1-121110nnxxxxxxxa", "OH-X1-301110nnxxxxxxxa", "OH-X1-324110nnxxxxxxxa",
			"OH-X1-321510nnxxxxxxxa", "OH-X1-321140nnxxxxxxxa", "OH-X1-32111wnnxxxxxxxa", "OH-X1-32111-nnxxxxxxxa",
			"OH-X1-321110xnxxxxxxxa", "OH-X1-321110nxxxxxxxxa", "OH-X1-321110nnwxxxxxxa", "OH-X1-321110nnxxxxpxxa",
			"OH-X1-321110nnxxxxvxxa", "OH-X1-321110nnxxxxxxxz", "OH-X1-321110nnxxxxxxxb", "OH-X1-321110nn\xC9xxxxxxa",
			std::string("OH-X1-321110nn\0xxxxxxa", 22), "RMG/Templates/OH-X1-321110nnxxxxxxxa.xdb", "OH-2v2v2-Lu-Normal.xdb",
		};
		for (const std::string &name : bad) {
			Choices c;
			std::string canonical, error;
			Template t;
			Check(!DecodeRecipe(name, &c) && !CanonicalRecipe(name, &canonical) && !BuildRecipe(name, &t, &error) &&
			          !error.empty(),
			      "\"" + name + "\" refused");
		}
		std::string canonical;
		Check(CanonicalRecipe("oh-x1-321110NNXXXXXXXA.XDB", &canonical) && canonical == "OH-X1-321110nnxxxxxxxa",
		      "any case is read; written back as OH-X1-321110nnxxxxxxxa, not " + canonical);
	}

	// --- Paths the game asks its file system for ---------------------------
	{
		const char *const served[] = {
			"RMG/Templates/OH-X1-321110nnxxxxxxxa.xdb", "rmg/templates/oh-x1-321110nnxxxxxxxa.xdb",
			"RMG\\Templates\\OH-X1-321110nnxxxxxxxa.xdb", "/RMG/Templates/OH-X1-321110NNXXXXXXXA.XDB",
			"\\rmg\\TEMPLATES/oh-X1-321110nnxxxxxxxA.Xdb",
		};
		for (const char *p : served) {
			std::string recipe;
			Check(mods::rmg::RecipeOfPath(p, &recipe) && recipe == "OH-X1-321110nnxxxxxxxa", std::string("path ") + p + " served");
		}
		const char *const others[] = {
			"RMG/Templates/OH-X1-321110nnxxxxxxxa", "RMG/Templates/OH-X1-321110nnxxxxxxxa.xdb.bak",
			"RMG/Templates/sub/OH-X1-321110nnxxxxxxxa.xdb", "Maps/OH-X1-321110nnxxxxxxxa.xdb",
			"RMG/OH-X1-321110nnxxxxxxxa.xdb", "data/RMG/Templates/OH-X1-321110nnxxxxxxxa.xdb",
			"RMG/Templates/OH-2v2v2-Lu-Normal.xdb", "RMG/Templates/OH-X1-331110nnxxxxxxxa.xdb", "OH-X1-321110nnxxxxxxxa.xdb",
			"RMG/Templates/ OH-X1-321110nnxxxxxxxa.xdb", "RMG_Templates/OH-X1-321110nnxxxxxxxa.xdb", "",
		};
		for (const char *p : others) {
			std::string recipe;
			Check(!mods::rmg::RecipeOfPath(p, &recipe), std::string("path \"") + p + "\" left to the game");
		}
	}

	// --- References, as maps and the multiplayer game info name templates ----
	{
		std::string recipe;
		Check(mods::rmg::RecipeOfReference("/RMG/Templates/OH-X1-321110nnxxxxxxxa.xdb#xpointer(/RMGTemplate)", &recipe) &&
		          recipe == "OH-X1-321110nnxxxxxxxa",
		      "a map's template reference gives its recipe");
		Check(mods::rmg::RecipeOfReference("/rmg/templates/oh-x1-321110nnxxxxxxxa.xdb", &recipe), "a reference without #xpointer");
		Check(!mods::rmg::RecipeOfReference("/RMG/Templates/OH-2v2v2-Lu-Normal.xdb#xpointer(/RMGTemplate)", &recipe) &&
		          !mods::rmg::RecipeOfReference("#xpointer(/RMGTemplate)", &recipe) && !mods::rmg::RecipeOfReference("", &recipe),
		      "other references are not recipes");
	}

	// --- Template links in game memory (rmg_join.cpp) ------------------------
	// A link is an NDb pointer; the object's database id sits at +0xc and starts
	// with the path (an nstl::string: begin, end, capacity), as in sRMGProps.
	{
		static char path[] = "/RMG/Templates/OH-X1-321110nnxxxxxxxa.xdb#xpointer(/RMGTemplate)";
		struct {
			unsigned char head[0xc];
			const char *begin, *end, *cap;  // the database id: path...
			unsigned char key_made, pad[3];
			unsigned hash;
			const char *key_begin, *key_end, *key_cap;  // ...and key
		} object = {};
		object.begin = path;
		object.end = path + sizeof(path) - 1;
		object.cap = object.end + 1;
		const void *link[2] = {&object, nullptr};
		std::string reference, recipe;
		Check(mods::TemplateLinkReference(link, &reference) && reference == path &&
		          mods::rmg::RecipeOfReference(reference, &recipe) && recipe == "OH-X1-321110nnxxxxxxxa",
		      "a template link in memory gives its path and recipe");
		object.begin = object.end = nullptr;
		object.key_begin = path;
		object.key_end = path + 10;
		Check(mods::TemplateLinkReference(link, &reference) && reference == std::string(path, 10),
		      "an empty path: the key is read instead");
		object.key_begin = object.key_end = nullptr;
		Check(!mods::TemplateLinkReference(link, &reference), "an empty database id: nothing");
		const void *empty[2] = {nullptr, nullptr};
		const void *bad[2] = {reinterpret_cast<const void *>(16), nullptr};
		Check(!mods::TemplateLinkReference(empty, &reference) && !mods::TemplateLinkReference(bad, &reference) &&
		          !mods::TemplateLinkReference(nullptr, &reference),
		      "an empty or broken link: nothing, no crash");
		Check(mods::kAdvMapDescRmgProps == 0x254 && 0x108 + mods::kRmgPropsTemplateLink == 0x130,
		      "the template link: map description +0x27c, game info +0x130");
	}

	// --- Teams of a recipe map, by player number -----------------------------
	{
		Choices c;  // 2v2v2
		const int want[] = {-1, 0, 0, 1, 1, 2, 2, -1};  // players 0 (none) to 7
		bool ok = true;
		for (int p = 0; p < 8; p++)
			ok = ok && mods::rmg::RecipeTeamOf(c, p) == want[p];
		Check(ok, "2v2v2: players 1+2, 3+4, 5+6");
		c.teams = 2;
		c.per_team = 4;
		Check(mods::rmg::RecipeTeamOf(c, 4) == 0 && mods::rmg::RecipeTeamOf(c, 5) == 1 && mods::rmg::RecipeTeamOf(c, 8) == 1 &&
		          mods::rmg::RecipeTeamOf(c, 9) == -1,
		      "4v4: players 1-4, 5-8");
		c.teams = 6;
		c.per_team = 1;
		Check(mods::rmg::RecipeTeamOf(c, 1) == 0 && mods::rmg::RecipeTeamOf(c, 6) == 5 && mods::rmg::RecipeTeamOf(c, 7) == -1,
		      "free-for-all: one team per player");
	}

	// --- The list of [rmg] recipes= ------------------------------------------
	{
		struct {
			const char *text;
			const char *names;  // as written back, joined by ","
			int bad;
		} lists[] = {
			{"", "", 0},
			{"OH-X1-321110nnxxxxxxxa", "OH-X1-321110nnxxxxxxxa", 0},
			{" OH-X1-321110nnxxxxxxxa , oh-x1-611111NNxxxxxxxa.xdb ; a comment, OH-X1-221110nnxxxxxxxa",
			 "OH-X1-321110nnxxxxxxxa,OH-X1-611111nnxxxxxxxa", 0},
			{"OH-X1-321110nnxxxxxxxa,,OH-X1-321110NNXXXXXXXA,", "OH-X1-321110nnxxxxxxxa", 0},
			{"junk, OH-X1-331110nnxxxxxxxa,OH-2v2v2-Lu-Normal.xdb", "", 3},
			{"OH-X1-321110nn xxxxxxxa", "OH-X1-321110nnxxxxxxxa", 0},  // spaces are ignored, even inside
		};
		for (const auto &l : lists) {
			std::vector<std::string> names, bad;
			mods::rmg::ParseRecipeList(l.text, &names, &bad);
			std::string joined;
			for (const std::string &n : names)
				joined += (joined.empty() ? "" : ",") + n;
			Check(joined == l.names && static_cast<int>(bad.size()) == l.bad,
			      std::string("recipes=") + l.text + " gives " + joined + " and " + std::to_string(bad.size()) + " bad");
		}
	}

	// --- Every character at every place --------------------------------------
	{
		int tried = 0, taken = 0;
		const std::string base = "OH-X1-321110nnxxxxxxxa";
		for (int pos = 0; pos < 16; pos++) {
			for (int ch = 0x20; ch < 0x7f; ch++) {
				std::string name = base;
				name[6 + pos] = static_cast<char>(ch);
				const char low = static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
				bool allowed = strchr(kAllowed[pos], low) != nullptr && low != 0;
				int teams = 3, per_team = 2;
				if (allowed && pos == 0)
					teams = low - '0';
				if (allowed && pos == 1)
					per_team = low - '0';
				allowed = allowed && teams * per_team <= 8;
				Choices c;
				std::string canonical;
				const bool ok = DecodeRecipe(name, &c) && CanonicalRecipe(name, &canonical);
				++tried;
				taken += ok;
				if (!Check(ok == allowed, "place " + std::to_string(pos + 1) + ", '" + std::string(1, static_cast<char>(ch)) +
				                              "': " + (allowed ? "refused" : "taken")))
					continue;
				if (ok)
					Check(Lower(canonical) == Lower(name) && Encoded(c) == canonical,
					      "place " + std::to_string(pos + 1) + " '" + std::string(1, static_cast<char>(ch)) + "' written back as " + canonical);
			}
		}
		printf("recipe letters: %d names tried, %d taken\n", tried, taken);
	}

	// --- Choices an X1 name cannot hold are refused --------------------------
	{
		struct {
			const char *what;
			void (*change)(Choices *);
		} refused[] = {
			{"a typed name", [](Choices *c) { c->name = "Mine"; }},
			{"guards for the start zones", [](Choices *c) { c->guards_by_distance[0] = "weak"; }},
			{"9 teams", [](Choices *c) { c->teams = 9; c->per_team = 1; }},
			{"1 team", [](Choices *c) { c->teams = 1; }},
			{"0 players per team", [](Choices *c) { c->per_team = 0; }},
			{"5 players per team", [](Choices *c) { c->teams = 2; c->per_team = 5; }},
			{"9 players", [](Choices *c) { c->per_team = 3; }},
			{"4 team zones", [](Choices *c) { c->team_zones = 4; }},
			{"-1 team zones", [](Choices *c) { c->team_zones = -1; }},
			{"5 border zones", [](Choices *c) { c->border_zones = 5; }},
			{"4 centre zones", [](Choices *c) { c->centre_zones = 4; }},
			{"loot gold", [](Choices *c) { c->loot = "gold"; }},
			{"guards empty", [](Choices *c) { c->guards = ""; }},
			{"loot tier lots", [](Choices *c) { c->loot_by_distance[2] = "lots"; }},
			{"size xl", [](Choices *c) { c->size = "xl"; }},
			{"ally link none (the maker takes it as weak)", [](Choices *c) { c->ally_link = "none"; }},
			{"start mines some (the maker takes it as all)", [](Choices *c) { c->start_mines = "some"; }},
		};
		for (const auto &r : refused) {
			Choices c;
			r.change(&c);
			std::string name;
			Check(!EncodeRecipe(c, &name), std::string("refused: ") + r.what);
		}
	}

	// --- Round trips on the parity cases ------------------------------------
	std::vector<Case> cases;
	if (!rmgtest::LoadCases(dir, &cases)) {
		printf("FAIL: no %s/cases.txt (run node tools/rmg/test_template_maker.js --expected %s)\n", dir.c_str(), dir.c_str());
		return 1;
	}
	int trips = 0, served = 0, not_encodable = 0;
	for (const Case &k : cases) {
		if (k.refused || !k.unknown.empty())
			continue;
		std::string name;
		const bool holdable = k.choices.name.empty() && k.choices.guards_by_distance[0].empty();
		if (!Check(EncodeRecipe(k.choices, &name) == holdable, k.id + ": " + (holdable ? "not encoded" : "encoded")))
			continue;
		if (!holdable) {
			++not_encodable;
			continue;
		}
		Choices back, low, high;
		std::string canonical;
		Check(name.size() == 22 && DecodeRecipe(name, &back) && Same(back, k.choices), k.id + ": " + name + " gives back the choices");
		Check(DecodeRecipe(Lower(name) + ".xdb", &low) && Same(low, k.choices) && DecodeRecipe(Upper(name) + ".XDB", &high) &&
		          Same(high, k.choices),
		      k.id + ": " + name + " in small and capital letters");
		Check(CanonicalRecipe(Lower(name), &canonical) && canonical == name, k.id + ": " + name + " written back the same");
		trips++;
		std::string want, error;
		Template t;
		if (!Check(rmgtest::ReadFile(dir + "/" + k.file, &want), k.id + ": read " + k.file))
			continue;
		served += Check(BuildRecipe(name, &t, &error) && t.xml == Starred(want, k.name) && t.name == k.name + "*" &&
		                    t.file_name == name + ".xdb",
		                k.id + ": " + name + " builds JavaScript's template, named " + k.name + "*");
	}
	printf("recipe round trips: %d choices -> name -> the same choices; %d recipe builds equal to JavaScript's "
	       "(with \"*\" after the name); %d choices an X1 name cannot hold\n",
	       trips, served, not_encodable);
	Check(trips >= 300, "at least 300 round trips");

	// --- The ready templates as recipes -------------------------------------
	int ready = 0, ready_equal = 0;
	std::vector<std::string> seen;
	printf("ready templates and their recipe names:\n");
	for (const Case &k : cases) {
		if (k.id.compare(0, 6, "ready-") != 0)
			continue;
		++ready;
		std::string name, shipped, error;
		Choices back;
		Template plain, t;
		if (!Check(EncodeRecipe(k.choices, &name), k.id + ": has a recipe name"))
			continue;
		if (!Check(rmgtest::ReadFile(ready_dir + "/" + k.file_name, &shipped), k.id + ": read " + ready_dir + "/" + k.file_name))
			continue;
		bool ok = Check(DecodeRecipe(name, &back) && MakeTemplate(back, &plain, &error) && plain.xml == shipped,
		                k.id + ": the maker builds " + k.file_name + " from " + name + ", byte for byte");
		ok = Check(BuildRecipe(name, &t, &error) && t.xml == Starred(shipped, k.name), k.id + ": " + name +
		                                                                                  " serves " + k.file_name +
		                                                                                  " with \"*\" after the name") &&
		     ok;
		for (const std::string &s : seen)
			ok = Check(s != name, k.id + ": " + name + " only once") && ok;
		seen.push_back(name);
		ready_equal += ok;
		printf("  %-30s %s.xdb, listed as %s\n", k.file_name.c_str(), name.c_str(), t.name.c_str());
	}
	Check(ready >= 1 && ready_equal == ready, "every ready template builds from its recipe name");
	printf("ready templates: %d of %d built from their recipe names (the same bytes; the served file's name has a \"*\")\n",
	       ready_equal, ready);

	// --- X1 names build what they built when they were recorded --------------
	// Players build a template from its name on their own PCs, and saved maps
	// keep the name: what an X1 name builds must never change. The file was
	// written once; a difference here means the maker changed what old names
	// build. Undo that, or give the change a new version (X2).
	if (fingerprints == "--write-fingerprints") {
		const std::string file = argc > 4 ? argv[4] : "";
		FILE *f = nullptr;
		if (file.empty() || (fopen_s(&f, file.c_str(), "rb") == 0 && f)) {
			if (f)
				fclose(f);
			printf("FAIL: --write-fingerprints needs a file that does not exist yet (X1 fingerprints are written once)\n");
			return 1;
		}
		if (fopen_s(&f, file.c_str(), "wb") || !f) {
			printf("FAIL: cannot write %s\n", file.c_str());
			return 1;
		}
		fprintf(f, "# What X1 recipe names build: the name, the file's size in bytes, and its FNV-1a 64-bit\n"
		           "# fingerprint. Checked by src/hook/test/rmg_recipe_test.cpp (build.ps1 -Test).\n"
		           "# Written once. NEVER change or remove a line to make the test pass:\n"
		           "# a difference means the maker now builds something else for an old name. Undo that\n"
		           "# change, or give it a new recipe version (X2). Adding lines is fine.\n");
		const std::vector<std::string> names = NamesToRecord(cases);
		for (const std::string &n : names)
			fprintf(f, "%s\n", FingerprintLine(n).c_str());
		fclose(f);
		printf("X1 fingerprints: %u names written to %s\n", static_cast<unsigned>(names.size()), file.c_str());
	} else if (!fingerprints.empty()) {
		std::string text;
		int names = 0, same = 0;
		if (Check(rmgtest::ReadFile(fingerprints, &text), "read " + fingerprints)) {
			for (std::string line : rmgtest::Split(text, '\n')) {
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				if (line.empty() || line[0] == '#')
					continue;
				++names;
				const std::string recipe = line.substr(0, line.find(' '));
				same += Check(FingerprintLine(recipe) == line, "X1 drift: " + recipe + " now builds " + FingerprintLine(recipe) +
				                                                   ", recorded " + line);
			}
		}
		Check(names >= 100, "at least 100 recorded X1 names");
		printf("X1 fingerprints: %d of %d recorded names build what they built when recorded\n", same, names);
	}

	printf(g_failures ? "rmg recipe test: %d of %d checks FAILED\n" : "rmg recipe test: all %d checks passed\n",
	       g_failures ? g_failures : g_checks, g_checks);
	return g_failures ? 1 : 0;
}
