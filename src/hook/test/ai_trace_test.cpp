// Test the AI trace hook's calling convention without the game.
//
// The game calls the AI turn driver as  ai->slot0(&commands, flag)  with
// __thiscall (this in ecx, two stack arguments, callee pops 8 bytes). We call
// the hook exactly like that and check that (1) the original function receives
// the same this/arguments and (2) the log line has the right command count and
// flag. Built with /RTCs: if the hook popped the wrong number of bytes, the
// compiler's runtime check stops the test ("ESP was not properly saved").
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "../log.h"

namespace hooks {
void *AiTraceHookForTest(void *fake_driver);
}

namespace {

struct Vector {
	void **begin, **end, **capacity;
};

void *g_seen_self, *g_seen_commands;
int g_seen_flag;
void *g_items[4];

// Stand-in for 0x0086dd40: same convention (__thiscall, 2 stack arguments).
void __fastcall FakeDriver(void *self, void *, void *commands, int flag)
{
	g_seen_self = self;
	g_seen_commands = commands;
	g_seen_flag = flag;
	Vector *v = static_cast<Vector *>(commands);
	v->begin = g_items;
	v->end = g_items + 2;  // "the AI produced 2 commands"
	*reinterpret_cast<DWORD *>(static_cast<char *>(self) + 0x128) = 4;  // stage
}

typedef void(__thiscall *GameCallFn)(void *self, void *commands, int flag);

}  // namespace

int main()
{
	hooklog::Open(nullptr);
	GameCallFn hook = reinterpret_cast<GameCallFn>(hooks::AiTraceHookForTest(reinterpret_cast<void *>(&FakeDriver)));

	static char ai[0x148];
	*reinterpret_cast<DWORD *>(ai + 0x128) = 3;
	Vector commands = {};

	hook(ai, &commands, 1);
	hooklog::Close();

	int failures = 0;
	if (g_seen_self != ai) { printf("FAIL: this not passed through\n"); ++failures; }
	if (g_seen_commands != &commands) { printf("FAIL: command list not passed through\n"); ++failures; }
	if (g_seen_flag != 1) { printf("FAIL: flag not passed through\n"); ++failures; }

	FILE *f = nullptr;
	char line[512] = {};
	if (fopen_s(&f, "open-homm5.log", "r") == 0 && f) {
		while (fgets(line, sizeof(line), f)) {
			if (strstr(line, "ai-step"))
				break;
		}
		fclose(f);
	}
	if (!strstr(line, "cmds=2") || !strstr(line, "flag=1") || !strstr(line, "stage 3->4")) {
		printf("FAIL: unexpected log line: %s\n", line);
		++failures;
	}
	if (failures)
		return 1;
	printf("OK: hook keeps the game's calling convention; log: %s", strstr(line, "ai-step"));
	return 0;
}
