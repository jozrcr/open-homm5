// Test of the skill wheel's data (src/mods/skill_wheel_logic.cpp): the game's two tables in
// their own shape (a few items of each), and the wheel of a class. No game needed.
#include <stdio.h>

#include "../../mods/skill_wheel_logic.h"

using namespace mods::wheel;

namespace {

int g_failures = 0;

void Check(bool ok, const char *what)
{
	printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		++g_failures;
}

// GameMechanics/RefTables/Skills.xdb, cut down (the 2008 game's shape, with \r\n line ends).
const char kSkills[] =
	"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<Table_HeroSkill_SkillID ObjectRecordID=\"-1\">\r\n\t<objects>\r\n"
	"\t\t<Item>\r\n\t\t\t<ID>HERO_SKILL_NONE</ID>\r\n\t\t\t<obj>\r\n\t\t\t\t<Texture/>\r\n\t\t\t\t<NameFileRef/>\r\n"
	"\t\t\t\t<SkillType>SKILLTYPE_SKILL</SkillType>\r\n\t\t\t\t<HeroClass>HERO_CLASS_NONE</HeroClass>\r\n"
	"\t\t\t\t<BasicSkillID>HERO_SKILL_NONE</BasicSkillID>\r\n\t\t\t\t<SkillPrerequisites/>\r\n\t\t\t</obj>\r\n\t\t</Item>\r\n"
	// 1: a skill
	"\t\t<Item>\r\n\t\t\t<ID>HERO_SKILL_OFFENCE</ID>\r\n\t\t\t<obj>\r\n\t\t\t\t<Texture>\r\n\t\t\t\t\t<Item/>\r\n"
	"\t\t\t\t\t<Item href=\"/Textures/HeroScreen/Skills/Offence_1.xdb#xpointer(/Texture)\"/>\r\n"
	"\t\t\t\t\t<Item href=\"/Textures/HeroScreen/Skills/Offence_2.xdb#xpointer(/Texture)\"/>\r\n"
	"\t\t\t\t\t<Item href=\"/Textures/HeroScreen/Skills/Offence_3.xdb#xpointer(/Texture)\"/>\r\n\t\t\t\t</Texture>\r\n"
	"\t\t\t\t<NameFileRef>\r\n\t\t\t\t\t<Item href=\"/Text/Game/Skills/Common/Offence/1/Name.txt\"/>\r\n"
	"\t\t\t\t\t<Item href=\"/Text/Game/Skills/Common/Offence/2/Name.txt\"/>\r\n"
	"\t\t\t\t\t<Item href=\"/Text/Game/Skills/Common/Offence/3/Name.txt\"/>\r\n\t\t\t\t</NameFileRef>\r\n"
	"\t\t\t\t<SkillType>SKILLTYPE_SKILL</SkillType>\r\n\t\t\t\t<HeroClass>HERO_CLASS_NONE</HeroClass>\r\n"
	"\t\t\t\t<BasicSkillID>HERO_SKILL_NONE</BasicSkillID>\r\n\t\t\t\t<SkillPrerequisites/>\r\n\t\t\t</obj>\r\n\t\t</Item>\r\n"
	// 2: a standard ability of it
	"\t\t<Item>\r\n\t\t\t<ID>HERO_SKILL_FRENZY</ID>\r\n\t\t\t<obj>\r\n\t\t\t\t<Texture>\r\n"
	"\t\t\t\t\t<Item href=\"/Textures/HeroScreen/Perks/Offence_Frenzy_grey.xdb#xpointer(/Texture)\"/>\r\n"
	"\t\t\t\t\t<Item href=\"Perks/Offence_Frenzy.xdb#xpointer(/Texture)\"/>\r\n\t\t\t\t</Texture>\r\n"
	"\t\t\t\t<NameFileRef>\r\n\t\t\t\t\t<Item href=\"/Text/Game/Skills/Common/Offence/Frenzy/Name.txt\"/>\r\n\t\t\t\t</NameFileRef>\r\n"
	"\t\t\t\t<SkillType>SKILLTYPE_STANDART_PERK</SkillType>\r\n\t\t\t\t<HeroClass>HERO_CLASS_NONE</HeroClass>\r\n"
	"\t\t\t\t<BasicSkillID>HERO_SKILL_OFFENCE</BasicSkillID>\r\n\t\t\t\t<SkillPrerequisites/>\r\n\t\t\t</obj>\r\n\t\t</Item>\r\n"
	// 3: a special ability: the knight needs Frenzy and the class ability, the ranger only Frenzy
	"\t\t<Item>\r\n\t\t\t<ID>HERO_SKILL_RETRIBUTION</ID>\r\n\t\t\t<obj>\r\n"
	"\t\t\t\t<SkillType>SKILLTYPE_SPECIAL_PERK</SkillType>\r\n\t\t\t\t<HeroClass>HERO_CLASS_KNIGHT</HeroClass>\r\n"
	"\t\t\t\t<BasicSkillID>HERO_SKILL_OFFENCE</BasicSkillID>\r\n\t\t\t\t<SkillPrerequisites>\r\n"
	"\t\t\t\t\t<Item>\r\n\t\t\t\t\t\t<Class>HERO_CLASS_KNIGHT</Class>\r\n\t\t\t\t\t\t<dependenciesIDs>\r\n"
	"\t\t\t\t\t\t\t<Item>HERO_SKILL_FRENZY</Item>\r\n\t\t\t\t\t\t\t<Item>HERO_SKILL_EXPERT_TRAINER</Item>\r\n"
	"\t\t\t\t\t\t</dependenciesIDs>\r\n\t\t\t\t\t</Item>\r\n"
	"\t\t\t\t\t<Item>\r\n\t\t\t\t\t\t<Class>HERO_CLASS_RANGER</Class>\r\n\t\t\t\t\t\t<dependenciesIDs>\r\n"
	"\t\t\t\t\t\t\t<Item>HERO_SKILL_FRENZY</Item>\r\n\t\t\t\t\t\t</dependenciesIDs>\r\n\t\t\t\t\t</Item>\r\n"
	"\t\t\t\t</SkillPrerequisites>\r\n\t\t\t</obj>\r\n\t\t</Item>\r\n"
	// 4: the knight's racial skill, 5: its class ability, 6: another class's ability
	"\t\t<Item>\r\n\t\t\t<ID>HERO_SKILL_TRAINING</ID>\r\n\t\t\t<obj>\r\n\t\t\t\t<SkillType>SKILLTYPE_SKILL</SkillType>\r\n"
	"\t\t\t\t<HeroClass>HERO_CLASS_NONE</HeroClass>\r\n\t\t\t\t<BasicSkillID>HERO_SKILL_NONE</BasicSkillID>\r\n"
	"\t\t\t\t<SkillPrerequisites/>\r\n\t\t\t</obj>\r\n\t\t</Item>\r\n"
	"\t\t<Item>\r\n\t\t\t<ID>HERO_SKILL_EXPERT_TRAINER</ID>\r\n\t\t\t<obj>\r\n\t\t\t\t<SkillType>SKILLTYPE_CLASS_PERK</SkillType>\r\n"
	"\t\t\t\t<HeroClass>HERO_CLASS_KNIGHT</HeroClass>\r\n\t\t\t\t<BasicSkillID>HERO_SKILL_TRAINING</BasicSkillID>\r\n"
	"\t\t\t\t<SkillPrerequisites/>\r\n\t\t\t</obj>\r\n\t\t</Item>\r\n"
	"\t\t<Item>\r\n\t\t\t<ID>HERO_SKILL_FOREST_GUARD_EMBLEM</ID>\r\n\t\t\t<obj>\r\n\t\t\t\t<SkillType>SKILLTYPE_CLASS_PERK</SkillType>\r\n"
	"\t\t\t\t<HeroClass>HERO_CLASS_RANGER</HeroClass>\r\n\t\t\t\t<BasicSkillID>HERO_SKILL_TRAINING</BasicSkillID>\r\n"
	"\t\t\t\t<SkillPrerequisites/>\r\n\t\t\t</obj>\r\n\t\t</Item>\r\n"
	"\t</objects>\r\n</Table_HeroSkill_SkillID>\r\n";

const char kClasses[] =
	"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<Table_HeroClassDesc_HeroClass>\r\n\t<objects>\r\n"
	"\t\t<Item>\r\n\t\t\t<ID>HERO_CLASS_NONE</ID>\r\n\t\t\t<obj>\r\n\t\t\t\t<NameFileRef href=\"HeroClass/HeroClassNone.txt\"/>\r\n"
	"\t\t\t\t<SkillsProbs/>\r\n\t\t\t</obj>\r\n\t\t</Item>\r\n"
	"\t\t<Item>\r\n\t\t\t<ID>HERO_CLASS_KNIGHT</ID>\r\n\t\t\t<obj>\r\n\t\t\t\t<NameFileRef href=\"HeroClass/HeroClassKnight.txt\"/>\r\n"
	"\t\t\t\t<SkillsProbs>\r\n\t\t\t\t\t<Item>\r\n\t\t\t\t\t\t<SkillID>HERO_SKILL_TRAINING</SkillID>\r\n\t\t\t\t\t\t<Prob>10</Prob>\r\n"
	"\t\t\t\t\t</Item>\r\n\t\t\t\t\t<Item>\r\n\t\t\t\t\t\t<SkillID>HERO_SKILL_OFFENCE</SkillID>\r\n\t\t\t\t\t\t<Prob>8</Prob>\r\n"
	"\t\t\t\t\t</Item>\r\n\t\t\t\t</SkillsProbs>\r\n\t\t\t</obj>\r\n\t\t</Item>\r\n"
	"\t\t<Item>\r\n\t\t\t<ID>HERO_CLASS_WIZARD</ID>\r\n\t\t\t<obj>\r\n\t\t\t\t<NameFileRef href=\"HeroClass/HeroClassWizard.txt\"/>\r\n"
	"\t\t\t\t<SkillsProbs>\r\n\t\t\t\t\t<Item>\r\n\t\t\t\t\t\t<SkillID>HERO_SKILL_OFFENCE</SkillID>\r\n\t\t\t\t\t\t<Prob>3</Prob>\r\n"
	"\t\t\t\t\t</Item>\r\n\t\t\t\t</SkillsProbs>\r\n\t\t\t</obj>\r\n\t\t</Item>\r\n"
	"\t</objects>\r\n</Table_HeroClassDesc_HeroClass>\r\n";

}  // namespace

