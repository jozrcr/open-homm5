// Robot mode: lets tools/robot/run_ai_test.py run the game unattended.
//
// Active only when a file "open-homm5-robot.ini" sits next to the game exe (only
// ever in a test copy of the game). It then:
//   - redirects the "My Documents" folder (SHGetFolderPathA) to a robot home
//     folder, so the game uses its own profile and never touches the player's;
//   - counts AI turns and closes the game after a number of them, or after a
//     time limit.
// Config lines (key=value):
//   home=C:\Games\H5_test\robot_home
//   quit_after_ai_turns=12
//   quit_after_seconds=600
//   command=advmap /Maps/Multiplayer/A1M4/map.xdb#xpointer(/AdvMapDesc) 0 1 1
//   command_delay_seconds=8
//   end_turn_every_seconds=5      (ends the passive human's turns, see robot_endturn.cpp)
//   press_enter_every_seconds=0   (robot keyboard; not read by the game in the background yet)
#pragma once
#include <windows.h>

namespace robot {

// From DllMain (process attach): read the config and, if present, redirect
// Documents. Must run before the game reads its profile.
void Init();
bool Active();
// From the AI trace: an AI player has just started a new turn (its stage was 0).
void OnAiTurnStart(void *ai);

// Robot keyboard (robot_input.cpp). From the DirectInput8Create proxy: hook the
// DirectInput object so the keyboard device can be fed robot key presses.
void OnDirectInputCreated(void *direct_input);
// Press and release one key (a DIK_ code, e.g. 0x1C = Enter). False if busy.
bool PressKey(int dik_code);

// Robot human player (robot_endturn.cpp): ends the passive human's turns.
void OnAiStep();               // from the AI trace, every AI step
void InstallEndTurnHelper();   // from the main loop, once the build is known
void MaybeEndHumanTurn(DWORD every_ms, DWORD ai_idle_ms);

// Optional segment trace (robot_trace.cpp), when trace_segments=1.
void InstallSegmentTrace();
// Experiment: single-player segment length in ms (segment_ms=N in the ini).
void SetSegmentLength(DWORD ms);

}  // namespace robot
