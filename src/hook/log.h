// Tiny thread-safe logger: open-homm5.log next to the game exe.
#pragma once
#include <windows.h>

namespace hooklog {

// Path of a file next to the game exe: <exe folder>\<stem><ext>, or
// <stem>-<instance><ext> when the OPEN_HOMM5_INSTANCE environment variable is
// set (the robot sets it when it runs several copies of the game at once, so
// each copy gets its own log and robot config).
void PathNextToExe(const wchar_t *stem, const wchar_t *ext, wchar_t *out, DWORD size);

void Open(HMODULE self);
void Close();
// printf-style; adds a timestamp (ms since the DLL loaded) and the thread id.
void Write(const char *fmt, ...);

}  // namespace hooklog
