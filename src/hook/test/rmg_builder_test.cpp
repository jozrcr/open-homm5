// Test of the template builder's data (src/mods/rmg_builder_data.cpp), no game
// needed. Run by build.ps1 -Test:
//   rmg_builder_test.exe <src/mods/rmg/templates>
// It checks that:
//   - the presets are the ready templates: each recipe builds its ready file's
//     bytes (with the name), and every ready file has a preset;
//   - whatever the panel can offer has a recipe name (so "Use" always works),
//     and the maker's own check (CheckChoices) agrees with MakeTemplate;
//   - the zone map's positions are the web page's (numbers from Node);
//   - the French words of the maker's warnings, the game's text format, and
//     that a UI file we do not know is left alone.
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "../../mods/rmg_builder_data.h"
#include "../../mods/rmg_maker.h"
#include "../../mods/rmg_recipe.h"

namespace {

int g_failed = 0;
int g_checks = 0;

void Check(bool ok, const char *what)
{
	++g_checks;
	if (!ok) {
		++g_failed;
		printf("FAIL: %s\n", what);
	}
}

bool ReadFileBytes(const std::string &path, std::string *out)
{
	FILE *f = nullptr;
	if (fopen_s(&f, path.c_str(), "rb") || !f)
		return false;
	char buf[65536];
	size_t n;
	out->clear();
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
		out->append(buf, n);
	fclose(f);
	return true;
}

using namespace mods::rmg;
namespace data = mods::rmg::builder;

void TestPresets(const std::string &dir)
{
	for (int i = 0; i < data::kPresetCount; ++i) {
		const data::Preset &p = data::kPresets[i];
		Choices c;
		char what[256];
		_snprintf_s(what, sizeof(what), _TRUNCATE, "preset %s decodes", p.recipe);
		Check(DecodeRecipe(p.recipe, &c), what);
		Template t;
		std::string error;
		_snprintf_s(what, sizeof(what), _TRUNCATE, "preset %s builds %s", p.recipe, p.name);
		Check(MakeTemplate(c, &t, &error) && t.name == p.name, what);
		std::string file;
		_snprintf_s(what, sizeof(what), _TRUNCATE, "preset %s = the ready file %s.xdb, byte for byte", p.recipe, p.name);
		Check(ReadFileBytes(dir + "\\" + p.name + ".xdb", &file) && file == t.xml, what);
		_snprintf_s(what, sizeof(what), _TRUNCATE, "PresetOf(%s) = %d", p.recipe, i);
		Check(data::PresetOf(c) == i, what);
	}
	// every ready file has a preset
	WIN32_FIND_DATAA fd;
	HANDLE find = FindFirstFileA((dir + "\\*.xdb").c_str(), &fd);
	int files = 0;
	if (find != INVALID_HANDLE_VALUE) {
		do {
			++files;
			std::string name(fd.cFileName);
			name = name.substr(0, name.size() - 4);
			bool found = false;
			for (int i = 0; i < data::kPresetCount; ++i)
				found = found || name == data::kPresets[i].name;
			char what[256];
			_snprintf_s(what, sizeof(what), _TRUNCATE, "the ready file %s has a preset", fd.cFileName);
			Check(found, what);
		} while (FindNextFileA(find, &fd));
		FindClose(find);
	}
	Check(files == data::kPresetCount, "as many presets as ready files");
	Choices custom;
	custom.loot = "poor";
	custom.teams = 7;
	custom.per_team = 1;
	Check(data::PresetOf(custom) == -1, "other choices: no preset");
}

// What the panel offers: its steppers' ranges, its lists, the per-distance
// table (no guards for the start zones), every size and underground.
void TestPanelChoices()
{
	const char *const sizes[] = {"auto", "T", "S", "M", "L", "XL", "H", "I"};
	const char *const allies[] = {"weak", "open"};
	const char *const mines[] = {"all", "woodore"};
	unsigned seed = 12345;
	auto next = [&seed](int n) {
		seed ^= seed << 13;
		seed ^= seed >> 17;
		seed ^= seed << 5;
		return static_cast<int>(seed % static_cast<unsigned>(n));
	};
	int offered = 0, refused = 0, agree = 0;
	for (int teams = 1; teams <= 9; ++teams)
		for (int per = 0; per <= 5; ++per)
			for (int r = 0; r < 12; ++r) {
				Choices c;
				c.teams = teams;
				c.per_team = per;
				c.team_zones = next(4);
				c.border_zones = next(5);
				c.centre_zones = next(4);
				c.border_towns = next(2) != 0;
				c.team_towns = next(2) != 0;
				c.ally_link = allies[next(2)];
				c.start_mines = mines[next(2)];
				c.loot = data::kLootKeys[next(4)];
				c.guards = data::kGuardKeys[next(3)];
				for (int d = 0; d < 4; ++d) {
					const int l = next(5), g = next(4);
					c.loot_by_distance[d] = l ? data::kLootKeys[l - 1] : "";
					c.guards_by_distance[d] = d > 0 && g ? data::kGuardKeys[g - 1] : "";
				}
				c.size = sizes[next(8)];
				c.underground = next(2) != 0;
				std::string check_error, make_error;
				const bool checked = CheckChoices(c, &check_error);
				Template t;
				const bool made = MakeTemplate(c, &t, &make_error);
				agree += checked == made && check_error == make_error;
				const bool in_panel = teams >= 2 && teams <= 8 && per >= 1 && per <= 4 && teams * per <= 8;
				if (in_panel) {
					++offered;
					std::string recipe;
					Choices back;
					if (!made || !EncodeRecipe(c, &recipe) || !DecodeRecipe(recipe, &back)) {
						printf("FAIL: panel choices %d x %d have no recipe (%s)\n", teams, per, make_error.c_str());
						++g_failed;
					}
				}
				else
					refused += !made;
			}
	Check(agree == 9 * 6 * 12, "CheckChoices agrees with MakeTemplate (same answer, same words)");
	Check(offered == 144 && refused > 300, "the panel's choices and the refused ones were both tried");
	printf("ok  : %d panel choices with recipe names; %d refused ones; CheckChoices agrees on %d\n", offered, refused, agree);
}

void TestPositions()
{
	struct Case {
		int teams, per, team_zones, border, centre;
		const char *expected;  // positions() of template-maker.html, from Node
	};
	const Case cases[] = {
		{3, 2, 1, 1, 1,
		 "240.393,54.174 319.607,54.174 460.734,298.612 421.126,367.214 138.874,367.214 99.266,298.612 280.000,129.800 "
		 "375.436,295.100 184.564,295.100 441.254,146.900 280.000,426.200 118.746,146.900 280.000,240.000"},
		{2, 4, 2, 2, 2,
		 "240.393,54.174 266.711,50.465 293.289,50.465 319.607,54.174 319.607,425.826 293.289,429.535 266.711,429.535 "
		 "240.393,425.826 263.859,130.989 296.141,130.989 296.141,349.011 263.859,349.011 433.902,145.689 433.902,334.311 "
		 "126.098,334.311 126.098,145.689 304.042,215.958 255.958,264.042"},
		{5, 1, 3, 3, 3,
		 "280.000,50.000 460.701,181.287 391.679,393.713 168.321,393.713 99.299,181.287 263.859,130.989 280.000,129.800 "
		 "296.141,130.989 378.688,190.962 384.806,205.946 388.664,221.665 357.134,318.705 344.774,329.154 331.017,337.680 "
		 "228.983,337.680 215.226,329.154 202.866,318.705 171.336,221.665 175.194,205.946 181.312,190.962 353.949,69.114 "
		 "389.446,89.361 419.671,116.864 465.374,257.523 457.087,297.539 440.270,334.784 320.618,421.716 280.000,426.200 "
		 "239.382,421.716 119.730,334.784 102.913,297.539 94.626,257.523 140.329,116.864 170.554,89.361 206.051,69.114 "
		 "304.042,215.958 288.800,272.841 247.159,231.200"},
	};
	for (const Case &k : cases) {
		Choices c;
		c.teams = k.teams;
		c.per_team = k.per;
		c.team_zones = k.team_zones;
		c.border_zones = k.border;
		c.centre_zones = k.centre;
		Template t;
		std::string error;
		MakeTemplate(c, &t, &error);
		const std::vector<data::Point> p = data::ZonePositions(t, c);
		std::vector<data::Point> want;
		for (const char *s = k.expected; *s;) {
			float x = 0, y = 0;
			int used = 0;
			if (sscanf_s(s, "%f,%f%n", &x, &y, &used) != 2)
				break;
			want.push_back({x, y});
			s += used;
			while (*s == ' ')
				++s;
		}
		bool same = p.size() == want.size();
		for (size_t i = 0; same && i < p.size(); ++i)
			same = fabs(p[i].x - want[i].x) < 0.01 && fabs(p[i].y - want[i].y) < 0.01;
		char what[128];
		_snprintf_s(what, sizeof(what), _TRUNCATE, "zone positions = the page's (%d teams of %d, %d zones)", k.teams, k.per,
		            static_cast<int>(want.size()));
		Check(same, what);
	}
}

void TestWords()
{
	const std::string small = "Zones are small (3600 tiles each): pick a bigger map or fewer zones.";
	const std::string big = "Zones are very big (9000 tiles each): the generator may leave them empty; pick a smaller map or more zones.";
	const std::string rich = "Some zones hold more treasure (340000) than any shipped template (252000); not tested in the game yet.";
	const std::string players = "the game allows at most 8 players (9 asked)";
	Check(data::Localize(small, data::kEnglish) == small, "English warnings stay as the maker says them");
	Check(data::Localize(small, data::kFrench).find("3600 cases") != std::string::npos, "French: small zones");
	Check(data::Localize(big, data::kFrench).find("9000 cases") != std::string::npos, "French: big zones");
	Check(data::Localize(rich, data::kFrench).find("340000") != std::string::npos &&
	          data::Localize(rich, data::kFrench).find("252000") != std::string::npos,
	      "French: treasure");
	Check(data::Localize(players, data::kFrench).find("(9 demand") != std::string::npos, "French: 8 players at most");
	Check(data::Localize("something else", data::kFrench) == "something else", "an unknown text stays as it is");
	for (int language = 0; language < 2; ++language) {
		const data::Words &w = data::WordsFor(language);
		const char *const *all = reinterpret_cast<const char *const *>(&w);
		bool filled = true;
		for (size_t i = 0; i < sizeof(data::Words) / sizeof(const char *); ++i)
			filled = filled && all[i] && all[i][0];
		Check(filled, language ? "every French word is there" : "every English word is there");
	}
	const std::string text = data::GameText("Mod\xc3\xa8les...");
	Check(text == std::string("\xff\xfeM\0o\0d\0\xe8\0l\0e\0s\0.\0.\0.\0", 22), "the game's text format (UTF-16 with its mark)");
	std::string out = "untouched";
	Check(!data::MakeMainPanel("<WindowSimpleShared/>", &out) && out == "untouched", "a main panel we do not know is left alone");
	Check(!data::MakeButton("<WindowMSButton/>", &out) && !data::MakeReaction("<UISRunReaction/>", &out),
	      "Save button files we do not know are left alone");
}

}  // namespace

int main(int argc, char **argv)
{
	if (argc < 2) {
		printf("usage: rmg_builder_test <src/mods/rmg/templates>\n");
		return 2;
	}
	TestPresets(argv[1]);
	TestPanelChoices();
	TestPositions();
	TestWords();
	if (g_failed) {
		printf("rmg builder: %d of %d checks FAILED\n", g_failed, g_checks);
		return 1;
	}
	printf("rmg builder: all %d checks passed\n", g_checks);
	return 0;
}
