// The skill wheel's data, without the game (skill_wheel.cpp draws it): the game's own tables,
// GameMechanics/RefTables/Skills.xdb (every skill and ability: its kind, its skill, what each
// class needs before it) and HeroClass.xdb (each class's skills and their chances), read as
// text, and the wheel of one class built from them.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace mods {
namespace wheel {

enum Kind {
	kSkill,     // SKILLTYPE_SKILL: a skill (basic, advanced, expert)
	kStandard,  // SKILLTYPE_STANDART_PERK: one of a skill's three first abilities
	kClass,     // SKILLTYPE_CLASS_PERK: a class's own ability of its racial skill
	kSpecial,   // SKILLTYPE_SPECIAL_PERK: an ability that needs others first
	kUltimate,  // SKILLTYPE_UINQUE_PERK: the racial skill's last ability
	kOther,
};

struct Skill {
	std::string id;          // "HERO_SKILL_LOGISTICS"
	int number = 0;          // its place in the table: the game's skill number (HERO_SKILL_LOGISTICS = 1)
	Kind kind = kOther;
	std::string hero_class;  // HeroClass ("HERO_CLASS_NONE" for most)
	std::string basic;       // BasicSkillID: the skill an ability belongs to
	std::map<std::string, std::vector<std::string>> needs;  // by class: the abilities needed first
	std::vector<std::string> names;         // text files: a skill's three levels, an ability's one
	std::vector<std::string> descriptions;  // the same for the descriptions
	std::vector<std::string> textures;      // texture files (.xdb): a skill: [0] none, [1..3] its levels;
	                                        // an ability: [0] grey, [1] in colour
};

struct HeroClass {
	std::string id;         // "HERO_CLASS_KNIGHT"
	std::string name_file;  // its name's text file (full path)
	std::vector<std::pair<std::string, int>> skills;  // SkillsProbs: skill id, chance (in the table's order)
};

struct Tables {
	std::vector<Skill> skills;  // in the table's order: skills[n].number == n
	std::map<std::string, int> by_id;
	std::vector<HeroClass> classes;
	const Skill *Find(const std::string &id) const;
};

// Skills.xdb / HeroClass.xdb as text. `folder` is the tables' folder ("/GameMechanics/RefTables/"):
// relative links are made full paths with it. False when nothing was found.
bool ParseSkills(const std::string &xml, const std::string &folder, Tables *out);
bool ParseClasses(const std::string &xml, const std::string &folder, Tables *out);

struct Perk {
	int number = 0;            // the ability's skill number
	Kind kind = kStandard;
	std::vector<int> needs;    // the abilities this class needs first (skill numbers)
};

struct Row {
	int skill = 0;             // the skill's number
	int chance = 0;            // its chance for this class (SkillsProbs)
	std::vector<Perk> perks;   // standard, the class's own, special, ultimate
};

// The wheel of a class: its skills in the class table's order (most likely first), each with
// the abilities this class can take (a standard one without needs for any class, the class's
// own and ultimate ones of that class, a special one when its needs name this class).
std::vector<Row> WheelOf(const Tables &t, const std::string &class_id);

// The texture file's picture: "<DestName href="X.dds"/>" in a texture .xdb, made a full path
// with the .xdb's own folder ("" when there is none).
std::string TexturePicture(const std::string &texture_xml, const std::string &texture_path);

// "/A/B/C.xdb#xpointer(/X)" -> "/A/B/" (the folder of a link's file, for its relative links).
std::string FolderOf(const std::string &path);
// A link relative to `folder` made a full path; "#xpointer(...)" taken off.
std::string Resolve(const std::string &href, const std::string &folder);

}  // namespace wheel
}  // namespace mods
