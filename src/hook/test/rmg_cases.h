// Reads the cases that tools/rmg/test_template_maker.js --expected <folder>
// writes (its writeExpected() describes the format), for the tests of the C++
// template maker and of the recipe names.
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "../../mods/rmg_maker.h"

namespace rmgtest {

inline bool ReadFile(const std::string &path, std::string *out)
{
	FILE *f = nullptr;
	if (fopen_s(&f, path.c_str(), "rb") || !f)
		return false;
	out->clear();
	char buf[65536];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
		out->append(buf, n);
	fclose(f);
	return true;
}

inline std::string Unhex(const std::string &h)
{
	std::string out;
	for (size_t i = 0; i + 1 < h.size(); i += 2)
		out += static_cast<char>(strtol(h.substr(i, 2).c_str(), nullptr, 16));
	return out;
}

inline std::vector<std::string> Split(const std::string &s, char sep)
{
	std::vector<std::string> out(1);
	for (char c : s) {
		if (c == sep)
			out.emplace_back();
		else
			out.back() += c;
	}
	return out;
}

// One choice ("c.<key>=<value>", key as in template_maker.js) into the C++ choices.
inline bool SetChoice(mods::rmg::Choices *c, const std::string &key, const std::string &v)
{
	if (key == "teams")
		c->teams = atoi(v.c_str());
	else if (key == "perTeam")
		c->per_team = atoi(v.c_str());
	else if (key == "teamZones")
		c->team_zones = atoi(v.c_str());
	else if (key == "borderZones")
		c->border_zones = atoi(v.c_str());
	else if (key == "centreZones")
		c->centre_zones = atoi(v.c_str());
	else if (key == "borderTowns")
		c->border_towns = v == "true";
	else if (key == "teamTowns")
		c->team_towns = v == "true";
	else if (key == "underground")
		c->underground = v == "true";
	else if (key == "allyLink")
		c->ally_link = v;
	else if (key == "startMines")
		c->start_mines = v;
	else if (key == "loot")
		c->loot = v;
	else if (key == "guards")
		c->guards = v;
	else if (key == "size")
		c->size = v;
	else if (key == "name")
		c->name = Unhex(v);
	else if (key.size() == 16 && key.compare(0, 15, "lootByDistance.") == 0 && key[15] >= '0' && key[15] <= '3')
		c->loot_by_distance[key[15] - '0'] = v;
	else if (key.size() == 18 && key.compare(0, 17, "guardsByDistance.") == 0 && key[17] >= '0' && key[17] <= '3')
		c->guards_by_distance[key[17] - '0'] = v;
	else
		return false;
	return true;
}

struct Case {
	std::string id;            // ready-1, random-1, special-1, ...
	std::string file;          // the expected .xdb in the folder, if it builds
	bool refused = false;      // JavaScript refuses these choices...
	std::string error;         // ...with this message
	mods::rmg::Choices choices;
	std::string name, file_name, warnings, stats;  // what JavaScript made of them
	std::string unknown;       // a field this reader does not know (a new choice?)
};

// False when cases.txt cannot be read.
inline bool LoadCases(const std::string &dir, std::vector<Case> *cases)
{
	std::string text;
	if (!ReadFile(dir + "/cases.txt", &text))
		return false;
	for (const std::string &line : Split(text, '\n')) {
		if (line.empty())
			continue;
		Case k;
		for (const std::string &field : Split(line, '\t')) {
			const size_t eq = field.find('=');
			const std::string key = field.substr(0, eq), v = eq == std::string::npos ? "" : field.substr(eq + 1);
			if (key == "id")
				k.id = v;
			else if (key == "file")
				k.file = v;
			else if (key == "error") {
				k.error = Unhex(v);
				k.refused = true;
			} else if (key == "out.name")
				k.name = Unhex(v);
			else if (key == "out.fileName")
				k.file_name = Unhex(v);
			else if (key == "out.warnings")
				k.warnings = Unhex(v);
			else if (key == "out.stats")
				k.stats = Unhex(v);
			else if (key.compare(0, 2, "c.") != 0 || !SetChoice(&k.choices, key.substr(2), v))
				k.unknown = key;
		}
		cases->push_back(k);
	}
	return true;
}

}  // namespace rmgtest
