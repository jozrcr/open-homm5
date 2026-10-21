#include "game_build.h"

#include <windows.h>
#include <string.h>

namespace {

// Fingerprint of the vanilla 3.1 build (docs/reference-binaries.md). The exe has
// no ASLR, so it always loads at 0x400000 and these addresses are fixed.
const DWORD kImageBase = 0x00400000;
const DWORD kTimeDateStamp = 0x48469f9f;  // 2008-06-04 13:58:55 UTC
const DWORD kSizeOfImage = 0x00dd9000;         // unwrapped: reference exe, MMH55_Utility(_64)
const DWORD kSizeOfImageSteam = 0x00e2d000;    // Steam-wrapped H5_GameOLD.exe (extra .bind section)
const DWORD kCPlayerAIVftable = 0x00e4c8bc;  // slot 0 = AI turn driver
const DWORD kTurnDriver = 0x0086dd40;
const unsigned char kTurnDriverStart[] = {0x8b, 0x44, 0x24, 0x04, 0x81, 0xec, 0x9c, 0x00, 0x00, 0x00, 0x56, 0x50};
// One of MMH5.5's patches: je -> jmp at 0x81eeb7 (docs/reference-binaries.md).
const DWORD kMmh55PatchSite = 0x0081eeb7;

}  // namespace

GameBuild DetectGameBuild()
{
	HMODULE exe = GetModuleHandleW(nullptr);
	if (reinterpret_cast<DWORD>(exe) != kImageBase)
		return GameBuild::Unknown;
	const IMAGE_DOS_HEADER *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(exe);
	const IMAGE_NT_HEADERS32 *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(
		reinterpret_cast<const BYTE *>(exe) + dos->e_lfanew);
	const DWORD size = nt->OptionalHeader.SizeOfImage;
	if (nt->FileHeader.TimeDateStamp != kTimeDateStamp || (size != kSizeOfImage && size != kSizeOfImageSteam))
		return GameBuild::Unknown;
	if (*reinterpret_cast<const DWORD *>(kCPlayerAIVftable) != kTurnDriver)
		return GameBuild::Unknown;
	// Code bytes: also proves the Steam wrapper has decrypted .text by now.
	if (memcmp(reinterpret_cast<const void *>(kTurnDriver), kTurnDriverStart, sizeof(kTurnDriverStart)) != 0)
		return GameBuild::Unknown;
	const unsigned char patch = *reinterpret_cast<const unsigned char *>(kMmh55PatchSite);
	if (patch == 0x74)
		return GameBuild::Vanilla31;
	if (patch == 0xeb)
		return GameBuild::Mmh55Utility;
	return GameBuild::Unknown;
}

const char *GameBuildName(GameBuild build)
{
	switch (build) {
	case GameBuild::Vanilla31:
		return "vanilla ToE 3.1 (2008)";
	case GameBuild::Mmh55Utility:
		return "ToE 3.1 + MMH5.5 patches (MMH55_Utility)";
	default:
		return "unknown";
	}
}
