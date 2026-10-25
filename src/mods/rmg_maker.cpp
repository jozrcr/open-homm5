// The template maker in C++: a line-by-line port of tools/rmg/template_maker.js
// (see rmg_maker.h). Each function below has the name of its JavaScript
// original, so the two can be read side by side.
//
// Numbers: JavaScript computes in doubles, and so does this file, in the same
// order, so every product and quotient rounds the same way. Math.round is
// ported exactly (JsRound). Math.log only compares map sizes, where two sizes
// never come close enough for the last bit of a logarithm to matter (see
// ChooseSize).
#include "rmg_maker.h"

#include <limits.h>
#include <math.h>
#include <string.h>

#include <algorithm>
#include <set>
#include <utility>

namespace mods {
namespace rmg {

// Map sizes of the generator screen (side in tiles; .rdata 0xe556ac) and the
// letters MMH5.5's template names use for them.
const Size kSizes[7] = {
	{"T", "Tiny", 72}, {"S", "Small", 96}, {"M", "Medium", 136}, {"L", "Large", 176},
	{"XL", "Extra large", 216}, {"H", "Huge", 256}, {"I", "Impossible", 320},
};

namespace {

struct LootLevel {
	const char *key;
	const char *label;
	double value;   // treasure value
	double points;  // treasure building and shrine points
};
const LootLevel kLoot[4] = {
	{"poor", "Poor", 0.6, 0.7},
	{"normal", "Normal", 1.0, 1.0},
	{"rich", "Rich", 1.35, 1.25},
	{"veryrich", "Very rich", 1.7, 1.5},
};
struct GuardLevel {
	const char *key;
	const char *label;
	double scale;
};
const GuardLevel kGuards[3] = {
	{"weak", "Weak", 0.7},
	{"normal", "Normal", 1.0},
	{"strong", "Strong", 1.3},
};
// The highest treasure value in any shipped template; above it is untested.
const int kMaxShippedValue = 252000;

// Zone recipes (RECIPES): MMH5.5's balanced templates' values.
struct Recipe {
	bool can_be_player_start;
	bool town;
	int town_guard_strenght;
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
const Recipe kStartRecipe = {true, true, 10, {1, 1, 1, 1, 1, 1, 0}, 0, {1, 1, 1, 1, 0, 0, 0}, 8, 8, 12, 1, 1, 8, 40, 22, 0, 12, 56000};
const Recipe kTeamRecipe = {false, false, 10, {0, 0, 0, 0, 0, 0, 2}, 0, {0, 0, 0, 0, 0, 0, 0}, 52, 6, 8, 0, 1, 8, 20, 24, 32, 48, 168000};
const Recipe kBorderRecipe = {false, false, 10, {0, 0, 0, 0, 0, 0, 0}, 2, {0, 0, 0, 0, 1, 0, 0}, 48, 8, 12, 0, 1, 4, 20, 24, 0, 42, 148000};
const Recipe kCentreRecipe = {false, false, 10, {0, 0, 0, 0, 0, 0, 1}, 0, {0, 0, 0, 0, 0, 0, 0}, 52, 8, 12, 0, 1, 0, 0, 24, 40, 48, 252000};

const Recipe &RecipeOf(ZoneKind kind)
{
	switch (kind) {
	case ZoneKind::kStart: return kStartRecipe;
	case ZoneKind::kTeam: return kTeamRecipe;
	case ZoneKind::kBorder: return kBorderRecipe;
	default: return kCentreRecipe;
	}
}

// Guard strengths of MMH5.5's templates, by kind of connection (GUARD), in the
// order of LinkKind: ally, startTeam, startBorder, teamBorder (not used by the
// layout), borderBorder, centre, centreCentre.
const int kGuard[7] = {2, 6, 6, 8, 9, 12, 10};

const LootLevel *FindLoot(const std::string &key)
{
	for (const LootLevel &l : kLoot)
		if (key == l.key)
			return &l;
	return nullptr;
}

const GuardLevel *FindGuards(const std::string &key)
{
	for (const GuardLevel &g : kGuards)
		if (key == g.key)
			return &g;
	return nullptr;
}

// JavaScript's Math.round: the nearest integer, a half going up (towards
// +infinity). x - floor(x) is exact for doubles, so this is exact too.
double JsRound(double x)
{
	const double f = floor(x);
	return x - f >= 0.5 ? f + 1 : f;
}

// round(n, step)
int RoundTo(double n, int step)
{
	return static_cast<int>((std::max)(0.0, JsRound(n / step) * step));
}

std::string Str(int n)
{
	return std::to_string(n);
}

// layoutName(o)
std::string LayoutName(const Choices &o)
{
	if (o.per_team == 1)
		return Str(o.teams) + "P-FFA";
	std::string s;
	for (int i = 0; i < o.teams; i++)
		s += (i ? "v" : "") + Str(o.per_team);
	return s;
}

// area(size, underground)
int Area(const Size &size, bool underground)
{
	return size.side * size.side * (underground ? 2 : 1);
}

struct GraphZone {
	ZoneKind kind;
	int team;
};
struct Link {
	int a;
	int b;
	LinkKind kind;
};

// graph(o): the zones and the links between them (0-based).
void BuildGraph(const Choices &o, std::vector<GraphZone> *zones, std::vector<Link> *out)
{
	auto add = [zones](ZoneKind kind, int team) {
		zones->push_back({kind, team});
		return static_cast<int>(zones->size()) - 1;
	};
	std::vector<std::vector<int>> starts, team_zones, borders;
	std::vector<int> centres;
	// Start zones first and teammates next to each other (1+2, 3+4, ...).
	for (int t = 0; t < o.teams; t++) {
		starts.emplace_back();
		for (int p = 0; p < o.per_team; p++)
			starts[t].push_back(add(ZoneKind::kStart, t));
	}
	for (int t = 0; t < o.teams; t++) {
		team_zones.emplace_back();
		for (int z = 0; z < o.team_zones; z++)
			team_zones[t].push_back(add(ZoneKind::kTeam, t));
	}
	// Two teams meet on two sides; three or more meet once per neighbour pair.
	const int sides = o.teams == 2 ? 2 : o.teams;
	for (int s = 0; s < sides; s++) {
		borders.emplace_back();
		for (int z = 0; z < o.border_zones; z++)
			borders[s].push_back(add(ZoneKind::kBorder, -1));
	}
	for (int z = 0; z < o.centre_zones; z++)
		centres.push_back(add(ZoneKind::kCentre, -1));

	std::vector<Link> links;
	for (int t = 0; t < o.teams; t++) {
		const std::vector<int> &st = starts[t], &tz = team_zones[t];
		const int ns = static_cast<int>(st.size()), nt = static_cast<int>(tz.size());
		for (int p = 1; p < ns; p++)
			links.push_back({st[p - 1], st[p], LinkKind::kAlly});
		// Each team zone is reached from its share of the team's players.
		for (int z = 0; z < nt; z++) {
			for (int p = 0; p < ns; p++)
				if (nt == 1 || p % nt == z)
					links.push_back({st[p], tz[z], LinkKind::kStartTeam});
			if (z > 0)
				links.push_back({tz[z - 1], tz[z], LinkKind::kStartTeam});
		}
	}
	// Side s lies between team s (its last player) and team s+1 (its first).
	for (int s = 0; s < sides; s++) {
		const int a = s % o.teams, b = (s + 1) % o.teams;
		const int left = starts[a].back(), right = starts[b][0];
		const std::vector<int> &row = borders[s];
		if (row.empty()) {
			links.push_back({left, right, LinkKind::kStartBorder});
			continue;
		}
		links.push_back({left, row[0], LinkKind::kStartBorder});
		for (size_t z = 1; z < row.size(); z++)
			links.push_back({row[z - 1], row[z], LinkKind::kBorderBorder});
		links.push_back({row.back(), right, LinkKind::kStartBorder});
	}
	// The centre is reached from each team's treasure zones (or its players).
	for (size_t z = 0; z < centres.size(); z++) {
		for (int t = 0; t < o.teams; t++) {
			const std::vector<int> &from = team_zones[t].empty() ? starts[t] : team_zones[t];
			links.push_back({from[(z + t) % from.size()], centres[z], LinkKind::kCentre});
		}
		if (z > 0)
			links.push_back({centres[z - 1], centres[z], LinkKind::kCentreCentre});
	}
	// Two teams with no border zones would get the same link on both sides:
	// keep the first link of each pair of zones.
	std::set<std::pair<int, int>> seen;
	for (const Link &l : links)
		if (seen.insert({(std::min)(l.a, l.b), (std::max)(l.a, l.b)}).second)
			out->push_back(l);
}

const int kFar = INT_MAX;  // JavaScript's Infinity: no path to a start zone (never happens)

// distances(g): steps from each zone to the nearest start zone (0 for start zones).
std::vector<int> Distances(const std::vector<GraphZone> &zones, const std::vector<Link> &links)
{
	std::vector<std::vector<int>> adj(zones.size());
	for (const Link &l : links) {
		adj[l.a].push_back(l.b);
		adj[l.b].push_back(l.a);
	}
	std::vector<int> dist(zones.size()), todo;
	for (size_t i = 0; i < zones.size(); i++)
		dist[i] = zones[i].kind == ZoneKind::kStart ? 0 : kFar;
	for (size_t i = 0; i < zones.size(); i++)
		if (dist[i] == 0)
			todo.push_back(static_cast<int>(i));
	for (size_t head = 0; head < todo.size(); head++) {
		const int a = todo[head];
		for (int b : adj[a])
			if (dist[b] > dist[a] + 1) {
				dist[b] = dist[a] + 1;
				todo.push_back(b);
			}
	}
	return dist;
}

// tierOf(d)
int TierOf(int d)
{
	return (std::min)(d, 3);
}

// zoneFields(kind, index, o, dwellingTier, lootKey)
Zone ZoneFields(const GraphZone &g, int index, const Choices &o, int dwelling_tier, const LootLevel &loot)
{
	const Recipe &r = RecipeOf(g.kind);
	Zone z;
	z.kind = g.kind;
	z.team = g.team;
	z.index = index;
	z.can_be_player_start = r.can_be_player_start;
	z.town = r.town;
	z.town_guard_strenght = r.town_guard_strenght;
	memcpy(z.mines, r.mines, sizeof(z.mines));
	z.abandoned_mines = r.abandoned_mines;
	memcpy(z.dwellings, r.dwellings, sizeof(z.dwellings));
	z.upg_buildings_density = r.upg_buildings_density;
	z.treasure_density = r.treasure_density;
	z.treasure_chest_density = r.treasure_chest_density;
	z.prisons = r.prisons;
	z.land_cartographer = r.land_cartographer;
	z.shop_points = r.shop_points;
	z.luck_moral_buildings_density = r.luck_moral_buildings_density;
	z.resource_buildings_density = r.resource_buildings_density;
	if (g.kind == ZoneKind::kStart && o.start_mines == "woodore") {
		const int wood_ore[7] = {1, 1, 0, 0, 0, 0, 0};
		memcpy(z.mines, wood_ore, sizeof(z.mines));
	}
	if (g.kind == ZoneKind::kBorder) {
		z.town = o.border_towns;
		memset(z.dwellings, 0, sizeof(z.dwellings));
		z.dwellings[4 + dwelling_tier % 3] = 1;  // tiers 5, 6, 7 in turn, as MMH5.5 does
	}
	if (g.kind == ZoneKind::kTeam)
		z.town = o.team_towns;
	z.treasure_blocks_total_value = RoundTo(r.treasure_blocks_total_value * loot.value, 1000);
	z.treasure_building_points = RoundTo(r.treasure_building_points * loot.points, 1);
	z.shrine_points = RoundTo(r.shrine_points * loot.points, 1);
	return z;
}

// guardValue(kind, o, guardKey)
int GuardValue(LinkKind kind, const Choices &o, const GuardLevel &guards)
{
	if (kind == LinkKind::kAlly && o.ally_link == "open")
		return 0;
	const int g = kGuard[static_cast<int>(kind)];
	return kind == LinkKind::kAlly ? g : (std::max)(1, static_cast<int>(JsRound(g * guards.scale)));
}

// chooseSize(o, zoneCount). The best fit compares |log(tiles per zone / 5800)|
// between sizes. Two sizes tie exactly only when side1 * side2 * (1 or 2) =
// 5800 * zones, and no side is a multiple of 29 (a factor of 5800), so they
// never tie; for any sensible number of zones they differ by more than 1e-9,
// far more than the last bit of a logarithm. So C's log and JavaScript's agree.
const Size *ChooseSize(const Choices &o, int zone_count)
{
	const Size *best = nullptr;
	double best_miss = 0;
	for (const Size &s : kSizes) {
		const double per = static_cast<double>(Area(s, o.underground)) / zone_count;
		const double miss = fabs(log(per / kZoneAreaIdeal));
		if (o.size == s.key)
			return &s;
		if (o.size == "auto" && (!best || miss < best_miss)) {
			best = &s;
			best_miss = miss;
		}
	}
	return best;
}

// xmlValue(v)
std::string XmlValue(const std::string &v)
{
	std::string out;
	for (char c : v) {
		if (c == '&')
			out += "&amp;";
		else if (c == '<')
			out += "&lt;";
		else if (c == '>')
			out += "&gt;";
		else
			out += c;
	}
	return out;
}

const char *Bool(bool b)
{
	return b ? "true" : "false";
}

// toXml(t): laid out like the game's own files (UTF-8 with BOM, CRLF).
std::string ToXml(const Template &t)
{
	std::string out;
	auto line = [&out](const std::string &s) {
		out += s;
		out += "\r\n";
	};
	auto field = [&line](const char *f, const std::string &v) {
		line(std::string("      <") + f + ">" + XmlValue(v) + "</" + f + ">");
	};
	auto array = [&line](const char *f, const int *v, int n) {
		line(std::string("      <") + f + ">");
		for (int i = 0; i < n; i++)
			line("        <Item>" + Str(v[i]) + "</Item>");
		line(std::string("      </") + f + ">");
	};
	line("\xEF\xBB\xBF<?xml version=\"1.0\" encoding=\"UTF-8\"?>");  // the BOM, U+FEFF
	line("<RMGTemplate>");
	line("  <Editor name=\"open-homm5 template maker\" version=\"1\" />");
	line("  <NameFileRef href=\"\" />");
	line("  <DescriptionFileRef href=\"\" />");
	line("  <Name>" + XmlValue(t.name) + "</Name>");
	line("  <Zones>");
	// The fields in the game's order (FIELD_ORDER).
	for (const Zone &z : t.zones) {
		line("    <Item>");
		field("Index", Str(z.index));
		field("Setting", "RACE_RANDOM_TYPE");
		field("CanBeWater", "false");
		field("Size", "10");
		field("CanBePlayerStart", Bool(z.can_be_player_start));
		field("Town", Bool(z.town));
		field("TownGuardStrenght", Str(z.town_guard_strenght));
		array("Mines", z.mines, 7);
		field("AbandonedMines", Str(z.abandoned_mines));
		array("Dwellings", z.dwellings, 7);
		field("UpgBuildingsDensity", Str(z.upg_buildings_density));
		field("TreasureDensity", Str(z.treasure_density));
		field("TreasureChestDensity", Str(z.treasure_chest_density));
		field("Prisons", Str(z.prisons));
		field("LandCartographer", Str(z.land_cartographer));
		field("ShopPoints", Str(z.shop_points));
		field("ShrinePoints", Str(z.shrine_points));
		field("LuckMoralBuildingsDensity", Str(z.luck_moral_buildings_density));
		field("ResourceBuildingsDensity", Str(z.resource_buildings_density));
		field("TreasureBuildingPoints", Str(z.treasure_building_points));
		field("TreasureBlocksTotalValue", Str(z.treasure_blocks_total_value));
		field("DenOfThieves", "0");
		field("RedwoodObservatoryDensity", "0");
		field("BuffPoints", "0");
		line("    </Item>");
	}
	line("  </Zones>");
	line("  <Connections>");
	for (const Connection &c : t.connections) {
		line("    <Item>");
		line("      <SourceZoneIndex>" + Str(c.source) + "</SourceZoneIndex>");
		line("      <DestZoneIndex>" + Str(c.dest) + "</DestZoneIndex>");
		line("      <TwoWay>true</TwoWay>");
		line("      <GuardStrenght>" + Str(c.guard_strenght) + "</GuardStrenght>");
		line(std::string("      <Guarded>") + Bool(c.guarded) + "</Guarded>");
		line("      <Wide>false</Wide>");
		line("    </Item>");
	}
	line("  </Connections>");
	line("  <GraalOnMap>false</GraalOnMap>");
	line("  <MinPlayers>" + Str(t.min_players) + "</MinPlayers>");
	line("  <MaxPlayers>" + Str(t.max_players) + "</MaxPlayers>");
	line("  <MinMapSize>" + Str(t.min_map_size) + "</MinMapSize>");
	line("  <MaxMapSize>" + Str(t.max_map_size) + "</MaxMapSize>");
	line(std::string("  <Underground>") + Bool(t.underground) + "</Underground>");
	line("  <TestTemplate>false</TestTemplate>");
	line("</RMGTemplate>");  // and the last CRLF: JavaScript joins with a final ""
	return out;
}

// name.replace(/[^A-Za-z0-9.-]+/g, "_") + ".xdb". On UTF-8 bytes this gives
// what JavaScript gives on UTF-16: a run of other characters becomes one "_"
// either way.
std::string FileName(const std::string &name)
{
	std::string out;
	bool run = false;
	for (char c : name) {
		const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
		if (plain)
			out += c;
		else if (!run)
			out += '_';
		run = !plain;
	}
	return out + ".xdb";
}

}  // namespace

// makeTemplate(options)
bool MakeTemplate(const Choices &o, Template *out, std::string *error)
{
	*out = Template();
	if (!(o.teams >= 2 && o.per_team >= 1)) {
		*error = "need at least 2 teams (1 player per team = free-for-all)";
		return false;
	}
	const int players = o.teams * o.per_team;
	if (players > 8) {
		*error = "the game allows at most 8 players (" + Str(players) + " asked)";
		return false;
	}
	const LootLevel *loot = FindLoot(o.loot);
	if (!loot) {
		*error = "unknown loot level " + o.loot;
		return false;
	}
	const GuardLevel *guards = FindGuards(o.guards);
	if (!guards) {
		*error = "unknown guard level " + o.guards;
		return false;
	}
	for (const std::string &k : o.loot_by_distance)
		if (!k.empty() && !FindLoot(k)) {
			*error = "unknown loot level " + k;
			return false;
		}
	for (const std::string &k : o.guards_by_distance)
		if (!k.empty() && !FindGuards(k)) {
			*error = "unknown guard level " + k;
			return false;
		}
	auto loot_at = [&o](int d) { return FindLoot(o.loot_by_distance[TierOf(d)].empty() ? o.loot : o.loot_by_distance[TierOf(d)]); };
	auto guards_at = [&o](int d) {
		return FindGuards(o.guards_by_distance[TierOf(d)].empty() ? o.guards : o.guards_by_distance[TierOf(d)]);
	};

	std::vector<GraphZone> graph_zones;
	std::vector<Link> links;
	BuildGraph(o, &graph_zones, &links);
	const std::vector<int> dist = Distances(graph_zones, links);
	const int zone_count = static_cast<int>(graph_zones.size());
	const Size *size = ChooseSize(o, zone_count);
	if (!size) {
		*error = "unknown map size " + o.size;
		return false;
	}
	Template &t = *out;
	t.distance = dist;
	t.size = *size;
	const int map_area = Area(*size, o.underground);
	const int per_zone = static_cast<int>(JsRound(static_cast<double>(map_area) / zone_count));
	if (per_zone < kZoneAreaMin)
		t.warnings.push_back("Zones are small (" + Str(per_zone) + " tiles each): pick a bigger map or fewer zones.");
	if (per_zone > kZoneAreaMax)
		t.warnings.push_back("Zones are very big (" + Str(per_zone) +
		                     " tiles each): the generator may leave them empty; pick a smaller map or more zones.");

	int tier = 0;
	for (int i = 0; i < zone_count; i++) {
		const GraphZone &g = graph_zones[i];
		t.zones.push_back(ZoneFields(g, i + 1, o, g.kind == ZoneKind::kBorder ? tier++ : 0, *loot_at(dist[i])));
	}
	int richest = t.zones[0].treasure_blocks_total_value;
	for (const Zone &z : t.zones)
		richest = (std::max)(richest, z.treasure_blocks_total_value);
	if (richest > kMaxShippedValue)
		t.warnings.push_back("Some zones hold more treasure (" + Str(richest) + ") than any shipped template (" +
		                     Str(kMaxShippedValue) + "); not tested in the game yet.");
	for (const Link &l : links) {
		const int guard = GuardValue(l.kind, o, *guards_at((std::max)(dist[l.a], dist[l.b])));
		t.connections.push_back({l.a + 1, l.b + 1, guard, guard > 0, l.kind});
	}

	bool tiered = false;
	for (int k = 0; k < 4; k++)
		tiered = tiered || (!o.loot_by_distance[k].empty() && o.loot_by_distance[k] != o.loot) ||
		         (!o.guards_by_distance[k].empty() && o.guards_by_distance[k] != o.guards);
	const double thousands = map_area / 1000.0;
	if (!o.name.empty()) {
		t.name = o.name;
	} else {
		std::string label = loot->label;
		const size_t space = label.find(' ');  // String.replace(" ", ""): the first space only
		if (space != std::string::npos)
			label.erase(space, 1);
		t.name = "OH-" + LayoutName(o) + "-" + size->key + (o.underground ? "u" : "") + "-" + label;
		if (o.border_towns || o.team_towns)
			t.name += "-Towns";
		if (tiered)
			t.name += "-Tiers";
	}
	t.min_players = 2;
	t.max_players = players;
	t.min_map_size = static_cast<int>(floor(thousands * 0.97));
	t.max_map_size = static_cast<int>(ceil(thousands * 1.03));
	t.underground = o.underground;
	t.file_name = FileName(t.name);
	t.xml = ToXml(t);

	Stats &s = t.stats;
	s.players = players;
	s.zones = zone_count;
	s.size = std::string(size->name) + " (" + Str(size->side) + "x" + Str(size->side) +
	         (o.underground ? ", with underground" : "") + ")";
	s.tiles_per_zone = per_zone;
	s.connections = static_cast<int>(t.connections.size());
	s.neutral_zones = zone_count - players;
	for (int &n : s.by_distance)
		n = 0;
	for (int d : dist)
		s.by_distance[TierOf(d)]++;
	return true;
}

}  // namespace rmg
}  // namespace mods
