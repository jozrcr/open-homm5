// The template builder's data (see rmg_builder_data.h).
#include "rmg_builder_data.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <map>

#include "rmg_recipe.h"

// UTF-8 letters for the French words (the sources are compiled as plain bytes).
#define A_GRAVE "\xc3\xa0"
#define A_GRAVE_CAP "\xc3\x80"
#define E_ACUTE "\xc3\xa9"
#define E_ACUTE_CAP "\xc3\x89"
#define E_GRAVE "\xc3\xa8"
#define C_CEDIL "\xc3\xa7"
#define MIDDOT "\xc2\xb7"
#define TIMES "\xc3\x97"

namespace mods {
namespace rmg {
namespace builder {

// The 24 ready templates, in make_templates.js's order. The recipe of each is
// the ready file's choices (tools/rmg/README.md, "The ready templates"); the
// test checks that each builds that name.
const Preset kPresets[] = {
	{"OH-X1-221110nnxxxxxxxa", "OH-2v2-Lu-Normal"},
	{"OH-X1-221111nnxxxxxxxa", "OH-2v2-Lu-Normal-Towns"},
	{"OH-X1-221220rnxxxxxxxa", "OH-2v2-Lu-Rich"},
	{"OH-X1-221221rnxxxxxxxa", "OH-2v2-Lu-Rich-Towns"},
	{"OH-X1-321110nnxxxxxxxa", "OH-2v2v2-Lu-Normal"},
	{"OH-X1-321111nnxxxxxxxa", "OH-2v2v2-Lu-Normal-Towns"},
	{"OH-X1-321210rnxxxxxxxa", "OH-2v2v2-XLu-Rich"},
	{"OH-X1-321211rnxxxxxxxa", "OH-2v2v2-XLu-Rich-Towns"},
	{"OH-X1-231110nnxxxxxxxa", "OH-3v3-Lu-Normal"},
	{"OH-X1-231220rnxxxxxxxa", "OH-3v3-XLu-Rich"},
	{"OH-X1-231221vnxxxxxxxa", "OH-3v3-XLu-Veryrich-Towns"},
	{"OH-X1-241110nnxxxxxxxa", "OH-4v4-Lu-Normal"},
	{"OH-X1-241111nnxxxxxxxa", "OH-4v4-Lu-Normal-Towns"},
	{"OH-X1-242220rnxxxxxxxa", "OH-4v4-XLu-Rich"},
	{"OH-X1-242221rnxxxxxxxa", "OH-4v4-XLu-Rich-Towns"},
	{"OH-X1-311110nnxxxxxxxa", "OH-3P-FFA-Lu-Normal"},
	{"OH-X1-311110rnxxxxxxxa", "OH-3P-FFA-Lu-Rich"},
	{"OH-X1-411210nnxxxxxxxa", "OH-4P-FFA-XLu-Normal"},
	{"OH-X1-411210rnxxxxxxxa", "OH-4P-FFA-XLu-Rich"},
	{"OH-X1-611110nnxxxxxxxa", "OH-6P-FFA-XLu-Normal"},
	{"OH-X1-611111nnxxxxxxxa", "OH-6P-FFA-XLu-Normal-Towns"},
	{"OH-X1-611110rnxxxxxxxa", "OH-6P-FFA-XLu-Rich"},
	{"OH-X1-811110nnxxxxxxxa", "OH-8P-FFA-Hu-Normal"},
	{"OH-X1-811110rnxxxxxxxa", "OH-8P-FFA-Hu-Rich"},
};
const int kPresetCount = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));

int PresetOf(const Choices &choices)
{
	std::string recipe;
	if (!EncodeRecipe(choices, &recipe))
		return -1;
	for (int i = 0; i < kPresetCount; ++i)
		if (recipe == kPresets[i].recipe)
			return i;
	return -1;
}

const char *const kLootKeys[4] = {"poor", "normal", "rich", "veryrich"};
const char *const kGuardKeys[3] = {"weak", "normal", "strong"};

namespace {

// Short words: they must fit the game's 1024x768 layout (the panel measures
// them and cuts with "..." only as a last resort).
const Words kEnglishWords = {
	"Template builder",
	"Start from", "(custom)",
	"Teams", "Number of teams", "Players per team", "Teammates' path", {"Weak guard", "Open"},
	"Start zone mines", {"All six", "Wood and ore"},
	"Neutral zones", "Per team", "Between teams", "In the centre", "Towns between teams", "Extra town per team",
	"Loot and guards", "Loot", {"Poor", "Normal", "Rich", "Very rich"}, "Guards", {"Weak", "Normal", "Strong"},
	"By distance from the bases", "Same", {"Start", "Next", "2 steps", "Further"},
	"Map", "Size", "Auto: %s",
	{"Tiny (72" TIMES "72)", "Small (96" TIMES "96)", "Medium (136" TIMES "136)", "Large (176" TIMES "176)",
	 "Extra large (216" TIMES "216)", "Huge (256" TIMES "256)", "Impossible (320" TIMES "320)"},
	"Underground",
	"colours = teams " MIDDOT " grey = neutral " MIDDOT " gold = centre",
	"%d players " MIDDOT " %d zones (%d neutral)",
	"%s",
	"%s " MIDDOT " underground",
	"Use", "Save as file", "Cancel",
	"Saved: %s", "Not saved: %s", "The game's screen was not set (see open-homm5.log)",
	"Template...", "The template builder (open-homm5)",
};

const Words kFrenchWords = {
	"Cr" E_ACUTE "ateur de mod" E_GRAVE "les",
	"Partir d'un mod" E_GRAVE "le", "(personnalis" E_ACUTE ")",
	E_ACUTE_CAP "quipes", "Nombre d'" E_ACUTE "quipes", "Joueurs par " E_ACUTE "quipe", "Passage entre alli" E_ACUTE "s",
	{"Gard" E_ACUTE " (faible)", "Ouvert"},
	"Mines de d" E_ACUTE "part", {"Les six", "Bois et minerai"},
	"Zones neutres", "Par " E_ACUTE "quipe", "Entre les " E_ACUTE "quipes", "Au centre", "Villes entre les " E_ACUTE "quipes",
	"Ville en plus par " E_ACUTE "quipe",
	"Butin et gardes", "Butin", {"Pauvre", "Normal", "Riche", "Tr" E_GRAVE "s riche"}, "Gardes",
	{"Faibles", "Normales", "Fortes"},
	"Selon la distance des bases", "Idem", {"D" E_ACUTE "part", "Voisines", A_GRAVE_CAP " 2 pas", "Plus loin"},
	"Carte", "Taille", "Auto : %s",
	{"Minuscule (72" TIMES "72)", "Petite (96" TIMES "96)", "Moyenne (136" TIMES "136)", "Grande (176" TIMES "176)",
	 "Tr" E_GRAVE "s grande (216" TIMES "216)", "Immense (256" TIMES "256)", "Impossible (320" TIMES "320)"},
	"Souterrain",
	"couleurs = " E_ACUTE "quipes " MIDDOT " gris = neutre " MIDDOT " or = centre",
	"%d joueurs " MIDDOT " %d zones (%d neutres)",
	"%s",
	"%s " MIDDOT " souterrain",
	"Utiliser", "Enregistrer", "Annuler",
	"Enregistr" E_ACUTE " : %s", "Non enregistr" E_ACUTE " : %s",
	"L'" E_ACUTE "cran du jeu n'a pas " E_ACUTE "t" E_ACUTE " r" E_ACUTE "gl" E_ACUTE " (voir open-homm5.log)",
	"Mod" E_GRAVE "les...", "Le cr" E_ACUTE "ateur de mod" E_GRAVE "les (open-homm5)",
};

bool StartsWith(const std::string &s, const char *prefix)
{
	return s.compare(0, strlen(prefix), prefix) == 0;
}

std::string Format(const char *format, int a, int b = 0)
{
	char text[256];
	_snprintf_s(text, sizeof(text), _TRUNCATE, format, a, b);
	return text;
}

}  // namespace

const Words &WordsFor(int language)
{
	return language == kFrench ? kFrenchWords : kEnglishWords;
}

std::string Localize(const std::string &text, int language)
{
	if (language != kFrench)
		return text;
	int a = 0, b = 0;
	if (StartsWith(text, "Zones are small (") && sscanf_s(text.c_str(), "Zones are small (%d", &a) == 1)
		return Format("Zones petites (%d cases chacune) : carte plus grande ou moins de zones.", a);
	if (StartsWith(text, "Zones are very big (") && sscanf_s(text.c_str(), "Zones are very big (%d", &a) == 1)
		return Format("Zones tr" E_GRAVE "s grandes (%d cases chacune) : le g" E_ACUTE "n" E_ACUTE "rateur peut les "
		              "laisser vides ; carte plus petite ou plus de zones.",
		              a);
	if (StartsWith(text, "Some zones hold more treasure (") &&
	    sscanf_s(text.c_str(), "Some zones hold more treasure (%d) than any shipped template (%d", &a, &b) == 2)
		return Format("Des zones ont plus de tr" E_ACUTE "sor (%d) que tout mod" E_GRAVE "le livr" E_ACUTE
		              " (%d) : pas encore test" E_ACUTE " en jeu.",
		              a, b);
	if (StartsWith(text, "the game allows at most 8 players (") &&
	    sscanf_s(text.c_str(), "the game allows at most 8 players (%d", &a) == 1)
		return Format("Le jeu permet 8 joueurs au plus (%d demand" E_ACUTE "s).", a);
	if (StartsWith(text, "need at least 2 teams"))
		return "Il faut au moins 2 " E_ACUTE "quipes (1 joueur par " E_ACUTE "quipe = chacun pour soi).";
	return text;
}

// positions(g, o) of template-maker.html, line for line.
std::vector<Point> ZonePositions(const Template &t, const Choices &o)
{
	const double kPi = 3.14159265358979323846;
	const double cx = kMapCentreX, cy = kMapCentreY, R = kMapRadius;
	const int T = o.teams;
	const double step = 2 * kPi / (T > 0 ? T : 1);
	const int sides = T == 2 ? 2 : T;
	std::map<std::pair<int, int>, std::vector<int>> by_team;  // (kind, team) -> zone numbers
	const int n = static_cast<int>(t.zones.size());
	int centres = 0;
	for (int i = 0; i < n; ++i) {
		by_team[std::make_pair(static_cast<int>(t.zones[i].kind), t.zones[i].team)].push_back(i);
		centres += t.zones[i].kind == ZoneKind::kCentre;
	}
	int border_idx = 0, centre_idx = 0;
	std::vector<Point> pos;
	for (int i = 0; i < n; ++i) {
		const Zone &z = t.zones[i];
		double a, r;
		if (z.kind == ZoneKind::kStart || z.kind == ZoneKind::kTeam) {
			const std::vector<int> &list = by_team[std::make_pair(static_cast<int>(z.kind), z.team)];
			int k = 0;
			while (k < static_cast<int>(list.size()) && list[k] != i)
				++k;
			const int count = static_cast<int>(list.size());
			const double spread = (step * 0.5 < 0.42 ? step * 0.5 : 0.42) * (z.kind == ZoneKind::kTeam ? 0.7 : 1);
			a = -kPi / 2 + z.team * step + (count > 1 ? (static_cast<double>(k) / (count - 1) - 0.5) * spread : 0);
			r = z.kind == ZoneKind::kStart ? R : R * 0.58;
		}
		else if (z.kind == ZoneKind::kBorder) {
			const int per = o.border_zones > 0 ? o.border_zones : 1;
			const int side = border_idx / per, k2 = border_idx % per;
			border_idx++;
			const double side_step = 2 * kPi / sides;
			a = -kPi / 2 + side * side_step + side_step / 2 +
			    (per > 1 ? (static_cast<double>(k2) / (per - 1) - 0.5) * side_step * 0.35 : 0);
			r = R * (T == 2 ? 0.95 : 0.98);
		}
		else {
			a = -kPi / 2 + centre_idx * (2 * kPi / (centres > 1 ? centres : 1)) + kPi / 4;
			r = centres > 1 ? 34 : 0;
			centre_idx++;
		}
		pos.push_back({static_cast<float>(cx + r * cos(a)), static_cast<float>(cy + r * sin(a))});
	}
	return pos;
}

// --- the native button --------------------------------------------------------------------
const char kMainPanelPath[] = "UI/RMGScreen/MainPanel.(WindowSimpleShared).xdb";
const char kSaveButtonPath[] = "UI/RMGScreen/Save.(WindowMSButton).xdb";
const char kSaveReactionPath[] = "UI/RMGScreen/Save.(UISRunReaction).xdb";
const char kButtonPath[] = "UI/RMGScreen/OpenHomm5Builder.(WindowMSButton).xdb";
const char kReactionPath[] = "UI/RMGScreen/OpenHomm5Builder.(UISRunReaction).xdb";
const char kLabelPath[] = "UI/RMGScreen/OpenHomm5Builder.txt";
const char kTooltipPath[] = "UI/RMGScreen/TTOpenHomm5Builder.txt";
const char kCommand[] = "oh_builder";

namespace {

// Replaces `from` by `to`, which must be in `text` exactly once.
bool ReplaceOnce(std::string *text, const char *from, const char *to)
{
	const size_t at = text->find(from);
	if (at == std::string::npos || text->find(from, at + 1) != std::string::npos)
		return false;
	text->replace(at, strlen(from), to);
	return true;
}

bool Known(const std::string &bytes, unsigned long long fingerprint)
{
	return Fingerprint(bytes) == fingerprint;
}

}  // namespace

bool MakeMainPanel(const std::string &original, std::string *out)
{
	if (!Known(original, kMainPanelFingerprint))
		return false;
	std::string s = original;
	// One more child, right after the Save button.
	if (!ReplaceOnce(&s, "\t\t<Item href=\"Save.(WindowMSButton).xdb#xpointer(/WindowMSButton)\"/>\r\n",
	                 "\t\t<Item href=\"Save.(WindowMSButton).xdb#xpointer(/WindowMSButton)\"/>\r\n"
	                 "\t\t<Item href=\"OpenHomm5Builder.(WindowMSButton).xdb#xpointer(/WindowMSButton)\"/>\r\n"))
		return false;
	*out = s;
	return true;
}

bool MakeButton(const std::string &save_button, std::string *out)
{
	if (!Known(save_button, kSaveButtonFingerprint))
		return false;
	std::string s = save_button;
	char position[64];
	_snprintf_s(position, sizeof(position), _TRUNCATE, "<x>%d</x>\r\n\t\t\t\t<y>%d</y>", kButtonX, kButtonY);
	const bool ok = ReplaceOnce(&s, "<Name>SaveBtn</Name>", "<Name>OpenHomm5BuilderBtn</Name>") &&
	                ReplaceOnce(&s, "<TooltipFileRef href=\"TTSaveBtn.txt\"/>",
	                            "<TooltipFileRef href=\"TTOpenHomm5Builder.txt\"/>") &&
	                ReplaceOnce(&s, "<x>188</x>\r\n\t\t\t\t<y>430</y>", position) &&
	                ReplaceOnce(&s, "<Enabled>false</Enabled>", "<Enabled>true</Enabled>") &&
	                ReplaceOnce(&s, "\"Save.(UISRunReaction).xdb#xpointer(/UISRunReaction)\"",
	                            "\"OpenHomm5Builder.(UISRunReaction).xdb#xpointer(/UISRunReaction)\"") &&
	                ReplaceOnce(&s, "<TextFileRef href=\"SaveBtn.txt\"/>", "<TextFileRef href=\"OpenHomm5Builder.txt\"/>");
	if (!ok)
		return false;
	*out = s;
	return true;
}

bool MakeReaction(const std::string &save_reaction, std::string *out)
{
	if (!Known(save_reaction, kSaveReactionFingerprint))
		return false;
	std::string s = save_reaction;
	if (!ReplaceOnce(&s, "<First>save</First>", "<First>oh_builder</First>"))
		return false;
	*out = s;
	return true;
}

std::string GameText(const char *utf8)
{
	std::string out("\xff\xfe", 2);
	for (const unsigned char *p = reinterpret_cast<const unsigned char *>(utf8); *p;) {
		unsigned code = *p++;
		if (code >= 0xc0 && code < 0xe0 && (*p & 0xc0) == 0x80)
			code = ((code & 0x1f) << 6) | (*p++ & 0x3f);
		else if (code >= 0xe0 && code < 0xf0 && (p[0] & 0xc0) == 0x80 && (p[1] & 0xc0) == 0x80) {
			code = ((code & 0x0f) << 12) | ((p[0] & 0x3f) << 6) | (p[1] & 0x3f);
			p += 2;
		}
		out += static_cast<char>(code & 0xff);
		out += static_cast<char>(code >> 8);
	}
	return out;
}

}  // namespace builder
}  // namespace rmg
}  // namespace mods
