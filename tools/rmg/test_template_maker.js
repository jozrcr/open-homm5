// Checks the template maker on the ready templates and on many random choices:
//   node tools/rmg/test_template_maker.js
// The XML must be well formed, every zone reachable, start zones = players,
// teammates linked, indexes valid, and the map size must give sensible zones.
"use strict";
var TM = require("./template_maker.js");
var READY = require("./make_templates.js").READY;

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
    underground: rnd(2) === 1
  }, "random " + i + " (" + teams + "x" + perTeam + ")");
}

// Too many players is refused.
var refused = false;
try { TM.makeTemplate({ teams: 3, perTeam: 3 }); } catch (e) { refused = true; }
check(refused, "9 players refused");

console.log("%d of %d checks passed", checks - failures, checks);
process.exit(failures ? 1 : 0);
