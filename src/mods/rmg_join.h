// Mod: joining multiplayer random map games (see rmg_join.cpp).
#pragma once

#include <string>

namespace mods {

// Called by ApplyRmgTemplatesFromSettings. Always puts in the guard against a
// missing template (a crash fix); with recipes_served, also the connector hook
// that registers a joined game's recipe before the game lists its templates.
void ApplyRmgJoin(bool recipes_served);

// The database reference of the template a template link points to (an NDb
// pointer in game memory, as in sRMGProps), for example
// "/RMG/Templates/OH-X1-321110nnxxxxxxxa.xdb#xpointer(/RMGTemplate)". False
// when the link is empty or unreadable. Read with checks; no game call.
bool TemplateLinkReference(const void *link, std::string *reference);

// Offsets of the template link (sRMGProps +0x1c InitialParams, +0xc Template).
const unsigned kRmgPropsTemplateLink = 0x28;
const unsigned kAdvMapDescRmgProps = 0x254;  // in NDb::SAdvMapDesc (map.xdb's AdvMapDesc)

}  // namespace mods
