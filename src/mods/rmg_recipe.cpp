// Recipe names (see rmg_recipe.h). The whole format is the two tables below,
// written so that the page (JavaScript) can copy them as they are: choices by
// their template_maker.js names, values as the maker's words.
#include "rmg_recipe.h"

#include <stdlib.h>
#include <string.h>

#include <algorithm>
namespace mods {
namespace rmg {

const char kRecipePrefix[] = "OH-X1-";

namespace {

// The X1 name: "OH-X1-", then one character per row, in this order. In a row,
// the n-th letter stands for the n-th value (values are separated by "|"; an
// empty value is "the same as the overall level"). The flags row is a number
// from 0 to 31 written with the row's letters (base 32), one bit per yes/no
// choice of kX1Flags.
//
// NEVER CHANGE THESE TABLES: every PC of a game builds the template from its
// name, and saved maps keep the name. A new choice or value needs X2.
struct Row {
	const char *choice;   // template_maker.js's name for it
	const char *letters;  // written as here; read in any case
	const char *values;   // nullptr: the flags row
};
const Row kX1[16] = {
	{"teams", "2345678", "2|3|4|5|6|7|8"},
	{"perTeam", "1234", "1|2|3|4"},
	{"teamZones", "0123", "0|1|2|3"},
	{"borderZones", "01234", "0|1|2|3|4"},
	{"centreZones", "0123", "0|1|2|3"},
	{"flags", "0123456789abcdefghijklmnopqrstuv", nullptr},
	{"loot", "pnrv", "poor|normal|rich|veryrich"},
	{"guards", "wns", "weak|normal|strong"},
	{"lootByDistance.0", "xpnrv", "|poor|normal|rich|veryrich"},
	{"lootByDistance.1", "xpnrv", "|poor|normal|rich|veryrich"},
	{"lootByDistance.2", "xpnrv", "|poor|normal|rich|veryrich"},
	{"lootByDistance.3", "xpnrv", "|poor|normal|rich|veryrich"},
	{"guardsByDistance.1", "xwns", "|weak|normal|strong"},
	{"guardsByDistance.2", "xwns", "|weak|normal|strong"},
	{"guardsByDistance.3", "xwns", "|weak|normal|strong"},
	{"size", "aTSMLXHI", "auto|T|S|M|L|XL|H|I"},
};
// The flags row: bit n (value 1, 2, 4, 8, 16) is set when choice n has its
// "on" value; otherwise it must have its "off" value.
struct Flag {
	const char *choice;
	const char *on;
	const char *off;
};
const Flag kX1Flags[5] = {
	{"borderTowns", "true", "false"},
	{"teamTowns", "true", "false"},
	{"allyLink", "open", "weak"},
	{"startMines", "woodore", "all"},
	{"underground", "false", "true"},
};
// Choices outside the tables keep the maker's defaults: no typed name, and
// guardsByDistance.0 "" (the page has no guards for the start zones). One
// more rule: teams x perTeam is at most 8, as the maker says.

char Lower(char c)
{
	return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool SameText(const std::string &a, size_t at, const char *b)
{
	const size_t n = strlen(b);
	if (a.size() < at + n)
		return false;
	for (size_t i = 0; i < n; i++)
		if (Lower(a[at + i]) != Lower(b[i]))
			return false;
	return true;
}

// The n-th value of a row, or false.
bool ValueAt(const char *values, int n, std::string *out)
{
	const char *p = values;
	for (int i = 0; i < n; i++) {
		p = strchr(p, '|');
		if (!p)
			return false;
		++p;
	}
	const char *end = strchr(p, '|');
	out->assign(p, end ? end : p + strlen(p));
	return true;
}

int IndexOfValue(const char *values, const std::string &v)
{
	std::string s;
	for (int i = 0; ValueAt(values, i, &s); i++)
		if (s == v)
			return i;
	return -1;
}

// A choice as the maker's words or numbers, by its JavaScript name.
std::string Get(const Choices &c, const std::string &choice)
{
	if (choice == "teams")
		return std::to_string(c.teams);
	if (choice == "perTeam")
		return std::to_string(c.per_team);
	if (choice == "teamZones")
		return std::to_string(c.team_zones);
	if (choice == "borderZones")
		return std::to_string(c.border_zones);
	if (choice == "centreZones")
		return std::to_string(c.centre_zones);
	if (choice == "borderTowns")
		return c.border_towns ? "true" : "false";
	if (choice == "teamTowns")
		return c.team_towns ? "true" : "false";
	if (choice == "allyLink")
		return c.ally_link;
	if (choice == "startMines")
		return c.start_mines;
	if (choice == "underground")
		return c.underground ? "true" : "false";
	if (choice == "loot")
		return c.loot;
	if (choice == "guards")
		return c.guards;
	if (choice == "size")
		return c.size;
	if (choice.size() == 16 && choice.compare(0, 15, "lootByDistance.") == 0)
		return c.loot_by_distance[(choice[15] - '0') & 3];
	if (choice.size() == 18 && choice.compare(0, 17, "guardsByDistance.") == 0)
		return c.guards_by_distance[(choice[17] - '0') & 3];
	return std::string();
}

void Set(Choices *c, const std::string &choice, const std::string &v)
{
	if (choice == "teams")
		c->teams = atoi(v.c_str());
	else if (choice == "perTeam")
		c->per_team = atoi(v.c_str());
	else if (choice == "teamZones")
		c->team_zones = atoi(v.c_str());
	else if (choice == "borderZones")
		c->border_zones = atoi(v.c_str());
	else if (choice == "centreZones")
		c->centre_zones = atoi(v.c_str());
	else if (choice == "borderTowns")
		c->border_towns = v == "true";
	else if (choice == "teamTowns")
		c->team_towns = v == "true";
	else if (choice == "allyLink")
		c->ally_link = v;
	else if (choice == "startMines")
		c->start_mines = v;
	else if (choice == "underground")
		c->underground = v == "true";
	else if (choice == "loot")
		c->loot = v;
	else if (choice == "guards")
		c->guards = v;
	else if (choice == "size")
		c->size = v;
	else if (choice.size() == 16 && choice.compare(0, 15, "lootByDistance.") == 0)
		c->loot_by_distance[(choice[15] - '0') & 3] = v;
	else if (choice.size() == 18 && choice.compare(0, 17, "guardsByDistance.") == 0)
		c->guards_by_distance[(choice[17] - '0') & 3] = v;
}

}  // namespace

bool EncodeRecipe(const Choices &c, std::string *name)
{
	if (!c.name.empty() || !c.guards_by_distance[0].empty())
		return false;
	std::string s = kRecipePrefix;
	for (const Row &row : kX1) {
		int n = 0;
		if (!row.values) {
			for (int bit = 0; bit < 5; bit++) {
				const std::string v = Get(c, kX1Flags[bit].choice);
				if (v == kX1Flags[bit].on)
					n |= 1 << bit;
				else if (v != kX1Flags[bit].off)
					return false;
			}
		} else {
			n = IndexOfValue(row.values, Get(c, row.choice));
			if (n < 0)
				return false;
		}
		s += row.letters[n];
	}
	if (c.teams * c.per_team > 8)
		return false;
	*name = s;
	return true;
}

bool DecodeRecipe(const std::string &name, Choices *out)
{
	size_t n = name.size();
	if (n == kRecipeLength + 4 && SameText(name, kRecipeLength, ".xdb"))
		n = kRecipeLength;
	if (n != kRecipeLength || !SameText(name, 0, kRecipePrefix))
		return false;
	Choices c;  // the maker's defaults for what the name does not hold
	for (size_t i = 0; i < 16; i++) {
		const Row &row = kX1[i];
		const char *letter = nullptr;
		for (const char *p = row.letters; *p && !letter; ++p)
			if (Lower(*p) == Lower(name[6 + i]))
				letter = p;
		if (!letter)
			return false;
		const int at = static_cast<int>(letter - row.letters);
		if (!row.values) {
			for (int bit = 0; bit < 5; bit++)
				Set(&c, kX1Flags[bit].choice, at & (1 << bit) ? kX1Flags[bit].on : kX1Flags[bit].off);
		} else {
			std::string v;
			if (!ValueAt(row.values, at, &v))
				return false;
			Set(&c, row.choice, v);
		}
	}
	if (c.teams * c.per_team > 8)
		return false;
	*out = c;
	return true;
}

bool CanonicalRecipe(const std::string &name, std::string *canonical)
{
	Choices c;
	return DecodeRecipe(name, &c) && EncodeRecipe(c, canonical);
}

bool RecipeOfPath(const std::string &path, std::string *recipe)
{
	static const char kDir[] = "rmg/templates/";
	const size_t dir = sizeof(kDir) - 1;
	size_t at = 0;
	while (at < path.size() && (path[at] == '/' || path[at] == '\\'))
		++at;
	if (path.size() != at + dir + kRecipeLength + 4 || !SameText(path, path.size() - 4, ".xdb"))
		return false;
	for (size_t i = 0; i < dir; i++) {
		const char c = path[at + i] == '\\' ? '/' : Lower(path[at + i]);
		if (c != kDir[i])
			return false;
	}
	return CanonicalRecipe(path.substr(at + dir), recipe);
}

bool RecipeOfReference(const std::string &reference, std::string *recipe)
{
	return RecipeOfPath(reference.substr(0, reference.find('#')), recipe);
}

int RecipeTeamOf(const Choices &c, int player)
{
	if (c.per_team < 1 || player < 1 || player > c.teams * c.per_team)
		return -1;
	return (player - 1) / c.per_team;
}

void ParseRecipeList(const std::string &text, std::vector<std::string> *names, std::vector<std::string> *bad)
{
	std::string item;
	for (size_t i = 0;; ++i) {
		const char c = i < text.size() ? text[i] : 0;
		if (c && c != ',' && c != ';') {
			if (c != ' ' && c != '\t')
				item += c;
			continue;
		}
		std::string recipe;
		if (!item.empty() && !CanonicalRecipe(item, &recipe))
			bad->push_back(item);
		else if (!item.empty() && std::find(names->begin(), names->end(), recipe) == names->end())
			names->push_back(recipe);
		item.clear();
		if (!c || c == ';')
			break;
	}
}

unsigned long long Fingerprint(const std::string &bytes)
{
	unsigned long long h = 0xcbf29ce484222325ULL;
	for (unsigned char c : bytes) {
		h ^= c;
		h *= 0x100000001b3ULL;
	}
	return h;
}

bool SelfCheck(std::string *problem)
{
	// Lines of tools/rmg/x1_fingerprints.txt: a ready template, and two that
	// use every kind of choice (tiers, a fixed size, the yes/no choices).
	static const struct {
		const char *recipe;
		size_t size;
		unsigned long long fingerprint;
	} kKnown[] = {
		{"OH-X1-321110nnxxxxxxxa", 23121, 0x672b3d0f95ebed92ULL},
		{"OH-X1-24301arsnpnnnnnS", 26891, 0x6f7352e8f148138bULL},
		{"OH-X1-61332dnwnvnvnswT", 75770, 0x49ddd03d7964f071ULL},
	};
	for (const auto &k : kKnown) {
		Template t;
		std::string error;
		if (!BuildRecipe(k.recipe, &t, &error) || t.xml.size() != k.size || Fingerprint(t.xml) != k.fingerprint) {
			*problem = std::string(k.recipe) + " built " + std::to_string(t.xml.size()) + " bytes, not the recorded ones";
			return false;
		}
	}
	return true;
}

bool BuildRecipe(const std::string &name, Template *out, std::string *error)
{
	Choices c;
	std::string canonical;
	if (!DecodeRecipe(name, &c) || !EncodeRecipe(c, &canonical)) {
		*error = "not a recipe name (OH-X1- and 16 characters)";
		return false;
	}
	Template plain;
	if (!MakeTemplate(c, &plain, error))
		return false;
	c.name = plain.name + "*";
	if (!MakeTemplate(c, out, error))
		return false;
	out->file_name = canonical + ".xdb";
	return true;
}

}  // namespace rmg
}  // namespace mods
