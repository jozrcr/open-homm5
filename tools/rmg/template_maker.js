// open-homm5 template maker: builds random map templates (NDb::SRMGTemplate XML)
// for the game's own generator from a few choices. Used by the page
// (template-maker.html) and by make_templates.js (the ready templates).
// See docs/plans/map-generator.md and docs/subsystems/rmg.md.
//
// Layout: teams sit around a circle.
//   - Each team: its players' start zones (linked to each other by a weak guard)
//     and optionally its own treasure zones behind them.
//   - Between two neighbouring teams: "border" zones, where they meet.
//   - In the middle: optional rich treasure zones behind strong guards.
// Zone contents start from the values of MMH5.5's balanced templates
// (start zone = 6P-Triangle zone 1, border = zone 7, centre = zone 13,
// team zone = 4P-Team-Mu zone 5) and are scaled by the loot and guard levels.
(function (root) {
  "use strict";

  // Map sizes of the generator screen (side in tiles; .rdata 0xe556ac) and the
  // letters MMH5.5's template names use for them.
  var SIZES = [
    { key: "T", name: "Tiny", side: 72 },
    { key: "S", name: "Small", side: 96 },
    { key: "M", name: "Medium", side: 136 },
    { key: "L", name: "Large", side: 176 },
    { key: "XL", name: "Extra large", side: 216 },
    { key: "H", name: "Huge", side: 256 },
    { key: "I", name: "Impossible", side: 320 }
  ];

  // Tiles per zone that MMH5.5's templates are tuned for (its ARMG manual:
  // about 5,000-6,600; zones far bigger make the generator drop artifacts).
  var ZONE_AREA = { min: 4000, ideal: 5800, max: 7500 };

  var LOOT = {
    poor: { label: "Poor", value: 0.6, points: 0.7 },
    normal: { label: "Normal", value: 1.0, points: 1.0 },
    rich: { label: "Rich", value: 1.35, points: 1.25 },
    veryrich: { label: "Very rich", value: 1.7, points: 1.5 }
  };
  var GUARDS = {
    weak: { label: "Weak", scale: 0.7 },
    normal: { label: "Normal", scale: 1.0 },
    strong: { label: "Strong", scale: 1.3 }
  };
  // The highest treasure value in any shipped template; above it is untested.
  var MAX_SHIPPED_VALUE = 252000;

  // Zone recipes. Field names and order as in the game's templates.
  var RECIPES = {
    start: {
      CanBePlayerStart: true, Town: true, TownGuardStrenght: 10,
      Mines: [1, 1, 1, 1, 1, 1, 0], AbandonedMines: 0, Dwellings: [1, 1, 1, 1, 0, 0, 0],
      UpgBuildingsDensity: 8, TreasureDensity: 8, TreasureChestDensity: 12, Prisons: 1,
      LandCartographer: 1, ShopPoints: 8, ShrinePoints: 40, LuckMoralBuildingsDensity: 22,
      ResourceBuildingsDensity: 0, TreasureBuildingPoints: 12, TreasureBlocksTotalValue: 56000
    },
    team: {
      CanBePlayerStart: false, Town: false, TownGuardStrenght: 10,
      Mines: [0, 0, 0, 0, 0, 0, 2], AbandonedMines: 0, Dwellings: [0, 0, 0, 0, 0, 0, 0],
      UpgBuildingsDensity: 52, TreasureDensity: 6, TreasureChestDensity: 8, Prisons: 0,
      LandCartographer: 1, ShopPoints: 8, ShrinePoints: 20, LuckMoralBuildingsDensity: 24,
      ResourceBuildingsDensity: 32, TreasureBuildingPoints: 48, TreasureBlocksTotalValue: 168000
    },
    border: {
      CanBePlayerStart: false, Town: false, TownGuardStrenght: 10,
      Mines: [0, 0, 0, 0, 0, 0, 0], AbandonedMines: 2, Dwellings: [0, 0, 0, 0, 1, 0, 0],
      UpgBuildingsDensity: 48, TreasureDensity: 8, TreasureChestDensity: 12, Prisons: 0,
      LandCartographer: 1, ShopPoints: 4, ShrinePoints: 20, LuckMoralBuildingsDensity: 24,
      ResourceBuildingsDensity: 0, TreasureBuildingPoints: 42, TreasureBlocksTotalValue: 148000
    },
    centre: {
      CanBePlayerStart: false, Town: false, TownGuardStrenght: 10,
      Mines: [0, 0, 0, 0, 0, 0, 1], AbandonedMines: 0, Dwellings: [0, 0, 0, 0, 0, 0, 0],
      UpgBuildingsDensity: 52, TreasureDensity: 8, TreasureChestDensity: 12, Prisons: 0,
      LandCartographer: 1, ShopPoints: 0, ShrinePoints: 0, LuckMoralBuildingsDensity: 24,
      ResourceBuildingsDensity: 40, TreasureBuildingPoints: 48, TreasureBlocksTotalValue: 252000
    }
  };
  var FIELD_ORDER = [
    "Index", "Setting", "CanBeWater", "Size", "CanBePlayerStart", "Town", "TownGuardStrenght",
    "Mines", "AbandonedMines", "Dwellings", "UpgBuildingsDensity", "TreasureDensity",
    "TreasureChestDensity", "Prisons", "LandCartographer", "ShopPoints", "ShrinePoints",
    "LuckMoralBuildingsDensity", "ResourceBuildingsDensity", "TreasureBuildingPoints",
    "TreasureBlocksTotalValue", "DenOfThieves", "RedwoodObservatoryDensity", "BuffPoints"
  ];
  // Guard strengths of MMH5.5's templates, by kind of connection.
  var GUARD = { ally: 2, startTeam: 6, startBorder: 6, teamBorder: 8, borderBorder: 9, centre: 12, centreCentre: 10 };

  var DEFAULTS = {
    teams: 3,            // number of teams (1 player per team = free-for-all)
    perTeam: 2,          // players per team
    teamZones: 1,        // treasure zones per team, behind its players
    borderZones: 1,      // neutral zones between two neighbouring teams
    centreZones: 1,      // rich zones in the middle
    borderTowns: false,  // a town in each border zone
    teamTowns: false,    // a town in each team zone
    allyLink: "weak",    // "weak" guard or "open" between teammates
    startMines: "all",   // "all" six resources, or "woodore" only
    loot: "normal",
    guards: "normal",
    // Loot and guards by distance from the bases: keys 0 (start zones), 1 (next
    // to a base), 2 (two steps away), 3 (three or more); a missing key uses
    // `loot` / `guards`. A path's guard follows the farther of its two zones.
    lootByDistance: {},
    guardsByDistance: {},
    size: "auto",        // a SIZES key, or "auto": the best fit
    underground: true,
    name: ""             // empty: built from the choices
  };

  function clone(o) { return JSON.parse(JSON.stringify(o)); }
  function round(n, step) { return Math.max(0, Math.round(n / step) * step); }

  function layoutName(o) {
    if (o.perTeam === 1) return o.teams + "P-FFA";
    var parts = [];
    for (var i = 0; i < o.teams; i++) parts.push(o.perTeam);
    return parts.join("v");
  }

  function area(size, underground) { return size.side * size.side * (underground ? 2 : 1); }

  // Build the zone graph: [{kind, team}] and [[a, b, guard kind]] (0-based).
  function graph(o) {
    var zones = [], links = [];
    function add(kind, team) { zones.push({ kind: kind, team: team }); return zones.length - 1; }
    var starts = [], teamZones = [], borders = [], centres = [];
    // Start zones first and teammates next to each other (1+2, 3+4, ...), as in
    // MMH5.5's team templates; whether the game hands out start zones in this
    // order is still to be tested (docs/subsystems/rmg.md).
    for (var t = 0; t < o.teams; t++) {
      starts.push([]);
      for (var p = 0; p < o.perTeam; p++) starts[t].push(add("start", t));
    }
    for (t = 0; t < o.teams; t++) {
      teamZones.push([]);
      for (var z = 0; z < o.teamZones; z++) teamZones[t].push(add("team", t));
    }
    // Two teams meet on two sides; three or more meet once per neighbour pair.
    var sides = o.teams === 2 ? 2 : o.teams;
    for (var s = 0; s < sides; s++) {
      borders.push([]);
      for (z = 0; z < o.borderZones; z++) borders[s].push(add("border", -1));
    }
    for (z = 0; z < o.centreZones; z++) centres.push(add("centre", -1));

    for (t = 0; t < o.teams; t++) {
      var st = starts[t], tz = teamZones[t];
      for (p = 1; p < st.length; p++) links.push([st[p - 1], st[p], "ally"]);
      // Each team zone is reached from its share of the team's players.
      for (z = 0; z < tz.length; z++) {
        for (p = 0; p < st.length; p++)
          if (tz.length === 1 || p % tz.length === z) links.push([st[p], tz[z], "startTeam"]);
        if (z > 0) links.push([tz[z - 1], tz[z], "startTeam"]);
      }
    }
    // Side s lies between team s (its last player) and team s+1 (its first).
    for (s = 0; s < sides; s++) {
      var a = s % o.teams, b = (s + 1) % o.teams;
      var left = starts[a][starts[a].length - 1], right = starts[b][0];
      var row = borders[s];
      if (!row.length) { links.push([left, right, "startBorder"]); continue; }
      links.push([left, row[0], "startBorder"]);
      for (z = 1; z < row.length; z++) links.push([row[z - 1], row[z], "borderBorder"]);
      links.push([row[row.length - 1], right, "startBorder"]);
    }
    // The centre is reached from each team's treasure zones (or its players).
    for (z = 0; z < centres.length; z++) {
      for (t = 0; t < o.teams; t++) {
        var from = teamZones[t].length ? teamZones[t] : starts[t];
        links.push([from[(z + t) % from.length], centres[z], "centre"]);
      }
      if (z > 0) links.push([centres[z - 1], centres[z], "centreCentre"]);
    }
    // Two teams with no border zones would get the same link on both sides.
    var seen = {};
    links = links.filter(function (l) {
      var key = Math.min(l[0], l[1]) + "-" + Math.max(l[0], l[1]);
      if (seen[key]) return false;
      seen[key] = true;
      return true;
    });
    return { zones: zones, links: links };
  }

  // Steps from each zone to the nearest start zone (0 for start zones).
  function distances(g) {
    var adj = g.zones.map(function () { return []; });
    g.links.forEach(function (l) { adj[l[0]].push(l[1]); adj[l[1]].push(l[0]); });
    var dist = g.zones.map(function (z) { return z.kind === "start" ? 0 : Infinity; });
    var todo = [];
    dist.forEach(function (d, i) { if (d === 0) todo.push(i); });
    while (todo.length) {
      var a = todo.shift();
      adj[a].forEach(function (b) { if (dist[b] > dist[a] + 1) { dist[b] = dist[a] + 1; todo.push(b); } });
    }
    return dist;
  }
  function tierOf(d) { return String(Math.min(d, 3)); }

  function zoneFields(kind, index, o, dwellingTier, lootKey) {
    var r = clone(RECIPES[kind]);
    var loot = LOOT[lootKey];
    r.Index = index;
    r.Setting = "RACE_RANDOM_TYPE";
    r.CanBeWater = false;
    r.Size = 10;
    if (kind === "start" && o.startMines === "woodore") r.Mines = [1, 1, 0, 0, 0, 0, 0];
    if (kind === "border") {
      r.Town = !!o.borderTowns;
      r.Dwellings = [0, 0, 0, 0, 0, 0, 0];
      r.Dwellings[4 + dwellingTier % 3] = 1;  // tiers 5, 6, 7 in turn, as MMH5.5 does
    }
    if (kind === "team") r.Town = !!o.teamTowns;
    r.TreasureBlocksTotalValue = round(r.TreasureBlocksTotalValue * loot.value, 1000);
    r.TreasureBuildingPoints = round(r.TreasureBuildingPoints * loot.points, 1);
    r.ShrinePoints = round(r.ShrinePoints * loot.points, 1);
    r.DenOfThieves = 0;
    r.RedwoodObservatoryDensity = 0;
    r.BuffPoints = 0;
    return r;
  }

  function guardValue(kind, o, guardKey) {
    if (kind === "ally" && o.allyLink === "open") return 0;
    var g = GUARD[kind];
    return kind === "ally" ? g : Math.max(1, Math.round(g * GUARDS[guardKey].scale));
  }

  function chooseSize(o, zoneCount) {
    var best = null;
    for (var i = 0; i < SIZES.length; i++) {
      var per = area(SIZES[i], o.underground) / zoneCount;
      var miss = Math.abs(Math.log(per / ZONE_AREA.ideal));
      if (o.size === SIZES[i].key) return SIZES[i];
      if (o.size === "auto" && (!best || miss < best.miss)) best = { size: SIZES[i], miss: miss };
    }
    if (!best) throw new Error("unknown map size " + o.size);
    return best.size;
  }

  function xmlValue(v) {
    return String(v).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
  }

  // The template's XML, laid out like the game's own files (UTF-8 with BOM).
  function toXml(t) {
    var out = ["\ufeff<?xml version=\"1.0\" encoding=\"UTF-8\"?>", "<RMGTemplate>",
      "  <Editor name=\"open-homm5 template maker\" version=\"1\" />",
      "  <NameFileRef href=\"\" />", "  <DescriptionFileRef href=\"\" />",
      "  <Name>" + xmlValue(t.Name) + "</Name>", "  <Zones>"];
    t.Zones.forEach(function (z) {
      out.push("    <Item>");
      FIELD_ORDER.forEach(function (f) {
        var v = z[f];
        if (Array.isArray(v)) {
          out.push("      <" + f + ">");
          v.forEach(function (x) { out.push("        <Item>" + x + "</Item>"); });
          out.push("      </" + f + ">");
        } else {
          out.push("      <" + f + ">" + xmlValue(v) + "</" + f + ">");
        }
      });
      out.push("    </Item>");
    });
    out.push("  </Zones>", "  <Connections>");
    t.Connections.forEach(function (c) {
      out.push("    <Item>",
        "      <SourceZoneIndex>" + c.SourceZoneIndex + "</SourceZoneIndex>",
        "      <DestZoneIndex>" + c.DestZoneIndex + "</DestZoneIndex>",
        "      <TwoWay>true</TwoWay>",
        "      <GuardStrenght>" + c.GuardStrenght + "</GuardStrenght>",
        "      <Guarded>" + (c.Guarded ? "true" : "false") + "</Guarded>",
        "      <Wide>false</Wide>",
        "    </Item>");
    });
    out.push("  </Connections>",
      "  <GraalOnMap>false</GraalOnMap>",
      "  <MinPlayers>" + t.MinPlayers + "</MinPlayers>",
      "  <MaxPlayers>" + t.MaxPlayers + "</MaxPlayers>",
      "  <MinMapSize>" + t.MinMapSize + "</MinMapSize>",
      "  <MaxMapSize>" + t.MaxMapSize + "</MaxMapSize>",
      "  <Underground>" + (t.Underground ? "true" : "false") + "</Underground>",
      "  <TestTemplate>false</TestTemplate>",
      "</RMGTemplate>", "");
    return out.join("\r\n");
  }

  // options -> { name, fileName, xml, template, stats, warnings, graph }
  function makeTemplate(options) {
    var o = Object.assign({}, DEFAULTS, options || {});
    var warnings = [];
    if (!(o.teams >= 2 && o.perTeam >= 1)) throw new Error("need at least 2 teams (1 player per team = free-for-all)");
    var players = o.teams * o.perTeam;
    if (players > 8) throw new Error("the game allows at most 8 players (" + players + " asked)");
    if (!LOOT[o.loot]) throw new Error("unknown loot level " + o.loot);
    if (!GUARDS[o.guards]) throw new Error("unknown guard level " + o.guards);
    var lootBy = o.lootByDistance || {}, guardsBy = o.guardsByDistance || {};
    Object.keys(lootBy).forEach(function (k) { if (lootBy[k] && !LOOT[lootBy[k]]) throw new Error("unknown loot level " + lootBy[k]); });
    Object.keys(guardsBy).forEach(function (k) { if (guardsBy[k] && !GUARDS[guardsBy[k]]) throw new Error("unknown guard level " + guardsBy[k]); });
    function lootAt(d) { return lootBy[tierOf(d)] || o.loot; }
    function guardsAt(d) { return guardsBy[tierOf(d)] || o.guards; }

    var g = graph(o);
    var dist = distances(g);
    g.distance = dist;
    var size = chooseSize(o, g.zones.length);
    var mapArea = area(size, o.underground);
    var perZone = Math.round(mapArea / g.zones.length);
    if (perZone < ZONE_AREA.min)
      warnings.push("Zones are small (" + perZone + " tiles each): pick a bigger map or fewer zones.");
    if (perZone > ZONE_AREA.max)
      warnings.push("Zones are very big (" + perZone + " tiles each): the generator may leave them empty; pick a smaller map or more zones.");

    var tier = 0;
    var zones = g.zones.map(function (z, i) {
      return zoneFields(z.kind, i + 1, o, z.kind === "border" ? tier++ : 0, lootAt(dist[i]));
    });
    var richest = Math.max.apply(null, zones.map(function (z) { return z.TreasureBlocksTotalValue; }));
    if (richest > MAX_SHIPPED_VALUE)
      warnings.push("Some zones hold more treasure (" + richest + ") than any shipped template (" +
        MAX_SHIPPED_VALUE + "); not tested in the game yet.");
    var connections = g.links.map(function (l) {
      var guard = guardValue(l[2], o, guardsAt(Math.max(dist[l[0]], dist[l[1]])));
      return { SourceZoneIndex: l[0] + 1, DestZoneIndex: l[1] + 1, GuardStrenght: guard, Guarded: guard > 0, kind: l[2] };
    });

    var tiered = ["0", "1", "2", "3"].some(function (k) {
      return (lootBy[k] && lootBy[k] !== o.loot) || (guardsBy[k] && guardsBy[k] !== o.guards);
    });
    var thousands = mapArea / 1000;
    var name = o.name || ["OH", layoutName(o), size.key + (o.underground ? "u" : ""), LOOT[o.loot].label.replace(" ", ""),
      (o.borderTowns || o.teamTowns) ? "Towns" : null, tiered ? "Tiers" : null].filter(Boolean).join("-");
    var byDistance = [0, 0, 0, 0];
    dist.forEach(function (d) { byDistance[Math.min(d, 3)]++; });
    var template = {
      Name: name, Zones: zones, Connections: connections,
      MinPlayers: 2, MaxPlayers: players,
      MinMapSize: Math.floor(thousands * 0.97), MaxMapSize: Math.ceil(thousands * 1.03),
      Underground: !!o.underground
    };
    return {
      name: name,
      fileName: name.replace(/[^A-Za-z0-9.-]+/g, "_") + ".xdb",
      xml: toXml(template),
      template: template,
      graph: g,
      warnings: warnings,
      stats: {
        players: players, zones: zones.length, size: size.name + " (" + size.side + "x" + size.side +
          (o.underground ? ", with underground" : "") + ")",
        tilesPerZone: perZone, connections: connections.length,
        neutralZones: zones.length - players,
        byDistance: byDistance   // zone counts at 0, 1, 2 and 3+ steps from a base
      }
    };
  }

  var api = { makeTemplate: makeTemplate, SIZES: SIZES, LOOT: LOOT, GUARDS: GUARDS, DEFAULTS: DEFAULTS, ZONE_AREA: ZONE_AREA };
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  else root.TemplateMaker = api;
})(this);
