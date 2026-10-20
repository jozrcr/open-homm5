// Tiny thread-safe logger: open-homm5.log next to the game exe.
#pragma once
#include <windows.h>

namespace hooklog {

void Open(HMODULE self);
void Close();
// printf-style; adds a timestamp (ms since the DLL loaded) and the thread id.
void Write(const char *fmt, ...);

}  // namespace hooklog
