// Writes the ready-made templates (the owner's first picks, Q9) to a folder.
//
//   node tools/rmg/make_templates.js                  # -> src/mods/rmg/templates/
//   node tools/rmg/make_templates.js <folder>
//
// Each entry is a set of template maker choices (see template_maker.js).
"use strict";
var fs = require("fs");
var path = require("path");
var TM = require("./template_maker.js");

var READY = [
  // 2v2v2: three teams of two, a neutral zone between each pair of teams.
  { teams: 3, perTeam: 2, teamZones: 1, borderZones: 1, centreZones: 1 },
  { teams: 3, perTeam: 2, teamZones: 1, borderZones: 1, centreZones: 1, borderTowns: true },
  { teams: 3, perTeam: 2, teamZones: 1, borderZones: 2, centreZones: 1, borderTowns: true, loot: "rich" },
  // 2 vs 2.
  { teams: 2, perTeam: 2, teamZones: 1, borderZones: 1, centreZones: 1 },
  { teams: 2, perTeam: 2, teamZones: 1, borderZones: 2, centreZones: 2, borderTowns: true, loot: "rich" },
  // Free-for-all with more neutral zones.
  { teams: 4, perTeam: 1, teamZones: 1, borderZones: 2, centreZones: 1 },
  { teams: 6, perTeam: 1, teamZones: 1, borderZones: 1, centreZones: 1, borderTowns: true },
  // Bigger and richer: 3 vs 3 and 4 vs 4.
  { teams: 2, perTeam: 3, teamZones: 1, borderZones: 2, centreZones: 2, borderTowns: true, loot: "veryrich" },
  { teams: 2, perTeam: 4, teamZones: 2, borderZones: 2, centreZones: 2, borderTowns: true, loot: "rich" }
];

function main() {
  var out = process.argv[2] || path.join(__dirname, "..", "..", "src", "mods", "rmg", "templates");
  fs.mkdirSync(out, { recursive: true });
  READY.forEach(function (choices) {
    var t = TM.makeTemplate(choices);
    fs.writeFileSync(path.join(out, t.fileName), t.xml, "utf8");
    var s = t.stats;
    console.log("%s  %d players, %d zones (%d neutral), %s, %d tiles per zone%s", t.fileName, s.players, s.zones,
      s.neutralZones, s.size, s.tilesPerZone, t.warnings.length ? "\n    ! " + t.warnings.join("\n    ! ") : "");
  });
}

if (require.main === module) main();
module.exports = { READY: READY };
