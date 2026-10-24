// Robot mode: lets tools/robot/run_ai_test.py run the game unattended.
//
// Active only when a file "open-homm5-robot.ini" ("open-homm5-robot-<instance>.ini"
// when OPEN_HOMM5_INSTANCE is set, see log.h) sits next to the game exe (only
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
//   stuck_exit=1                  (close the game when the human player is stuck; 0 for a LAN
//                                  client, which never sees an AI step)
//   press_enter_every_seconds=0   (robot keyboard; not read by the game in the background yet)
//   step=12|menu|multi lan        (script steps: console, menu, ui or shot; see robot.cpp)
//   screenshot_dir=C:\Games\H5_test\robot_frames
//   screenshot_every_seconds=10   (robot_screen.cpp)
//   net_log=1                     (robot_net.cpp)
//   net_lan_relay_ports=8891      (two copies on one PC, robot_net.cpp)
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
void MaybeEndHumanTurn(DWORD every_ms, DWORD ai_idle_ms, bool stuck_exit);

// The game's narrow string as passed to button-command handlers: {begin, end,
// capacity} of chars (the handlers only read it).
struct GameString {
	const char *begin, *end, *capacity;
};

// Robot menus (robot_menu.cpp): learn the main-menu page objects, then press
// their buttons by command string (e.g. MenuCommand("multi", "lan")).
void InstallMenuHooks();
bool MenuCommand(const char *page, const char *command);

// Robot UI (robot_ui.cpp): send a button command to the screen of that class
// (e.g. UiCommand("NUI::CLobbyScreen", "create")), found through the game's RTTI.
bool UiCommand(const char *screen, const char *command);
// The live screen object of that class (last one found), or nullptr.
void *FindScreen(const char *screen);
// The RTTI type descriptor of a class or struct ("IWindow"), for the game's
// __RTDynamicCast, or nullptr.
const void *TypeDescriptorOf(const char *name);
// Research: "<screen class> <hex offset> ..." logs the objects along a pointer chain.
void Peek(const char *spec);
// Close windows waiting for a click the robot cannot give (combat results).
bool AutoCloseScreens();  // true while a battle or a box is being handled
// Post a game message by name, like a button with an ARSendGameMessage reaction.
bool PostGameMessage(const char *name, int param);
// The live object with this vftable and, at second_offset, second_vftable (last
// one found; *count = how many), by scanning the game's memory (~0.1 s).
void *FindLiveObject(DWORD vftable, DWORD second_offset, DWORD second_vftable, int *count);

// Robot lobby (robot_lobby.cpp): highlight the first map whose file path contains
// `wanted` on the multiplayer "create a game" screen (logs all maps).
bool PickMap(const char *wanted);
// Highlight and select the first save whose texts contain `wanted` on the
// single-player load screen (logs all saves); then "msg|load" loads it.
bool PickSave(const char *wanted);
// A checkbox of the multiplayer setup screen: "quick_combat|ghostmode|realtime <state>".
bool SetCreateOption(const char *spec);
// A slider of the multiplayer setup screen: "animationspeed|difficulty|timelimit|combatturnspeed <position>".
bool SetCreateSlider(const char *spec);
// Highlight game number `index` (0 = first) in the LAN lobby's games list.
bool PickGame(int index);
// Research: log the waiting room's player slots and whether the host can start.
void LogWaitSlots();
// Press a waiting-room row's state arrow: "<row> left|right".
bool PressSlotArrow(const char *spec);

// Robot screenshots (robot_screen.cpp): the game's own frames, saved as .bmp.
void InstallScreenshots(const char *dir);   // from Init, before Direct3D starts
void RequestScreenshot(const char *label);  // the next frame shown is saved
DWORD LastFrameTick();                      // GetTickCount() of the last frame shown

// Robot network log (robot_net.cpp), when net_log=1: socket calls in the log.
// For several copies of the game on one PC (LAN game discovery), see the file:
// share_udp_ports (net_share_udp_ports=1) and relay_ports (net_lan_relay_ports=P,Q).
// logged_packets: how many packets per socket and direction to log (-1 = default).
void InstallNetLog(bool share_udp_ports, const char *relay_ports, int logged_packets);

// Appends the return addresses found on a stack (starting at esp) to line: the
// first few system ones as "module+offset", then the game's own (Ghidra addresses).
void DescribeStack(DWORD esp, char *line, size_t size, int max_frames);

// Replace one import of the exe (its import address table entry); returns the
// original function, or nullptr if the import is not found.
void *PatchImport(const char *dll, const char *function, void *replacement);

// AI profiler (robot_profile.cpp), when profile_ai=1: samples the main thread
// while the AI thinks (OnAiThinking from the AI trace) into open-homm5-profile.txt.
void StartAiProfiler();
void OnAiThinking(bool thinking);

// Virtual clock (robot_clock.cpp), when virtual_clock_ms=N: the main thread's
// clock advances N ms per frame and by the time slept, not with real time.
void InstallVirtualClock(DWORD frame_ms);
void VirtualClockFrame();

// Pathfinder trace (robot_paths.cpp), when trace_paths=1: per AI step, the path
// searches by call site, their time, visited nodes and repeats ("paths step" lines).
void InstallPathTrace(int level);  // 1 = details of big searches, 2 = of every search, 3 = also time the mod's game helpers
void PathTraceStep();  // from OnAiStep

// Optional segment trace (robot_trace.cpp), when trace_segments=1.
void InstallSegmentTrace();
// Multiplayer segment trace (robot_trace.cpp), when trace_mp=1.
void InstallMpTrace();
// World checksum trace (robot_trace.cpp), when trace_crc=1: a "crc" log line per
// world checksum, to compare between the copies of a LAN game (desync check).
void InstallCrcTrace();
// Experiment: single-player segment length in ms (segment_ms=N in the ini).
void SetSegmentLength(DWORD ms);
// Experiment: multiplayer segment length, 100 or 50 ms (mp_segment_ms=N); every
// copy of the game must use the same value.
void SetMpSegmentLength(DWORD ms);
// Experiment: AI heroes walk N times faster (ai_walk_speed_scale=N); in
// multiplayer every copy must use the same N.
void SetAiWalkSpeedScale(int scale);
// Experiment: the hero movement update's 200 ms step (walk_step_ms=N); in
// multiplayer every copy must use the same N.
void SetWalkStepLength(DWORD ms);
// Trace: each change of the AI manager's "player busy" check (trace_ai_busy=1);
// experiment ai_busy_ignore_end=1: movement end times no longer count.
void InstallAiBusyTrace(bool ignore_end);

}  // namespace robot
