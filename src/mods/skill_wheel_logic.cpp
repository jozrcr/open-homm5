// The skill wheel's data (skill_wheel_logic.h). Plain text search through the game's tables:
// each <Item> of the objects list is one skill or ability, in the game's own order.
#include "skill_wheel_logic.h"

#include <stdlib.h>
#include <string.h>

namespace mods {
namespace wheel {
namespace {

// The text of <tag>...</tag> in [from, until), else "".
std::string Value(const std::string &xml, const char *tag, size_t from, size_t until)
{
	const std::string open = std::string("<") + tag + ">";
	const size_t a = xml.find(open, from);
	if (a == std::string::npos || a >= until)
		return std::string();
	const size_t b = xml.find('<', a + open.size());
	return b == std::string::npos || b > until ? std::string() : xml.substr(a + open.size(), b - a - open.size());
}

// Every href="..." in [from, until).
std::vector<std::string> Hrefs(const std::string &xml, size_t from, size_t until)
{
	std::vector<std::string> out;
	for (size_t a = xml.find("href=\"", from); a != std::string::npos && a < until; a = xml.find("href=\"", a + 6)) {
		const size_t b = xml.find('"', a + 6);
		if (b == std::string::npos || b > until)
			break;
		out.push_back(xml.substr(a + 6, b - a - 6));
	}
	return out;
}

// The span of <tag>...</tag> (or an empty <tag/>) in [from, until): [*a, *b), the inside.
bool Span(const std::string &xml, const char *tag, size_t from, size_t until, size_t *a, size_t *b)
{
	const std::string open = std::string("<") + tag + ">", close = std::string("</") + tag + ">";
	const size_t o = xml.find(open, from);
	if (o == std::string::npos || o >= until)
		return false;
	const size_t c = xml.find(close, o);
	if (c == std::string::npos || c > until)
		return false;
	*a = o + open.size();
	*b = c;
	return true;
}

// The <Item>s of the objects list: [start, end) of each, at the list's own depth.
std::vector<std::pair<size_t, size_t>> Objects(const std::string &xml)
{
	std::vector<std::pair<size_t, size_t>> out;
	size_t a = 0, b = 0;
	if (!Span(xml, "objects", 0, xml.size(), &a, &b))
		return out;
	// Each object is "<Item>\s*<ID>...</ID>\s*<obj>...</obj>\s*</Item>": find the IDs.
	for (size_t id = xml.find("<ID>", a); id != std::string::npos && id < b;) {
		const size_t obj_end = xml.find("</obj>", id);
		if (obj_end == std::string::npos || obj_end > b)
			break;
		out.push_back({id, obj_end});
		id = xml.find("<ID>", obj_end);
	}
	return out;
}

Kind KindOf(const std::string &type)
{
	if (type == "SKILLTYPE_SKILL")
		return kSkill;
	if (type == "SKILLTYPE_STANDART_PERK")
		return kStandard;
	if (type == "SKILLTYPE_CLASS_PERK")
		return kClass;
	if (type == "SKILLTYPE_SPECIAL_PERK")
		return kSpecial;
	if (type == "SKILLTYPE_UINQUE_PERK")
		return kUltimate;
	return kOther;
}

std::vector<std::string> Links(const std::string &xml, const char *tag, size_t from, size_t until, const std::string &folder)
{
	std::vector<std::string> out;
	size_t a = 0, b = 0;
	if (Span(xml, tag, from, until, &a, &b)) {
		// <Item/> (an empty slot) counts as "": a skill's textures start with one
		for (size_t i = xml.find("<Item", a); i != std::string::npos && i < b; i = xml.find("<Item", i + 5)) {
			const size_t end = xml.find('>', i);
			const std::string item = xml.substr(i, end - i);
			const size_t h = item.find("href=\"");
			out.push_back(h == std::string::npos ? std::string()
			                                     : Resolve(item.substr(h + 6, item.find('"', h + 6) - h - 6), folder));
		}
	}
	return out;
}

}  // namespace

const Skill *Tables::Find(const std::string &id) const
{
	auto it = by_id.find(id);
	return it == by_id.end() ? nullptr : &skills[it->second];
}

std::string FolderOf(const std::string &path)
{
	std::string p = path.substr(0, path.find('#'));
	const size_t slash = p.rfind('/');
	return slash == std::string::npos ? std::string("/") : p.substr(0, slash + 1);
}

std::string Resolve(const std::string &href, const std::string &folder)
{
	std::string h = href.substr(0, href.find('#'));
	if (h.empty() || h[0] == '/')
		return h;
	return folder + h;
}

bool ParseSkills(const std::string &xml, const std::string &folder, Tables *out)
{
	out->skills.clear();
	out->by_id.clear();
	for (const auto &o : Objects(xml)) {
		Skill s;
		s.id = Value(xml, "ID", o.first, o.second);
		s.number = static_cast<int>(out->skills.size());
		s.kind = KindOf(Value(xml, "SkillType", o.first, o.second));
		s.hero_class = Value(xml, "HeroClass", o.first, o.second);
		s.basic = Value(xml, "BasicSkillID", o.first, o.second);
		s.textures = Links(xml, "Texture", o.first, o.second, folder);
		s.names = Links(xml, "NameFileRef", o.first, o.second, folder);
		s.descriptions = Links(xml, "DescriptionFileRef", o.first, o.second, folder);
		size_t a = 0, b = 0;
		if (Span(xml, "SkillPrerequisites", o.first, o.second, &a, &b)) {
			for (size_t c = xml.find("<Class>", a); c != std::string::npos && c < b; c = xml.find("<Class>", c + 7)) {
				const std::string cls = Value(xml, "Class", c, b);
				size_t da = 0, db = 0;
				std::vector<std::string> &needs = s.needs[cls];
				if (Span(xml, "dependenciesIDs", c, b, &da, &db))
					for (size_t i = xml.find("<Item>", da); i != std::string::npos && i < db; i = xml.find("<Item>", i + 6))
						needs.push_back(Value(xml, "Item", i, db));
			}
		}
		out->by_id[s.id] = s.number;
		out->skills.push_back(s);
	}
	return !out->skills.empty();
}

bool ParseClasses(const std::string &xml, const std::string &folder, Tables *out)
{
	out->classes.clear();
	for (const auto &o : Objects(xml)) {
		HeroClass c;
		c.id = Value(xml, "ID", o.first, o.second);
		const std::vector<std::string> name = Hrefs(xml, xml.find("<NameFileRef", o.first), o.second);
		if (!name.empty())
			c.name_file = Resolve(name[0], folder);
		size_t a = 0, b = 0;
		if (Span(xml, "SkillsProbs", o.first, o.second, &a, &b))
			for (size_t i = xml.find("<SkillID>", a); i != std::string::npos && i < b; i = xml.find("<SkillID>", i + 9))
				c.skills.push_back({Value(xml, "SkillID", i, b), atoi(Value(xml, "Prob", i, b).c_str())});
		out->classes.push_back(c);
	}
	return !out->classes.empty();
}

std::vector<Row> WheelOf(const Tables &t, const std::string &class_id)
{
	std::vector<Row> rows;
	const HeroClass *cls = nullptr;
	for (const HeroClass &c : t.classes)
		if (c.id == class_id)
			cls = &c;
	if (!cls)
		return rows;
	for (const auto &sp : cls->skills) {
		const Skill *skill = t.Find(sp.first);
		if (!skill)
			continue;
		Row row;
		row.skill = skill->number;
		row.chance = sp.second;
		// in this order: standard, the class's own, special, ultimate (each in the table's order)
		for (Kind kind : {kStandard, kClass, kSpecial, kUltimate}) {
			for (const Skill &p : t.skills) {
				if (p.kind != kind || p.basic != skill->id)
					continue;
				auto needs = p.needs.find(class_id);
				bool takes = false;
				if (kind == kStandard)
					takes = p.needs.empty() || needs != p.needs.end();
				else if (kind == kClass || kind == kUltimate)
					takes = p.hero_class == class_id;
				else
					takes = needs != p.needs.end();
				if (!takes)
					continue;
				Perk perk;
				perk.number = p.number;
				perk.kind = kind;
				if (needs != p.needs.end())
					for (const std::string &n : needs->second)
						if (const Skill *need = t.Find(n))
							perk.needs.push_back(need->number);
				row.perks.push_back(perk);
			}
		}
		rows.push_back(row);
	}
	return rows;
}

std::string TexturePicture(const std::string &texture_xml, const std::string &texture_path)
{
	const size_t a = texture_xml.find("<DestName href=\"");
	if (a == std::string::npos)
		return std::string();
	const size_t b = texture_xml.find('"', a + 16);
	if (b == std::string::npos)
		return std::string();
	return Resolve(texture_xml.substr(a + 16, b - a - 16), FolderOf(texture_path));
}

}  // namespace wheel
}  // namespace mods
