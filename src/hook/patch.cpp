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

// Import table entries already patched, with the function the loader had put
// there, so that an entry found by that function can be patched again.
struct Patched {
	IMAGE_THUNK_DATA32 *slot;
	DWORD function;
};
const int kMaxPatched = 32;
Patched g_patched[kMaxPatched];
int g_patched_count = 0;

// One import address table entry: the loader wrote the function's address there.
void *PatchSlot(IMAGE_THUNK_DATA32 *slot, void *replacement)
{
	DWORD old;
	if (!VirtualProtect(&slot->u1.Function, sizeof(DWORD), PAGE_READWRITE, &old))
		return nullptr;
	void *original = reinterpret_cast<void *>(slot->u1.Function);
	slot->u1.Function = reinterpret_cast<DWORD>(replacement);
	VirtualProtect(&slot->u1.Function, sizeof(DWORD), old, &old);
	bool known = false;
	for (int i = 0; i < g_patched_count; ++i)
		known = known || g_patched[i].slot == slot;
	if (!known && g_patched_count < kMaxPatched)
		g_patched[g_patched_count++] = {slot, reinterpret_cast<DWORD>(original)};
	return original;
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

// Replace one entry of the exe's import address table (the table of function
// pointers the loader filled in). Data, not code: nothing in the game is patched.
void *PatchImport(const char *dll, const char *function, void *replacement)
{
	BYTE *base = reinterpret_cast<BYTE *>(GetModuleHandleW(nullptr));
	const IMAGE_DOS_HEADER *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
	const IMAGE_NT_HEADERS32 *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(base + dos->e_lfanew);
	const IMAGE_DATA_DIRECTORY &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	const IMAGE_IMPORT_DESCRIPTOR *imp = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR *>(base + dir.VirtualAddress);
	for (; imp->Name; ++imp) {
		if (_stricmp(reinterpret_cast<const char *>(base + imp->Name), dll) != 0)
			continue;
		const IMAGE_THUNK_DATA32 *names = reinterpret_cast<const IMAGE_THUNK_DATA32 *>(base + imp->OriginalFirstThunk);
		IMAGE_THUNK_DATA32 *slots = reinterpret_cast<IMAGE_THUNK_DATA32 *>(base + imp->FirstThunk);
		for (; names->u1.AddressOfData; ++names, ++slots) {
			if (names->u1.Ordinal & IMAGE_ORDINAL_FLAG32)
				continue;
			const IMAGE_IMPORT_BY_NAME *by_name = reinterpret_cast<const IMAGE_IMPORT_BY_NAME *>(base + names->u1.AddressOfData);
			if (strcmp(reinterpret_cast<const char *>(by_name->Name), function) == 0)
				return PatchSlot(slots, replacement);
		}
		// Imported by number (WSOCK32.dll): find the slot holding the function's address.
		HMODULE module = GetModuleHandleA(dll);
		const DWORD address = module ? reinterpret_cast<DWORD>(GetProcAddress(module, function)) : 0;
		for (slots = reinterpret_cast<IMAGE_THUNK_DATA32 *>(base + imp->FirstThunk); address && slots->u1.Function;
		     ++slots) {
			if (slots->u1.Function == address)
				return PatchSlot(slots, replacement);
		}
		for (int i = 0; address && i < g_patched_count; ++i)  // patched before: stack on top
			if (g_patched[i].function == address)
				return PatchSlot(g_patched[i].slot, replacement);
	}
	return nullptr;
}

}  // namespace hookpatch
