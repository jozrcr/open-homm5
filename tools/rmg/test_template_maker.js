// Checks the template maker on the ready templates and on many random choices:
//   node tools/rmg/test_template_maker.js
// The XML must be well formed, every zone reachable, start zones = players,
// teammates linked, indexes valid, and the map size must give sensible zones.
//
//   node tools/rmg/test_template_maker.js --expected build/rmg_parity
// also writes what the C++ maker (src/mods/rmg_maker.cpp) must build, for its
// parity test (src/hook/test/rmg_maker_test.cpp, run by build.ps1 -Test): the
// ready templates, 400 random choices over every value the page and the
// recipe names offer, a few special cases, and choices the maker must refuse.
// See writeExpected() below for the file format.
"use strict";
var fs = require("fs");
var path = require("path");
var TM = require("./template_maker.js");
var MT = require("./make_templates.js");
var READY = MT.READY;

var failures = 0, checks = 0;
function check(ok, what) {
  checks++;
  if (!ok) { failures++; console.log("FAIL: " + what); }
}

// A small XML well-formedness check: every tag closed in order.
function wellFormed(xml) {
  var stack = [], re = /<(\/?)([A-Za-z][\w.-]*)([^>]*?)(\/?)>/g, m;
  var body = xml.replace(/^﻿/, "").replace(/<\?xml[^>]*\?>/, "");
  while ((m = re.exec(body))) {
    if (m[4] === "/") continue;
    if (m[1] === "/") { if (stack.pop() !== m[2]) return false; } else stack.push(m[2]);
  }
  return stack.length === 0;
}

function verify(choices, label) {
  var t;
  try { t = TM.makeTemplate(choices); } catch (e) { check(false, label + ": " + e.message); return; }
  var z = t.template.Zones, c = t.template.Connections, n = z.length;
  check(wellFormed(t.xml), label + ": XML well formed");
  check(t.xml.charCodeAt(0) === 0xfeff && t.xml.indexOf("<RMGTemplate>") > 0, label + ": header");
  var starts = z.filter(function (x) { return x.CanBePlayerStart; }).length;
  check(starts === t.stats.players && t.template.MaxPlayers === starts, label + ": one start zone per player");
  check(z.every(function (x, i) { return x.Index === i + 1; }), label + ": zone indexes 1..n");
  var seen = {};
  c.forEach(function (l) {
    var key = Math.min(l.SourceZoneIndex, l.DestZoneIndex) + "-" + Math.max(l.SourceZoneIndex, l.DestZoneIndex);
    check(l.SourceZoneIndex !== l.DestZoneIndex, label + ": no zone linked to itself");
    check(l.SourceZoneIndex >= 1 && l.SourceZoneIndex <= n && l.DestZoneIndex >= 1 && l.DestZoneIndex <= n,
      label + ": link " + key + " inside the template");
    check(!seen[key], label + ": link " + key + " only once");
    seen[key] = true;
    check(l.Guarded === l.GuardStrenght > 0, label + ": guard flag matches strength");
  });
  // Every zone reachable from zone 1.
  var adj = {}, todo = [1], reached = { 1: true };
  c.forEach(function (l) {
    (adj[l.SourceZoneIndex] = adj[l.SourceZoneIndex] || []).push(l.DestZoneIndex);
    (adj[l.DestZoneIndex] = adj[l.DestZoneIndex] || []).push(l.SourceZoneIndex);
  });
  while (todo.length) (adj[todo.pop()] || []).forEach(function (b) { if (!reached[b]) { reached[b] = true; todo.push(b); } });
  check(Object.keys(reached).length === n, label + ": every zone reachable");
  // Teammates' start zones directly linked to each other.
  var per = choices.perTeam || TM.DEFAULTS.perTeam;
  for (var s = 1; s <= starts; s++)
    if ((s - 1) % per !== per - 1) check(seen[s + "-" + (s + 1)], label + ": teammates " + s + " and " + (s + 1) + " linked");
  // An automatic size keeps zones within the generator's comfortable range, or
  // says so when no map size can (too many or too few zones).
  var comfortable = t.stats.tilesPerZone >= TM.ZONE_AREA.min && t.stats.tilesPerZone <= TM.ZONE_AREA.max;
  var warned = t.warnings.some(function (w) { return /Zones are/.test(w); });
  check(comfortable !== warned, label + ": zone size " + t.stats.tilesPerZone + (warned ? " (warned)" : ""));
  if ((choices.size || "auto") === "auto" && !comfortable) {
    var sides = TM.SIZES.map(function (s) { return s.side * s.side * (choices.underground ? 2 : 1) / n; });
    check(sides.every(function (a) { return a < TM.ZONE_AREA.min || a > TM.ZONE_AREA.max; }),
      label + ": a map size with comfortable zones exists, but auto missed it");
  }
  check(t.template.MinMapSize <= t.template.MaxMapSize, label + ": size range");
}

