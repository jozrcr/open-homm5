// Checks the template maker on the ready templates and on many random choices:
//   node tools/rmg/test_template_maker.js
// The XML must be well formed, every zone reachable, start zones = players,
// teammates linked, indexes valid, and the map size must give sensible zones.
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

// Random choices (deterministic generator, so a failure can be reproduced).
var seed = 12345;
function rnd(n) { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed % n; }
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

console.log("%d of %d checks passed", checks - failures, checks);
process.exit(failures ? 1 : 0);
