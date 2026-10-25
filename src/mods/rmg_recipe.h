// Recipe names: a template's file name that holds all its choices, so every PC
// can build the template from the name alone, with no file to share. For
// example OH-X1-321110nnxxxxxxxa.xdb is 2v2v2, Normal, with underground.
// The format is the table in rmg_recipe.cpp, explained in tools/rmg/README.md;
// docs/plans/in-game-generator.md says why.
//
// "X1" is the version of the format. What an X1 name builds must never change:
// players of one game build the template from its name on their own PCs. New
// choices or values get a new version (X2), and X1 names keep building as now.
#pragma once

#include <string>
#include <vector>

#include "rmg_maker.h"

namespace mods {
namespace rmg {

// "OH-X1-" and 16 characters, one per row of the table.
extern const char kRecipePrefix[];  // "OH-X1-"
const size_t kRecipeLength = 22;    // without ".xdb"

// Choices -> "OH-X1-<16 characters>" (without ".xdb"). False when an X1 name
// cannot hold the choices: a typed name, guards for the start zones
// (guards_by_distance[0]), or a number or word outside the table.
bool EncodeRecipe(const Choices &choices, std::string *name);

// "OH-X1-<16 characters>", with or without ".xdb", in any case (the game's
// file names are not case sensitive) -> choices. False for anything else,
// including more than 8 players.
bool DecodeRecipe(const std::string &name, Choices *choices);

// The name as EncodeRecipe writes it (sizes in capitals, the rest small,
// without ".xdb"), or false when it is not a recipe name.
bool CanonicalRecipe(const std::string &name, std::string *canonical);

// A path the game asks its file system for -> the recipe name (as
// CanonicalRecipe gives it), when the path is RMG/Templates/<recipe>.xdb: in
// any case, with / or \, leading slashes or not. False for any other path.
bool RecipeOfPath(const std::string &path, std::string *recipe);

// A list of recipe names as in open-homm5.ini's [rmg] recipes=: separated by
// commas, spaces ignored, ".xdb" or not, and ";" starts a comment. Each name
// goes once into *names, as CanonicalRecipe gives it; anything else goes into
// *bad as written.
void ParseRecipeList(const std::string &text, std::vector<std::string> *names, std::vector<std::string> *bad);

// FNV-1a, 64 bits, of a template's bytes: the fingerprint that
// tools/rmg/x1_fingerprints.txt records (not for security; easy in JavaScript).
unsigned long long Fingerprint(const std::string &bytes);

// Builds three recorded X1 names and compares them with their recorded size
// and fingerprint (from tools/rmg/x1_fingerprints.txt). The DLL runs it once at
// start: if this very build of the maker made other bytes, it must not serve
// recipes. False, with *problem set, on any difference.
bool SelfCheck(std::string *problem);

// What an X1 name builds: the maker's template from the decoded choices, named
// in the game's list after the maker's own name plus "*" (built from a recipe),
// e.g. "OH-2v2v2-Lu-Normal*". Its file_name is the canonical recipe + ".xdb".
bool BuildRecipe(const std::string &name, Template *out, std::string *error);

}  // namespace rmg
}  // namespace mods