READY.forEach(function (c, i) { verify(c, "ready " + (i + 1)); });

// The ready set is what the install package ships: names unique, nothing warns
// about zone sizes, the layouts we promise are all there, and the files in
// src/mods/rmg/templates are exactly what the maker builds now.
(function () {
  var all = MT.buildAll();
  var names = {}, files = {};
  all.forEach(function (t) {
    check(!names[t.name], "ready set: name " + t.name + " is unique");
    check(!files[t.fileName.toLowerCase()], "ready set: file " + t.fileName + " is unique (any case)");
    names[t.name] = files[t.fileName.toLowerCase()] = true;
    check(/^OH-[A-Za-z0-9.-]+\.xdb$/.test(t.fileName), "ready set: " + t.fileName + " is an OH-*.xdb with a plain name");
    check(t.warnings.every(function (w) { return !/Zones are/.test(w); }), "ready set: " + t.name + " has no zone size warning");
    check(t.bytes[0] === 0xef && t.bytes[1] === 0xbb && t.bytes[2] === 0xbf, "ready set: " + t.name + " starts with a UTF-8 BOM");
    check(!/(^|[^\r])\n/.test(t.xml) && t.xml.indexOf("\r\n") > 0, "ready set: " + t.name + " uses CRLF line endings only");
  });
  check(all.length >= 16 && all.length <= 24, "ready set: " + all.length + " templates (16 to 24 wanted)");

  // Layouts: 2v2, 2v2v2, 3v3, 4v4 and free-for-all for 3, 4, 6 and 8, each in Normal and Rich.
  [[2, 2], [3, 2], [2, 3], [2, 4], [3, 1], [4, 1], [6, 1], [8, 1]].forEach(function (l) {
    ["normal", "rich"].forEach(function (loot) {
      var n = all.filter(function (t) { return t.choices.teams === l[0] && t.choices.perTeam === l[1] && (t.choices.loot || "normal") === loot; }).length;
      check(n >= 1, "ready set: layout " + l[0] + "x" + l[1] + " has a " + loot + " template");
    });
  });
  var withTowns = all.filter(function (t) { return t.choices.borderTowns || t.choices.teamTowns; });
  check(withTowns.length >= 4 && withTowns.length < all.length, "ready set: some templates have towns in the neutral zones, some do not");

  // The committed files are what the maker builds now (run make_templates.js to refresh them).
  var onDisk = fs.existsSync(MT.DEFAULT_OUT) ? fs.readdirSync(MT.DEFAULT_OUT).filter(function (f) { return /\.xdb$/i.test(f); }) : [];
  all.forEach(function (t) {
    var p = path.join(MT.DEFAULT_OUT, t.fileName);
    check(fs.existsSync(p) && fs.readFileSync(p).equals(t.bytes), "ready set: src/mods/rmg/templates/" + t.fileName + " is up to date");
  });
  onDisk.forEach(function (f) { check(files[f.toLowerCase()], "ready set: src/mods/rmg/templates/" + f + " is in the ready set"); });
})();

// A deterministic random generator (xorshift32), so a failure can be
// reproduced. (Until 2026-10-25 this was "seed = (seed * 1103515245 + 12345) &
// 0x7fffffff; seed % n": its low bits barely change, so nearly all of its 400
// cases were free-for-all games with poor loot and no underground.)
function randomFrom(seed) {
  var s = seed | 0;
  return function (n) { s ^= s << 13; s ^= s >>> 17; s ^= s << 5; return (s >>> 0) % n; };
}

// Random choices.
var rnd = randomFrom(12345);
var loots = Object.keys(TM.LOOT), guards = Object.keys(TM.GUARDS);
for (var i = 0; i < 400; i++) {
  var perTeam = 1 + rnd(4), teams = 2 + rnd(Math.max(1, Math.floor(8 / perTeam) - 1));
  if (teams * perTeam > 8) continue;
  verify({
    teams: teams, perTeam: perTeam, teamZones: rnd(3), borderZones: rnd(3), centreZones: rnd(3),
    borderTowns: rnd(2) === 1, teamTowns: rnd(2) === 1, allyLink: rnd(2) ? "weak" : "open",
    startMines: rnd(2) ? "all" : "woodore", loot: loots[rnd(loots.length)], guards: guards[rnd(guards.length)],
    lootByDistance: { "0": rnd(2) ? loots[rnd(loots.length)] : "", "2": loots[rnd(loots.length)] },
    guardsByDistance: { "1": rnd(2) ? guards[rnd(guards.length)] : "", "3": guards[rnd(guards.length)] },
    underground: rnd(2) === 1
  }, "random " + i + " (" + teams + "x" + perTeam + ")");
}

