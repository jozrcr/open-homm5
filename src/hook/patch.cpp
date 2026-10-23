#include "patch.h"

namespace hookpatch {
namespace {

struct Record {
	DWORD site;
	DWORD original;
	const void *replacement;
	DWORD previous;
};
const int kMaxRecords = 128;
Record g_records[kMaxRecords];
int g_count = 0;

DWORD CallTarget(DWORD site)
{
	const BYTE *p = reinterpret_cast<const BYTE *>(site);
	if (p[0] != 0xE8)
		return 0;
	return site + 5 + *reinterpret_cast<const LONG *>(p + 1);
}

bool HookedOver(DWORD site, DWORD original, DWORD target)
{
	for (int i = 0; i < g_count; ++i)
		if (g_records[i].site == site && g_records[i].original == original &&
		    reinterpret_cast<DWORD>(g_records[i].replacement) == target)
			return true;
	return false;
}

}  // namespace

DWORD RedirectCall(DWORD site, DWORD original, const void *replacement)
{
	const DWORD previous = CallTarget(site);
	if (!previous || (previous != original && !HookedOver(site, original, previous)) || g_count >= kMaxRecords)
		return 0;
	BYTE *p = reinterpret_cast<BYTE *>(site);
	DWORD old;
	if (!VirtualProtect(p + 1, 4, PAGE_EXECUTE_READWRITE, &old))
		return 0;
	*reinterpret_cast<LONG *>(p + 1) = static_cast<LONG>(reinterpret_cast<DWORD>(replacement) - (site + 5));
	VirtualProtect(p + 1, 4, old, &old);
	FlushInstructionCache(GetCurrentProcess(), p, 5);
	g_records[g_count++] = {site, original, replacement, previous};
	return previous;
}

DWORD PreviousTarget(DWORD site, const void *replacement)
{
	for (int i = g_count - 1; i >= 0; --i)
		if (g_records[i].site == site && g_records[i].replacement == replacement)
			return g_records[i].previous;
	return 0;
}

}  // namespace hookpatch
