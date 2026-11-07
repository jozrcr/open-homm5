// Mod: the skill wheel in the game (skill_wheel.cpp): which skills and abilities a hero's
// class can get, with what each needs, and what the hero has. A small button on the hero
// screen (the original game: MMH5.5 has its own wheel), or a key.
#pragma once

namespace mods {

void ApplySkillWheelFromSettings();
// From the settings menu: [ui] skill_wheel and skill_wheel_key again, at once.
void ReloadSkillWheelSettings();

}  // namespace mods