int main()
{
	const std::string folder = "/GameMechanics/RefTables/";
	Tables t;
	Check(ParseSkills(kSkills, folder, &t), "skills: parsed");
	Check(t.skills.size() == 7, "skills: 7 items, in the table's order");
	const Skill *offence = t.Find("HERO_SKILL_OFFENCE");
	Check(offence && offence->number == 1 && offence->kind == kSkill, "Offence: number 1 (its place), a skill");
	Check(offence && offence->textures.size() == 4 && offence->textures[0].empty() &&
	          offence->textures[1] == "/Textures/HeroScreen/Skills/Offence_1.xdb",
	      "Offence: 4 textures, the first empty, the links without #xpointer");
	Check(offence && offence->names.size() == 3 && offence->names[2] == "/Text/Game/Skills/Common/Offence/3/Name.txt",
	      "Offence: a name per level");
	const Skill *frenzy = t.Find("HERO_SKILL_FRENZY");
	Check(frenzy && frenzy->kind == kStandard && frenzy->basic == "HERO_SKILL_OFFENCE" && frenzy->needs.empty(),
	      "Frenzy: a standard ability of Offence, needs nothing");
	Check(frenzy && frenzy->textures.size() == 2 && frenzy->textures[1] == "/GameMechanics/RefTables/Perks/Offence_Frenzy.xdb",
	      "Frenzy: a relative texture link made a full path");
	const Skill *retribution = t.Find("HERO_SKILL_RETRIBUTION");
	Check(retribution && retribution->kind == kSpecial && retribution->needs.size() == 2 &&
	          retribution->needs.at("HERO_CLASS_KNIGHT").size() == 2 && retribution->needs.at("HERO_CLASS_RANGER").size() == 1,
	      "Retribution: special, needs by class (knight 2, ranger 1)");

	Check(ParseClasses(kClasses, folder, &t), "classes: parsed");
	Check(t.classes.size() == 3 && t.classes[1].id == "HERO_CLASS_KNIGHT" && t.classes[1].skills.size() == 2 &&
	          t.classes[1].skills[0].second == 10 &&
	          t.classes[1].name_file == "/GameMechanics/RefTables/HeroClass/HeroClassKnight.txt",
	      "classes: the knight's two skills, its chance, its name's file");

	const std::vector<Row> knight = WheelOf(t, "HERO_CLASS_KNIGHT");
	Check(knight.size() == 2 && knight[0].skill == 4 && knight[0].chance == 10 && knight[1].skill == 1,
	      "knight: Training then Offence (the class table's order)");
	Check(knight.size() == 2 && knight[0].perks.size() == 1 && knight[0].perks[0].number == 5 && knight[0].perks[0].kind == kClass,
	      "knight: Training has the knight's own ability only (not the ranger's)");
	Check(knight.size() == 2 && knight[1].perks.size() == 2 && knight[1].perks[0].number == 2 &&
	          knight[1].perks[1].number == 3 && knight[1].perks[1].needs.size() == 2 && knight[1].perks[1].needs[1] == 5,
	      "knight: Offence has Frenzy, then Retribution needing Frenzy and Expert Trainer");
	const std::vector<Row> wizard = WheelOf(t, "HERO_CLASS_WIZARD");
	Check(wizard.size() == 1 && wizard[0].perks.size() == 1 && wizard[0].perks[0].number == 2,
	      "wizard: Offence has Frenzy only (Retribution names no wizard)");
	Check(WheelOf(t, "HERO_CLASS_NOBODY").empty(), "an unknown class: no wheel");

	Check(TexturePicture("<Texture>\r\n\t<SrcName href=\"/UI/Icons/Skills/x.tga\"/>\r\n\t<DestName href=\"Offence_1.dds\"/>",
	                     "/Textures/HeroScreen/Skills/Offence_1.xdb") == "/Textures/HeroScreen/Skills/Offence_1.dds",
	      "a texture's picture: next to its .xdb");
	Check(TexturePicture("<Texture/>", "/a/b.xdb").empty(), "a texture without a picture: none");
	Check(FolderOf("/MapObjects/Haven/Brem.(AdvMapHeroShared).xdb#xpointer(/AdvMapHeroShared)") == "/MapObjects/Haven/",
	      "the folder of a link's file");

	printf(g_failures ? "%d failure(s)\n" : "skill wheel test: all passed\n", g_failures);
	return g_failures ? 1 : 0;
}
