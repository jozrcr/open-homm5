#include "hooks.h"

#include <windows.h>

#include "log.h"

namespace hooks {

void InstallAiTrace();

void InstallAll()
{
	// Escape hatch for testing: an empty file "open-homm5.nohooks" next to the
	// exe keeps the DLL in forwarding-only mode.
	wchar_t path[MAX_PATH];
	DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
	while (n > 0 && path[n - 1] != L'\\')
		--n;
	lstrcpynW(path + n, L"open-homm5.nohooks", MAX_PATH - n);
	if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {
		hooklog::Write("open-homm5.nohooks found: hooks disabled");
		return;
	}
	InstallAiTrace();
}

}  // namespace hooks
