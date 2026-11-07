// Mod: a wider range for the game's volume sliders
// (docs/plans/lobby-audio-classes.md, item 1).
//
// The options screen has two sliders, music and sounds, each with 11 steps from
// 0 to 1 (UI/uiconsts.(UIGameConstsH5).xdb, console variables
// sound_musicvolume and sound_sfxvolume). They are straight: the lowest step
// that is not silent, 0.1, is a tenth of the sound wave's size, only 20 dB
// below full volume, and players found it still too loud.
//
// What the game does (vanilla 3.1, also MMH55_Utility): when a slider changes,
// the value times 255 is stored as an int at +0x18 of the sound effects'
// manager (global 0x010dd560) or of the music system (global 0x010dd564), and
// every playing channel is updated:
//   - a sound effect: 0x00d042e0 multiplies the sound's own volume, its group's
//     volume and a per-sound factor (0x00d043c0 for 2D sounds: the sounds
//     slider; 0x00d045b0 for 3D sounds: the sounds slider times
//     sound_sfx3Dvolume), then calls 0x00b26e80 at 0x00d0439b;
//   - a music stream: 0x00d04710 multiplies the music slider by the stream's
//     own volume, then calls 0x00b26e80 at 0x00d047ac.
// 0x00b26e80 (__thiscall(channel, float 0..1), ret 4) turns the float into
// FMOD's 0..255 and calls FSOUND_SetVolume. It is FSOUND_SetVolume's only
// caller, and these two calls are its only callers.
//
// With [audio] volume_curve=wide we redirect those two calls: the volume gets
// one more factor of the slider's stored value (audiovol::Wide), so a slider
// value s counts as s squared. Nothing stored changes, so every place that
// saves and restores a volume (the adventure map's dialog screen, cutscenes,
// the profile) keeps working as before, and the slider and the profile keep
// their values: turning the setting off gives back exactly the game's sound.
// Sound is not part of the game world: each player may choose.
//
// Settings, h5-together.ini [audio]:
//   volume_curve=game   game: the game's straight sliders (nothing installed)
//                       wide: each slider step about the same number of
//                       decibels; the lowest step is 10 times quieter
//   log_volume=0        testing: log the first N audible channel volumes set (the
//                       slider's value, the game's volume and ours)
#include "audio_volume.h"

#include <windows.h>

#include "../hook/log.h"
#include "../hook/patch.h"

namespace mods {
namespace {

// ---- the game (vanilla 3.1 layout, also MMH55_Utility) ----

const DWORD kChannelVolume = 0x00b26e80;  // __thiscall(channel, float 0..1), ret 4: FSOUND_SetVolume
const DWORD kSoundSite = 0x00d0439b;      // call kChannelVolume, in 0x00d042e0 (a sound effect)
const DWORD kMusicSite = 0x00d047ac;      // call kChannelVolume, in 0x00d04710 (a music stream)
const DWORD kSoundManager = 0x010dd560;   // global pointer: sound effects' manager, +0x18 the sounds slider (0..255)
const DWORD kMusicSystem = 0x010dd564;    // global pointer: music system, +0x18 the music slider (0..255)
const DWORD kSliderOffset = 0x18;

typedef void(__fastcall *ChannelVolumeFn)(void *channel, void *edx, float volume);
ChannelVolumeFn g_sound_next = nullptr;
ChannelVolumeFn g_music_next = nullptr;

LONG g_log_volume = 0;  // [audio] log_volume=N (testing)
volatile LONG g_logged = 0;

// A slider's stored value (0..255); 255 (no change) if the object is not there.
int Slider(DWORD global)
{
	const BYTE *object = *reinterpret_cast<const BYTE *const *>(global);
	return object ? *reinterpret_cast<const int *>(object + kSliderOffset) : 255;
}

// The last volume logged for sounds [0] and music [1] (FMOD's 0..255): only a
// change is logged, so a music fade-in or many sounds at the same volume take
// one line each, not dozens.
volatile LONG g_last_logged[2] = {-1, -1};

float Apply(int kind, DWORD global, float volume)
{
	const int slider = Slider(global);
	const float out = audiovol::Wide(volume, slider);
	if (g_log_volume > 0) {
		const LONG game = static_cast<LONG>(volume * 255.0f);
		if (game >= 1 && InterlockedExchange(&g_last_logged[kind], game) != game &&
		    InterlockedIncrement(&g_logged) <= g_log_volume)
			hooklog::Write("audio: %s: slider %d/255, game volume %ld/255 -> %d/255", kind ? "music" : "sound", slider,
			               game, static_cast<int>(out * 255.0f));
	}
	return out;
}

void __fastcall SoundVolumeHook(void *channel, void *edx, float volume)
{
	g_sound_next(channel, edx, Apply(0, kSoundManager, volume));
}

void __fastcall MusicVolumeHook(void *channel, void *edx, float volume)
{
	g_music_next(channel, edx, Apply(1, kMusicSystem, volume));
}

}  // namespace

void ApplyAudioVolumeFromSettings()
{
	wchar_t ini[MAX_PATH];
	hooklog::PathNextToExe(L"h5-together", L".ini", ini, MAX_PATH);  // h5-together-<instance>.ini for the robot
	wchar_t curve[16] = L"";
	GetPrivateProfileStringW(L"audio", L"volume_curve", L"wide", curve, 16, ini);
	g_log_volume = static_cast<LONG>(GetPrivateProfileIntW(L"audio", L"log_volume", 0, ini));
	if (lstrcmpiW(curve, L"wide") != 0) {
		if (curve[0] && lstrcmpiW(curve, L"game") != 0)
			hooklog::Write("audio: volume_curve=%ls is not game or wide: the game's sliders", curve);
		else
			hooklog::Write("audio: the game's volume sliders (volume_curve=game)");
		return;
	}
	g_sound_next = reinterpret_cast<ChannelVolumeFn>(
		hookpatch::RedirectCall(kSoundSite, kChannelVolume, reinterpret_cast<const void *>(&SoundVolumeHook)));
	if (g_sound_next)
		g_music_next = reinterpret_cast<ChannelVolumeFn>(
			hookpatch::RedirectCall(kMusicSite, kChannelVolume, reinterpret_cast<const void *>(&MusicVolumeHook)));
	if (!g_sound_next || !g_music_next) {
		// The two go together. If only the first went in, it stays: the sounds
		// get the wide curve and the music the game's (no harm).
		hooklog::Write("audio: the game's volume code is not as expected (%08lx, %08lx): the game's sliders%s",
		               kSoundSite, kMusicSite, g_sound_next ? " for the music" : "");
		return;
	}
	hooklog::Write("audio: volume_curve=wide: each slider step about the same number of decibels "
	               "(10%% = -40 dB instead of -20 dB; sounds at %08lx, music at %08lx)",
	               kSoundSite, kMusicSite);
}

}  // namespace mods
