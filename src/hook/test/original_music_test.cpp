// Test of the "original music" mod's text work (src/mods/original_music_xdb.cpp)
// on small made-up data files in the game's format. No game needed.
#include <stdio.h>

#include <string>
#include <vector>

#include "../../mods/original_music.h"

using mods::music::Change;
using mods::music::MakeOriginal;
using mods::music::Result;

namespace {

int g_failures = 0;

void Check(bool ok, const char *what)
{
	printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		++g_failures;
}

std::string Definition(const char *track)
{
	return std::string("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Music ObjectRecordID=\"102\">\n\t<FileName>") + track +
	       "</FileName>\n\t<FadeIn>2000</FadeIn>\n\t<FadeOut>2000</FadeOut>\n</Music>\n";
}

std::string Tile(const char *priority, const char *music)
{
	return std::string("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<AdvMapTile ObjectRecordID=\"14\">\n"
	                   "\t<Texture href=\"/Textures/Terrain/Grass/Flowers.xdb#xpointer(/Texture)\"/>\n\t<Priority>") +
	       priority + "</Priority>\n\t<AmbientSoundSet href=\"/Sounds/_(Ambient)/TerrainAmbients/Grass.xdb#xpointer(/AmbientSoundSet)\"/>\n\t" +
	       music + "\n\t<HideInEditor>false</HideInEditor>\n</AdvMapTile>\n";
}

const char kFlowers[] = "<AdventureMusic href=\"/Sounds/_(Music)/AdvMap-Terrain/Flowers.xdb#xpointer(/Music)\"/>";
const char kGrass[] = "<AdventureMusic href=\"/Sounds/_(Music)/AdvMap-Terrain/Grass.xdb#xpointer(/Music)\"/>";
const char kNone[] = "<AdventureMusic/>";

}  // namespace

