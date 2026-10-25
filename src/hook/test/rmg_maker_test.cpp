// Parity test of the C++ template maker (src/mods/rmg_maker.cpp) against the
// JavaScript one (tools/rmg/template_maker.js). No game needed.
//
// Node writes the expected files first (build.ps1 -Test does both):
//   node tools/rmg/test_template_maker.js --expected build/rmg_parity
//   rmg_maker_test.exe build/rmg_parity
// For every case, the C++ maker must build the same bytes, the same name, file
// name, warnings and numbers, or refuse with the same message.
#include <stdio.h>

#include <algorithm>
#include <string>
#include <vector>

#include "../../mods/rmg_maker.h"
#include "rmg_cases.h"

using mods::rmg::MakeTemplate;
using mods::rmg::Template;
using rmgtest::Case;

namespace {

int g_failures = 0;

void Fail(const std::string &id, const std::string &what)
{
	if (++g_failures <= 40)
		printf("FAIL: %s: %s\n", id.c_str(), what.c_str());
}

std::string Stats(const Template &t)
{
	const mods::rmg::Stats &s = t.stats;
	std::string out = std::to_string(s.players) + "|" + std::to_string(s.zones) + "|" + s.size + "|" +
	                  std::to_string(s.tiles_per_zone) + "|" + std::to_string(s.connections) + "|" +
	                  std::to_string(s.neutral_zones);
	for (int n : s.by_distance)
		out += "|" + std::to_string(n);
	return out;
}

std::string Warnings(const Template &t)
{
	std::string out;
	for (size_t i = 0; i < t.warnings.size(); i++)
		out += (i ? "\n" : "") + t.warnings[i];
	return out;
}

// Where two files first differ, with the line there.
std::string FirstDifference(const std::string &want, const std::string &got)
{
	size_t i = 0;
	while (i < want.size() && i < got.size() && want[i] == got[i])
		++i;
	const size_t nl = want.rfind('\n', i ? i - 1 : 0);
	const size_t start = nl == std::string::npos ? 0 : nl + 1;
	auto line_at = [start](const std::string &s) {
		const size_t end = s.find_first_of("\r\n", start);
		return start < s.size() ? s.substr(start, (std::min)(end == std::string::npos ? s.size() : end, start + 100) - start)
		                        : std::string();
	};
	return "bytes differ at " + std::to_string(i) + " (sizes " + std::to_string(want.size()) + " and " +
	       std::to_string(got.size()) + "): JavaScript \"" + line_at(want) + "\", C++ \"" + line_at(got) + "\"";
}

}  // namespace

int main(int argc, char **argv)
{
	if (argc < 2) {
		printf("usage: rmg_maker_test <folder written by test_template_maker.js --expected>\n");
		return 2;
	}
	const std::string dir = argv[1];
	std::vector<Case> cases;
	if (!rmgtest::LoadCases(dir, &cases)) {
		printf("FAIL: no %s/cases.txt (run node tools/rmg/test_template_maker.js --expected %s)\n", dir.c_str(), dir.c_str());
		return 1;
	}
	int built = 0, refused = 0, to_build = 0, to_refuse = 0, ready = 0, random = 0, special = 0;
	for (const Case &k : cases) {
		if (!k.unknown.empty()) {
			Fail(k.id, "unknown field " + k.unknown + " in cases.txt (a new choice in the JavaScript maker?)");
			continue;
		}
		ready += k.id.compare(0, 6, "ready-") == 0;
		random += k.id.compare(0, 7, "random-") == 0;
		special += k.id.compare(0, 8, "special-") == 0;
		Template t;
		std::string why;
		const bool ok = MakeTemplate(k.choices, &t, &why);
		if (k.refused) {
			++to_refuse;
			if (ok)
				Fail(k.id, "JavaScript refuses (" + k.error + "), C++ builds " + t.name);
			else if (why != k.error)
				Fail(k.id, "refused with \"" + why + "\", JavaScript says \"" + k.error + "\"");
			else
				++refused;
			continue;
		}
		++to_build;
		if (!ok) {
			Fail(k.id, "C++ refuses: " + why);
			continue;
		}
		std::string want;
		if (!rmgtest::ReadFile(dir + "/" + k.file, &want)) {
			Fail(k.id, "cannot read " + k.file);
			continue;
		}
		bool same = true;
		if (t.xml != want) {
			Fail(k.id, FirstDifference(want, t.xml));
			same = false;
		}
		if (t.name != k.name || t.file_name != k.file_name) {
			Fail(k.id, "name " + t.name + " / " + t.file_name + ", JavaScript " + k.name + " / " + k.file_name);
			same = false;
		}
		if (Warnings(t) != k.warnings) {
			Fail(k.id, "warnings \"" + Warnings(t) + "\", JavaScript \"" + k.warnings + "\"");
			same = false;
		}
		if (Stats(t) != k.stats) {
			Fail(k.id, "numbers " + Stats(t) + ", JavaScript " + k.stats);
			same = false;
		}
		built += same;
	}
	if (ready < 1 || random < 400) {
		printf("FAIL: only %d ready and %d random cases in %s/cases.txt\n", ready, random, dir.c_str());
		++g_failures;
	}
	printf("rmg maker parity: %d of %d templates equal to JavaScript's, byte for byte; %d of %d refusals with the same "
	       "message (cases: %d ready, %d random, %d special)\n",
	       built, to_build, refused, to_refuse, ready, random, special);
	printf(g_failures ? "rmg maker test: %d FAILED\n" : "rmg maker test: all passed\n", g_failures);
	return g_failures ? 1 : 0;
}