// The wider random choices of the C++ parity test (below) pass the same checks:
// they also reach 3 team zones, 4 border zones, 3 centre zones and every map size.
parityChoices().forEach(function (c, i) { verify(c, "parity " + (i + 1) + " (" + c.teams + "x" + c.perTeam + ")"); });

// Loot and guards by distance from the bases.
(function () {
  var base = { teams: 3, perTeam: 2, teamZones: 1, borderZones: 3, centreZones: 1 };
  var plain = TM.makeTemplate(base);
  var tiered = TM.makeTemplate(Object.assign({}, base, {
    lootByDistance: { "1": "poor", "3": "veryrich" }, guardsByDistance: { "2": "strong" } }));
  var dist = tiered.graph.distance;
  check(dist.indexOf(0) === 0 && Math.max.apply(null, dist) >= 2, "tiers: distances computed");
  var expect = { "1": TM.LOOT.poor.value, "3": TM.LOOT.veryrich.value };
  tiered.template.Zones.forEach(function (z, i) {
    var p = plain.template.Zones[i].TreasureBlocksTotalValue, t = z.TreasureBlocksTotalValue;
    var k = String(Math.min(dist[i], 3)), f = expect[k];
    if (f) check(Math.abs(t - Math.round(p * f / 1000) * 1000) <= 1000, "tiers: zone " + (i + 1) + " loot follows distance " + k);
    else check(t === p, "tiers: zone " + (i + 1) + " (distance " + k + ") keeps the overall loot");
  });
  tiered.template.Connections.forEach(function (c, i) {
    var d = Math.min(Math.max(dist[c.SourceZoneIndex - 1], dist[c.DestZoneIndex - 1]), 3);
    var p = plain.template.Connections[i].GuardStrenght;
    if (c.kind === "ally" || d !== 2) check(c.GuardStrenght === p, "tiers: path " + i + " keeps its guard");
    else check(c.GuardStrenght === Math.max(1, Math.round(p * TM.GUARDS.strong.scale)), "tiers: path " + i + " into distance 2 is strong");
  });
  check(/-Tiers$/.test(tiered.name) && !/Tiers/.test(plain.name), "tiers: the name says so");
  var counts = tiered.stats.byDistance;
  check(counts[0] === 6 && counts.reduce(function (a, b) { return a + b; }, 0) === tiered.stats.zones, "tiers: zone counts by distance");
})();

// Too many players is refused.
var refused = false;
try { TM.makeTemplate({ teams: 3, perTeam: 3 }); } catch (e) { refused = true; }
check(refused, "9 players refused");

// --- Expected files for the C++ maker's parity test ---------------------------

// Random choices over every value the page and the recipe names (OH-X1-...)
// can give, from their own fixed seed: the same 400 cases on every run.
function parityChoices() {
  var r = randomFrom(20261025);
  var sizes = ["auto"].concat(TM.SIZES.map(function (z) { return z.key; }));
  var list = [];
  for (var i = 0; i < 400; i++) {
    var perTeam = 1 + r(4), teams = 2 + r(Math.floor(8 / perTeam) - 1);
    var lootBy = {}, guardsBy = {};
    for (var k = 0; k < 4; k++) lootBy[k] = r(2) ? "" : loots[r(loots.length)];
    for (k = 1; k < 4; k++) guardsBy[k] = r(2) ? "" : guards[r(guards.length)];
    if (r(10) === 0) guardsBy[0] = guards[r(guards.length)];  // not on the page, but the maker takes it
    list.push({
      teams: teams, perTeam: perTeam, teamZones: r(4), borderZones: r(5), centreZones: r(4),
      borderTowns: r(2) === 1, teamTowns: r(2) === 1, allyLink: r(2) ? "weak" : "open",
      startMines: r(2) ? "all" : "woodore", loot: loots[r(loots.length)], guards: guards[r(guards.length)],
      lootByDistance: lootBy, guardsByDistance: guardsBy,
      size: r(2) ? "auto" : sizes[1 + r(sizes.length - 1)], underground: r(2) === 1
    });
  }
  return list;
}

