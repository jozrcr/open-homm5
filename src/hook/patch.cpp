#include "patch.h"

namespace hookpatch {
namespace {

struct Record {
	DWORD site;
	DWORD original;
	DWORD replacement;
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
		if (g_records[i].site == site && g_records[i].original == original && g_records[i].replacement == target)
			return true;
	return false;
}

}  // namespace

DWORD RedirectCalls(const DWORD *sites, int count, DWORD original, const void *replacement)
{
	if (count <= 0 || g_count + count > kMaxRecords)
		return 0;
	const DWORD previous = CallTarget(sites[0]);
	for (int i = 0; i < count; ++i) {
		const DWORD target = CallTarget(sites[i]);
		if (!target || target != previous || (target != original && !HookedOver(sites[i], original, target)))
			return 0;
	}
	for (int i = 0; i < count; ++i) {
		BYTE *p = reinterpret_cast<BYTE *>(sites[i]);
		DWORD old;
		if (!VirtualProtect(p + 1, 4, PAGE_EXECUTE_READWRITE, &old))
			continue;  // that site keeps calling `previous` directly, which is fine
		*reinterpret_cast<LONG *>(p + 1) = static_cast<LONG>(reinterpret_cast<DWORD>(replacement) - (sites[i] + 5));
		VirtualProtect(p + 1, 4, old, &old);
		FlushInstructionCache(GetCurrentProcess(), p, 5);
		g_records[g_count++] = {sites[i], original, reinterpret_cast<DWORD>(replacement)};
	}
	return previous;
}

DWORD PatchPointer(DWORD slot_address, DWORD original, const void *replacement)
{
	DWORD *slot = reinterpret_cast<DWORD *>(slot_address);
	const DWORD previous = *slot;
	if (g_count >= kMaxRecords || (previous != original && !HookedOver(slot_address, original, previous)))
		return 0;
	DWORD old;
	if (!VirtualProtect(slot, sizeof(DWORD), PAGE_READWRITE, &old))
		return 0;
	*slot = reinterpret_cast<DWORD>(replacement);
	VirtualProtect(slot, sizeof(DWORD), old, &old);
	g_records[g_count++] = {slot_address, original, reinterpret_cast<DWORD>(replacement)};
	return previous;
}

}  // namespace hookpatch
