// Writes the ready-made templates, the catalogue that the install package ships
// (tools/package/), to a folder.
//
//   node tools/rmg/make_templates.js                  # -> src/mods/rmg/templates/
//   node tools/rmg/make_templates.js <folder>
//
// Each entry is a set of template maker choices (see template_maker.js); the
// maker builds the name from them (OH-2v2v2-Lu-Rich-Towns), and the size is
// automatic. All names start with "OH-": the installer and uninstaller only
// touch files with those names.
//
// Every player of a multiplayer game needs the same files. Once a name has
// shipped, do not change what it builds; add a new entry instead.
//
// How the entries are built: "Normal" is the basic shape of a layout (one
// treasure zone behind each team, one neutral zone between neighbours, one rich
// zone in the middle). "Rich" has richer loot and, on the team layouts, more
// neutral zones and a bigger map. "Towns" puts a town in each neutral border
// zone. Free-for-all layouts keep one shape and only change the loot and towns.
"use strict";
var fs = require("fs");
var path = require("path");
var TM = require("./template_maker.js");

var DEFAULT_OUT = path.join(__dirname, "..", "..", "src", "mods", "rmg", "templates");

var READY = [
  // 2 vs 2 (4 players): Normal is 9 zones; Rich has two neutral zones per side and two in the middle.
  { teams: 2, perTeam: 2, teamZones: 1, borderZones: 1, centreZones: 1 },
  { teams: 2, perTeam: 2, teamZones: 1, borderZones: 1, centreZones: 1, borderTowns: true },
  { teams: 2, perTeam: 2, teamZones: 1, borderZones: 2, centreZones: 2, loot: "rich" },
  { teams: 2, perTeam: 2, teamZones: 1, borderZones: 2, centreZones: 2, borderTowns: true, loot: "rich" },
  // 2v2v2 (6 players): three teams of two, a neutral zone between each pair of teams.
  { teams: 3, perTeam: 2, teamZones: 1, borderZones: 1, centreZones: 1 },
  { teams: 3, perTeam: 2, teamZones: 1, borderZones: 1, centreZones: 1, borderTowns: true },
  { teams: 3, perTeam: 2, teamZones: 1, borderZones: 2, centreZones: 1, loot: "rich" },
  { teams: 3, perTeam: 2, teamZones: 1, borderZones: 2, centreZones: 1, borderTowns: true, loot: "rich" },
  // 3 vs 3 (6 players).
  { teams: 2, perTeam: 3, teamZones: 1, borderZones: 1, centreZones: 1 },
  { teams: 2, perTeam: 3, teamZones: 1, borderZones: 2, centreZones: 2, loot: "rich" },
  { teams: 2, perTeam: 3, teamZones: 1, borderZones: 2, centreZones: 2, borderTowns: true, loot: "veryrich" },
  // 4 vs 4 (8 players).
  { teams: 2, perTeam: 4, teamZones: 1, borderZones: 1, centreZones: 1 },
  { teams: 2, perTeam: 4, teamZones: 1, borderZones: 1, centreZones: 1, borderTowns: true },
  { teams: 2, perTeam: 4, teamZones: 2, borderZones: 2, centreZones: 2, loot: "rich" },
  { teams: 2, perTeam: 4, teamZones: 2, borderZones: 2, centreZones: 2, borderTowns: true, loot: "rich" },
  // Free-for-all: one private treasure zone per player, neutral zones between neighbours, a rich middle.
  { teams: 3, perTeam: 1, teamZones: 1, borderZones: 1, centreZones: 1 },
  { teams: 3, perTeam: 1, teamZones: 1, borderZones: 1, centreZones: 1, loot: "rich" },
  { teams: 4, perTeam: 1, teamZones: 1, borderZones: 2, centreZones: 1 },
  { teams: 4, perTeam: 1, teamZones: 1, borderZones: 2, centreZones: 1, loot: "rich" },
  { teams: 6, perTeam: 1, teamZones: 1, borderZones: 1, centreZones: 1 },
  { teams: 6, perTeam: 1, teamZones: 1, borderZones: 1, centreZones: 1, borderTowns: true },
  { teams: 6, perTeam: 1, teamZones: 1, borderZones: 1, centreZones: 1, loot: "rich" },
  { teams: 8, perTeam: 1, teamZones: 1, borderZones: 1, centreZones: 1 },
  { teams: 8, perTeam: 1, teamZones: 1, borderZones: 1, centreZones: 1, loot: "rich" }
];

// All ready templates, built: [{ choices, name, fileName, xml, bytes, stats, warnings }].
function buildAll() {
  return READY.map(function (choices) {
    var t = TM.makeTemplate(choices);
    t.choices = choices;
    // The file is the XML as UTF-8: BOM and CRLF stay as the maker wrote them.
    t.bytes = Buffer.from(t.xml, "utf8");
    return t;
  });
}

function main() {
  var out = process.argv[2] || DEFAULT_OUT;
  fs.mkdirSync(out, { recursive: true });
  var all = buildAll();
  all.forEach(function (t) {
    fs.writeFileSync(path.join(out, t.fileName), t.bytes);
    var s = t.stats;
    console.log("%s  %d players, %d zones (%d neutral), %s, %d tiles per zone%s", t.fileName, s.players, s.zones,
      s.neutralZones, s.size, s.tilesPerZone, t.warnings.length ? "\n    ! " + t.warnings.join("\n    ! ") : "");
  });
  // In the repo's own folder, drop the files of entries that no longer exist.
  if (path.resolve(out) === path.resolve(DEFAULT_OUT)) {
    var keep = {};
    all.forEach(function (t) { keep[t.fileName] = true; });
    fs.readdirSync(out).forEach(function (f) {
      if (/^OH-.*\.xdb$/.test(f) && !keep[f]) {
        fs.unlinkSync(path.join(out, f));
        console.log("removed %s (no longer in the ready set)", f);
      }
    });
  }
  console.log("%d templates written to %s", all.length, out);
}

if (require.main === module) main();
module.exports = { READY: READY, buildAll: buildAll, DEFAULT_OUT: DEFAULT_OUT };