// Cases worth a look of their own, and choices the maker must refuse (the C++
// maker must give the same message).
var SPECIAL = [
  {},  // the maker's defaults
  { name: "Tom & Jerry's <best> map *" },
  { name: "Été à Paris — café" },  // UTF-8 in the name, and a run of non-ASCII in the file name
  { name: "OH-2v2v2-Lu-Normal*" },
  { teams: 2, perTeam: 1, teamZones: 0, borderZones: 0, centreZones: 0 },  // 2 zones: very big zones
  { teams: 8, perTeam: 1, teamZones: 3, borderZones: 4, centreZones: 3 },  // 67 zones: small zones
  { teams: 2, perTeam: 3, borderZones: 0, guardsByDistance: { "0": "strong" } },  // start-to-start paths at distance 0
  { teams: 2, perTeam: 1, borderZones: 0, teamZones: 0 },  // the same link on both sides, kept once
  { loot: "rich", lootByDistance: { "1": "rich" }, guardsByDistance: { "2": "normal" } },  // tiers equal to the overall: no "Tiers"
  { teamZones: 0, teamTowns: true, borderZones: 0, borderTowns: true },  // "Towns" in the name with no zone to hold one
  { loot: "veryrich", lootByDistance: { "3": "veryrich" }, size: "I", underground: false },
  { teams: 1 }, { perTeam: 0 }, { teams: 3, perTeam: 3 }, { teams: 2, perTeam: 5 },
  { loot: "" }, { loot: "gold" }, { guards: "Strong" }, { lootByDistance: { "2": "lots" } },
  { guardsByDistance: { "1": "none" } }, { size: "xl" }, { size: "" }, { teams: 9, loot: "bad" }
];

// Writes <dir>/cases.txt and one .xdb per case. cases.txt has one line per
// case, fields separated by tabs:
//   id=<id>  then  file=<file in dir>  or  error=<hex of the message>
//   c.<choice>=<value>   only the choices the case sets (the others are the
//                        maker's defaults); names are hex of their UTF-8
//   out.<field>=<hex>    name, fileName, warnings (one per line) and stats
// Hex keeps the format trivial to read from C++.
function writeExpected(dir) {
  function hex(text) { return Buffer.from(text, "utf8").toString("hex"); }
  function choiceFields(c) {
    var f = [];
    Object.keys(c).forEach(function (k) {
      var v = c[k];
      if (k === "lootByDistance" || k === "guardsByDistance")
        Object.keys(v).forEach(function (d) { f.push("c." + k + "." + d + "=" + v[d]); });
      else f.push("c." + k + "=" + (k === "name" ? hex(v) : String(v)));
    });
    return f;
  }
  fs.rmSync(dir, { recursive: true, force: true });
  fs.mkdirSync(dir, { recursive: true });
  var cases = [];
  READY.forEach(function (c, i) { cases.push({ id: "ready-" + (i + 1), choices: c }); });
  parityChoices().forEach(function (c, i) { cases.push({ id: "random-" + (i + 1), choices: c }); });
  SPECIAL.forEach(function (c, i) { cases.push({ id: "special-" + (i + 1), choices: c }); });
  var lines = [], built = 0, refused = 0;
  cases.forEach(function (k) {
    var f = ["id=" + k.id], t;
    try { t = TM.makeTemplate(k.choices); } catch (e) { f.push("error=" + hex(e.message)); refused++; }
    if (t) {
      var file = k.id + ".xdb", s = t.stats;
      fs.writeFileSync(path.join(dir, file), Buffer.from(t.xml, "utf8"));
      f.push("file=" + file);
      built++;
    }
    f = f.concat(choiceFields(k.choices));
    if (t) {
      f.push("out.name=" + hex(t.name), "out.fileName=" + hex(t.fileName), "out.warnings=" + hex(t.warnings.join("\n")),
        "out.stats=" + hex([s.players, s.zones, s.size, s.tilesPerZone, s.connections, s.neutralZones].concat(s.byDistance).join("|")));
    }
    lines.push(f.join("\t"));
  });
  fs.writeFileSync(path.join(dir, "cases.txt"), lines.join("\n") + "\n");
  console.log("expected files: %d templates and %d refused choices written to %s", built, refused, dir);
}

var at = process.argv.indexOf("--expected");
if (at > 0) {
  if (!process.argv[at + 1]) { console.log("--expected needs a folder"); process.exit(2); }
  writeExpected(process.argv[at + 1]);
}

console.log("%d of %d checks passed", checks - failures, checks);
process.exit(failures ? 1 : 0);