int main()
{
	std::string out, note;
	std::vector<Change> changes;

	// music definitions: the original file
	{
		const std::string mod = Definition("..\\Music\\MMH55\\Adventure_Terrain_SummerPlains_002.ogg");
		const std::string orig = Definition("..\\Music\\AdvMap-Terrain\\Terrain-Grass.ogg");
		const Result r = MakeOriginal("sounds/_(music)/advmap-terrain/grass.xdb", mod, &orig, &out, &changes, &note);
		Check(r == Result::Patched && out == orig, "changed music definition becomes the original");
		Check(changes.size() == 1 && changes[0].what == "track" &&
		          changes[0].from == "..\\Music\\MMH55\\Adventure_Terrain_SummerPlains_002.ogg" &&
		          changes[0].to == "..\\Music\\AdvMap-Terrain\\Terrain-Grass.ogg",
		      "  and the log names both tracks");
		Check(MakeOriginal("sounds/_(music)/advmap-terrain/grass.xdb", orig, &orig, &out, &changes, &note) ==
		          Result::Unchanged,
		      "same music definition: unchanged");
		Check(MakeOriginal("sounds/_(music)/advmap-terrain/flowers.xdb", mod, nullptr, &out, &changes, &note) ==
		          Result::Unchanged,
		      "mod's own music definition (no original): unchanged");
	}
	// a music table: the original file
	{
		const std::string mod = "<TableRaceMusic>\n<combatMusics>\n<Item href=\"Battle-Themes/Haven.xdb#xpointer(/Music)\"/>\n"
		                        "<Item href=\"Battle-Themes/Haven6.xdb#xpointer(/Music)\"/>\n</combatMusics>\n</TableRaceMusic>\n";
		const std::string orig = "<TableRaceMusic>\n<combatMusics>\n<Item href=\"Battle-Themes/Haven.xdb#xpointer(/Music)\"/>\n"
		                         "</combatMusics>\n</TableRaceMusic>\n";
		const Result r = MakeOriginal("sounds/_(music)/tableracemusic.xdb", mod, &orig, &out, &changes, &note);
		Check(r == Result::Patched && out == orig, "changed music table becomes the original");
		Check(changes.size() == 1 && changes[0].what == "whole file" && changes[0].from == "2 music references" &&
		          changes[0].to == "1",
		      "  and the log counts the references");
	}
	// a map tile: only its music reference changes
	{
		const std::string mod = Tile("13", kFlowers);  // the mod also changed the priority: kept
		const std::string orig = Tile("12", kGrass);
		const Result r = MakeOriginal("mapobjects/_(advmaptile)/grass/flowers.xdb", mod, &orig, &out, &changes, &note);
		Check(r == Result::Patched && out == Tile("13", kGrass), "tile: original music, the mod's other data kept");
		Check(changes.size() == 1 && changes[0].what == "AdventureMusic" &&
		          changes[0].from == "AdvMap-Terrain/Flowers.xdb" && changes[0].to == "AdvMap-Terrain/Grass.xdb",
		      "  and the log names both definitions");
		Check(MakeOriginal("mapobjects/_(advmaptile)/grass/grass.xdb", orig, &orig, &out, &changes, &note) ==
		          Result::Unchanged,
		      "tile with the original music: unchanged");
		Check(MakeOriginal("mapobjects/_(advmaptile)/grass/flowers.xdb", Tile("13", kNone), &orig, &out, &changes,
		                   &note) == Result::Patched &&
		          out == Tile("13", kGrass),
		      "tile whose music the mod removed: original music back");
		const std::string spaced =
			Tile("13", "<AdventureMusic href=\"/Sounds/_(Music)/ADVMAP-Terrain/Grass.xdb#xpointer(/Music)\" />");
		Check(MakeOriginal("mapobjects/_(advmaptile)/grass/grass.xdb", spaced, &orig, &out, &changes, &note) ==
		          Result::Unchanged,
		      "same music written differently (space, case): unchanged");
	}
	// a hero theme the original game does not have; the space before "/>" too
	{
		const std::string mod = "<AdvMapHeroShared>\n\t<AdventureMusic href=\"/Sounds/_(Music)/AdvMap-Heroes/Zehir.xdb#xpointer(/Music)\" />\n"
		                        "\t<Class>HERO_CLASS_WIZARD</Class>\n</AdvMapHeroShared>\n";
		const std::string orig = "<AdvMapHeroShared>\n\t<AdventureMusic/>\n\t<Class>HERO_CLASS_WIZARD</Class>\n</AdvMapHeroShared>\n";
		Check(MakeOriginal("mapobjects/academy/cyrus.(advmapheroshared).xdb", mod, &orig, &out, &changes, &note) ==
		              Result::Patched &&
		          out == orig && changes.size() == 1 && changes[0].to.empty(),
		      "hero theme added by the mod: none, as in the original");
		const std::string fresh = mod;  // a hero that only the mod has
		Check(MakeOriginal("mapobjects/dungeon/kastore.(advmapheroshared).xdb", fresh, nullptr, &out, &changes, &note) ==
		              Result::Patched &&
		          out == orig,
		      "hero only the mod has: no theme");
	}
	// files without music, or that do not match
	{
		const std::string plain = "<Creature>\n\t<Icon href=\"/Textures/x.xdb#xpointer(/Texture)\"/>\n</Creature>\n";
		Check(MakeOriginal("gamemechanics/creature/x.xdb", plain, nullptr, &out, &changes, &note) == Result::Unchanged,
		      "file without music: unchanged");
		const std::string two = Tile("13", kFlowers) + kFlowers;
		Check(MakeOriginal("mapobjects/_(advmaptile)/grass/flowers.xdb", two, &plain, &out, &changes, &note) ==
		              Result::Patched &&
		          out == Tile("13", kNone) + kNone,
		      "original without that element: music emptied");
		const std::string orig = Tile("12", kGrass);
		Check(MakeOriginal("mapobjects/_(advmaptile)/grass/flowers.xdb", two, &orig, &out, &changes, &note) ==
		              Result::Skipped &&
		          !note.empty(),
		      "different number of music elements: left alone, with a note");
	}

	printf(g_failures ? "original music test: %d FAILED\n" : "original music test: all passed\n", g_failures);
	return g_failures ? 1 : 0;
}
