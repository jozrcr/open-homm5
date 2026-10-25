// The template maker in C++: random map templates (NDb::SRMGTemplate XML) for
// the game's own generator, made from a few choices. A faithful port of
// tools/rmg/template_maker.js: the same choices, the same checks, the same
// XML bytes. Pure code, no game access.
//
// The page (JavaScript) and the DLL (C++) must agree byte for byte: two players
// whose templates differ would build different maps. The parity test
// (src/hook/test/rmg_maker_test.cpp, run by build.ps1 -Test) compares the two on
// the ready templates and on hundreds of random choices. Change both together,
// or neither. See docs/plans/in-game-generator.md.
#pragma once

#include <string>
#include <vector>

namespace mods {
namespace rmg {

// The choices, with the defaults of template_maker.js (DEFAULTS). Words are the
// page's own: loot "poor", "normal", "rich", "veryrich"; guards "weak",
// "normal", "strong"; size "auto" or a size key ("T", "S", "M", "L", "XL", "H",
// "I").
struct Choices {
	int teams = 3;         // number of teams (1 player per team = free-for-all)
	int per_team = 2;      // players per team
	int team_zones = 1;    // treasure zones per team, behind its players
	int border_zones = 1;  // neutral zones between two neighbouring teams
	int centre_zones = 1;  // rich zones in the middle
	bool border_towns = false;
	bool team_towns = false;
	std::string ally_link = "weak";   // "open" = no guard between teammates; anything else = weak
	std::string start_mines = "all";  // "woodore" = wood and ore only; anything else = all six
	std::string loot = "normal";
	std::string guards = "normal";
	// By distance from the bases: 0 (start zones), 1, 2, 3 (three or more
	// steps). "" = the same as loot / guards. A path's guard follows the
	// farther of its two zones.
	std::string loot_by_distance[4];
	std::string guards_by_distance[4];
	std::string size = "auto";
	bool underground = true;
	std::string name;  // empty: made from the choices (UTF-8)
};

enum class ZoneKind { kStart, kTeam, kBorder, kCentre };
enum class LinkKind { kAlly, kStartTeam, kStartBorder, kTeamBorder, kBorderBorder, kCentre, kCentreCentre };

// One zone of the template, with the fields the XML holds.
struct Zone {
	ZoneKind kind;
	int team;  // -1 for neutral zones
	int index;  // 1-based
	bool can_be_player_start;
	bool town;
	int town_guard_strenght;  // sic, the game's spelling
	int mines[7];
	int abandoned_mines;
	int dwellings[7];
	int upg_buildings_density;
	int treasure_density;
	int treasure_chest_density;
	int prisons;
	int land_cartographer;
	int shop_points;
	int shrine_points;
	int luck_moral_buildings_density;
	int resource_buildings_density;
	int treasure_building_points;
	int treasure_blocks_total_value;
};

struct Connection {
	int source;  // 1-based zone indexes
	int dest;
	int guard_strenght;
	bool guarded;
	LinkKind kind;
};

struct Size {
	const char *key;   // "T" ... "I"
	const char *name;  // "Tiny" ... "Impossible"
	int side;          // tiles
};

struct Stats {
	int players;
	int zones;
	std::string size;  // "Large (176x176, with underground)"
	int tiles_per_zone;
	int connections;
	int neutral_zones;
	int by_distance[4];  // zone counts at 0, 1, 2 and 3+ steps from a base
};

struct Template {
	std::string name;       // as in the game's list
	std::string file_name;  // name with anything but A-Z a-z 0-9 . - as "_", plus ".xdb"
	std::string xml;        // the file: UTF-8 with a BOM, CRLF line ends
	std::vector<Zone> zones;
	std::vector<Connection> connections;
	std::vector<int> distance;  // steps from each zone to the nearest start zone
	Size size;
	int min_players;
	int max_players;
	int min_map_size;  // thousands of tiles
	int max_map_size;
	bool underground;
	std::vector<std::string> warnings;
	Stats stats;
};

// The map sizes of the generator screen, smallest first.
extern const Size kSizes[7];
// Tiles per zone that MMH5.5's templates are tuned for.
const int kZoneAreaMin = 4000;
const int kZoneAreaIdeal = 5800;
const int kZoneAreaMax = 7500;

// Builds the template. On bad choices returns false and sets *error to
// template_maker.js's message (the page shows the same words).
bool MakeTemplate(const Choices &choices, Template *out, std::string *error);

}  // namespace rmg
}  // namespace mods
