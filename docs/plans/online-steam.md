# Plan: online play over Steam (a "virtual LAN")

Status: **the spike passed (2026-11-02): two Steam accounts' games talk through Steam under app 15370.** Solo: Steam starts in the game's process, the relay network comes up, and a friends-only lobby is created ([results](#solo-spike-results-2026-11-02)), even when the robot, not Steam, started the game. Two PCs: a Linux laptop under Proton, on a phone's 4G connection, found the Windows host's friends-only lobby through the friends list, joined it, and 100 of 100 messages came back (median round trip 82 ms) ([results](#two-pc-test-results-2026-11-02)). **Steps 1 and 2 done too (2026-11-02):** the packet map shows every address in the game's packets comes from one Winsock call, and the virtual LAN core carried a whole robot LAN game (two copies on one PC, through a local stand-in for Steam) with all checksums equal and no real address in any packet ([results](#steps-1-and-2-the-packet-map-and-the-virtual-lan-core-2026-11-02)). **Step 3 built (2026-11-02):** the Steam transport (`[online] mode=steam`): on one PC, Steam starts in the game, the relays come up, a friends-only lobby is made when the game hosts, and a loopback robot LAN game still has all checksums equal ([results](#step-3-the-steam-transport-2026-11-02)). The two-PC test of the real game over Steam passed ([online-steam-test2.md](online-steam-test2.md)). **Step 4 built (2026-11-03):** the online UI: an "Online play: Off / Steam" row in Options > H5 Together, Steam's lines on the game's LAN screen, "Invite friends..." next to "Rules..." in the waiting room (Steam's own dialog, or our list), and an invite's banner and pop-up ([results](#step-4-the-online-ui-2026-11-03)). Code: `src/mods/virtual_lan*.cpp`, `src/mods/steam_runtime.cpp`; the spike `src/mods/steam_spike.cpp` (`[online] spike`, off by default). SDK 1.65 is at `ref\steamworks_sdk_165\` (never committed).

Goal: friends join each other's games **from inside the game**, with no GameRanger and no ZeroTier. This is phase 6 of the project ("built-in online play"). This plan picks Steam as the first way to do it. The older idea (our own lobby server plus GameNetworkingSockets) becomes the fallback.

## In short

- **The idea.** The game keeps its own LAN multiplayer code, unchanged. Our DLL carries the game's LAN packets over Steam, between Steam friends. Steam's relay servers get through home routers and firewalls. The game still thinks it is on a LAN.
- **What players see.** A friend's game shows up in the game's own **LAN list**. Later, an invite or a "Join game" from the Steam friends list works too.
- **The fallback.** Plain LAN and ZeroTier stay as they are. They cover GOG copies, players without Steam, and anything Steam refuses.
- **What we found in the game:**
  - LAN play is **UDP only**. There is one socket factory and normally two sockets: the discovery port 8889 and the game port 8888.
  - **The game writes its own IP addresses inside its packets.** The other PC then tries each of those addresses.
  - We can handle both at the Winsock level, the same place `lan_discovery.cpp` already hooks. We answer the game's "what are my addresses?" question with a fake address. No packet has to be rewritten.
- **The big unknown.** Will Steam accept lobbies and relayed messages under app id **15370**? That is Heroes V ToE's own id, and the game never used Steamworks. Valve's documents don't say. We found no mod that did exactly this; the known mods use Valve's test app 480 (Spacewar). **The spike answers this first.**
  - **Solo answer (2026-11-02): yes for init, relays and lobbies.** Steam fetched the relay configuration for `appid=15370` and reached its relays in 6 s. A friends-only lobby was created and its data read back. Messages between two PCs are the part still untested.
- **Effort:** about 10 sessions after a successful spike, plus test evenings with a friend.

## How a LAN game works today (what the virtual LAN must carry)

Sources: the decompiled functions in `analysis/export/dump/`, the exe's import table (read from `ref\MMH55_Utility_64.exe`), and the robot's socket logs (`net_log=1`, `build\robot\20261025-103804-lan\{host,client}\open-homm5.log`).

### Sockets and ports

| What | Where | Detail |
|---|---|---|
| The only socket factory of the game's network code | `0x00484c30` (called by `0x00484fe0`, which is called by `0x00513500`) | `socket(AF_INET, SOCK_DGRAM, UDP)`, then `bind(0.0.0.0:port)` only if port > 0, non-blocking (`FIONBIO`), `SO_BROADCAST` on. |
| Discovery socket | port `net_lan_match_maker_port` (default **8889**) | Opened when the LAN screen opens. It carries the "any games?" queries, the replies, the join request **and** a reliable link during the lobby phase. |
| Game socket | port `net_game_port` (default **8888**) | Opened when you host or join. It carries the address probes and the game's reliable link. |
| Send / receive | socket object vtable `0x00e133c8`: slot 9 = `0x00484d30` (`sendto`), slot 10 = `0x00484ea0` (`recvfrom`, **2048-byte buffer**) | One packet per call. The network thread `0x0047cbd0` polls every 10 ms. |
| "My addresses" | slot 11 = `0x00484ac0` | `getsockname` for the port, then **up to 4 IPv4 addresses from `gethostbyname(gethostname())`**. |
| Broadcast address | `0x00484db0` (socket object constructor) | The *classful* broadcast address of the first address from `gethostbyname(gethostname())`, for example 10.255.255.255. Plus 255.255.255.255 from `0x0047c8e0`. |

**No TCP in LAN play.** The exe does import `connect`, `send`, `recv` and `select`. But every use of them (code at `0x00d81000`–`0x00d83000`) belongs to ubi.com's statically linked "GS" client library (strings like `GSClientLibrary`, "connecting to ubi.com services"). ubi.com's servers are gone, and the LAN path never calls that library. The game's own network code (`NNet`) only goes through the thunks at `0x00d3e384`–`0x00d3e3ea`, and its only `socket()` call creates UDP sockets.

### The packets when you join a LAN game

From the robot's two copies on one PC (host ports 8889/8888, client ports 8891/8890). Byte 0 is the packet type. Meanings marked "probably" are guesses from sizes and timing.

| Byte 0 | Size | Socket | Direction | Meaning |
|---|---|---|---|---|
| `02` | 5 | discovery | everyone, broadcast, once a second | "Any games here?" plus 4 bytes that change each time. |
| `03` | 826 | discovery | host → asker, unicast | The game's description (what the LAN list shows), in the game's tagged serialization. It holds the host's **address record twice** (step 1, below). |
| `01` | 31 | discovery | joiner → host | The join request. Bytes 9–28: the joiner's discovery port (`bb 22` = 8891, little endian) and its **4 addresses**. |
| `04` | 33 | discovery | host → joiner | The join answer (probably `SEnterGameAllowedPacket`). Bytes 9–28: the host's discovery port and its **4 addresses**. |
| `00` | 18–273 | both | both ways | The reliable link: data and acknowledgements (see [multiplayer-sync.md](../subsystems/multiplayer-sync.md), "The LAN link"). In the lobby its data carries every player's **address record** (the host sends the player list to everyone). |
| `07` | 9 | game | both ways | Address probe. `07 00` + a 4-byte token goes to **each** address of the other PC; `07 01` + the same token is the answer. The first address that answers wins. |

**Step 1, the packet map (2026-11-02):** where each address sits, byte by byte, is in [multiplayer-sync.md, "Addresses inside the packets"](../subsystems/multiplayer-sync.md#addresses-inside-the-packets-2026-11-02). In short: every address in every packet comes from `gethostbyname(gethostname())` (through `0x00484ac0`), in two forms: the raw list of `01`/`04` (port + 4 addresses), and a player's record in `03` and the lobby's `00` data (a `sockaddr_in` with the first address and the game port, plus the list). A robot run scanned all 10,792 packets of the host for the PC's own addresses: 119 held some (`03` ×100, `00` ×17, `01`, `04`), all before the first AI turn; none came from anywhere else.

### Address-dependent things (what a virtual LAN must get right)

- **The game sends its own IPs inside packets.** Each PC tells the others its addresses (from `0x00484ac0`, via `gethostbyname`): in `01`, `03`, `04` and the lobby's `00` data (step 1 above). The other PC sends its `07` probes to all of them. In the robot logs the host probed the client's 4 addresses (ZeroTier, WSL, Ethernet, Hyper-V), and the client probed the host's 4.
- **A link is the other PC's IP:port.** Received packets are matched to links by their sender address. So a friend's fake address must stay the same for the whole game, and their packets must always arrive from that address **and from the same port**.
- **Every PC talks to every other PC.** The "segment done" reports go directly between all PCs, not through the host ([responsiveness.md](responsiveness.md)). So in a 4-player game each client also needs a path to the other clients. The host passes their addresses along.
- **Versions.** The game has a `NetGameVersion` check on connect (`0x00402c60`, next to "request for connect is sent"). Our co-op guard also compares add-on versions in its hello.

## Research: can we use Steam this way?

### 1. Steam networking under app 15370 (a game without Steamworks)

- **Starting Steam inside our process.** `SteamAPI_Init` must succeed before any Steam call. It fails when:
  - Steam is not running;
  - Steam can't tell which app this is ("running directly without a `steam_appid.txt`");
  - the user doesn't own the app;
  - the app "isn't fully set up" ([Valve: Steamworks API overview](https://partner.steamgames.com/doc/sdk/api)).

  That last case is the risk for 15370.
- **Telling Steam the app id.**
  - When Steam starts the game, it already tells the process the app id.
  - When the game is started another way (GameRanger, a shortcut), Valve's documented route is a `steam_appid.txt` file in the working folder. Valve says not to ship that file.
  - The common undocumented route is to set the environment variable `SteamAppId` (and `SteamGameId`) in our process before `SteamAPI_Init`. Wrapper libraries do this ([steamwand.js](https://github.com/JDeffner/steamwand.js/wiki/Getting-Started)). It writes nothing on disk, so it is our first choice. **The spike checks it.**
  - We must **not** call `SteamAPI_RestartAppIfNecessary`. It would relaunch the game through Steam, which starts the Steam build's `H5_Game.exe`, not MMH5.5's exe.
- **What friends see.** Once `SteamAPI_Init` succeeds as 15370, Steam counts the process as playing "Heroes of Might & Magic V: Tribes of the East", even if GameRanger or a shortcut started it.
  - Custom rich presence text needs localization tokens that the app's developer uploads (`steam_display`; [ISteamFriends](https://partner.steamgames.com/doc/api/ISteamFriends)), so we can't show "Map: ..., 3/4 players" in the friends list.
  - The `status` key (shown under "view game info") and the `connect` key ("Join game") need no setup.
- **Lobbies.**
  - Lobby types: private (invite only), friends only, public, invisible.
  - Up to 250 members; key/value data per lobby and per member; binary chat messages up to 4 KB.
  - An invite accepted while the game runs arrives as `GameLobbyJoinRequested_t`. If the game isn't running, Steam starts it with `+connect_lobby <id>` ([ISteamMatchmaking](https://partner.steamgames.com/doc/api/ISteamMatchmaking)).
  - Lobbies belong to the app id. That is why mods on Spacewar (480) see everybody's lobbies and must filter by a key of their own.
- **Relayed messages (Steam Datagram Relay, SDR).**
  - The new APIs (`ISteamNetworkingMessages`, `ISteamNetworkingSockets`) "relay packets through the Valve network by default" and hide IP addresses ([Steam networking overview](https://partner.steamgames.com/doc/features/multiplayer/networking)).
  - For P2P the developer only uses the APIs and "Steam will take care of everything else" ([SDR](https://partner.steamgames.com/doc/features/multiplayer/steamdatagramrelay)).
  - Valve does not say whether every app id gets SDR, or whether an app without Steamworks setup does. Steam Support adds that players can choose to always relay P2P traffic ("Peer-to-Peer Networking and Sharing Your IP Address", [Steam Support](https://help.steampowered.com/redirects/kb/9929-UAFN-9329)).
- **`ISteamNetworkingMessages` is the UDP-like one.** It has no connection handles: you send to a Steam user on a numbered channel, and the session is set up on the first send.
  - The receiver gets `SteamNetworkingMessagesSessionRequest_t` and must accept it.
  - Unreliable messages may be lost, reordered or duplicated, like UDP.
  - An idle session times out after a few minutes ([ISteamNetworkingMessages](https://partner.steamgames.com/doc/api/ISteamNetworkingMessages)).
  - `k_nSteamNetworkingSend_UnreliableNoDelay` drops a message only "if the connection is not fully connected". `UnreliableNoNagle` queues it instead. Messages can exceed one packet, but an unreliable message is lost whole if any piece is ([steamnetworkingtypes.h](https://github.com/ValveSoftware/GameNetworkingSockets/blob/master/include/steam/steamnetworkingtypes.h)).
- **Precedents:**
  - [Steam Bridge](https://modrinth.com/mod/steam-bridge) (Minecraft, MIT licence) carries the game's traffic over SDR. It hardcodes Spacewar 480 "to discourage use of commercial app IDs" and bundles `steam_api64.dll`.
  - [Porthole](https://store.steampowered.com/app/4963920) (a free Steam app, released 3 Aug 2026) shares a local TCP/UDP port with Steam friends through lobbies and Valve's relays, under its own app id.
  - A [Steam forum request](https://steamcommunity.com/discussions/forum/10/2801754975108469194/) asks Valve for exactly this "virtual LAN" for every game. Valve has not built it.
  - [CnCNet](https://www.openhub.net/p/cncnet) does the same for old C&C games with a replacement `wsock32.dll` and its own tunnel servers. That is our fallback design.
  - **We found no mod using the app id of a game that never integrated Steamworks.** So 15370 is untested ground.
- **The overlay.**
  - Steam's overlay "automatically hooks into any game launched from Steam". When the game is started another way, it loads at `SteamAPI_Init`, which must come before the D3D device is created ([overlay](https://partner.steamgames.com/doc/features/overlay)).
  - Our ImGui hook replaces `IDirect3DDevice9::Present` in the device's vtable (`d3d9_hook.cpp`), and the overlay hooks the same function, so the two must coexist. For players who already start the game from Steam, they probably already do.
  - The overlay is optional for us. It gives Valve's invite dialog (`ActivateGameOverlayInviteDialog`); our own ImGui friend picker can do the same with `InviteUserToLobby`.

### 2. Linux (Proton / Wine)

- Proton puts its own Steam client bridge, `lsteamclient`, into the game's Wine prefix. Its source has a version for every Steamworks SDK from 0.99u to 1.62 (Proton 9; [Proton/lsteamclient](https://github.com/ValveSoftware/Proton/tree/proton_9.0/lsteamclient)). Every Windows Steamworks game on Linux and the Steam Deck goes this way. So **a Windows `steam_api.dll` inside the game should reach the Linux Steam client** when the friend runs app 15370 through Steam with Proton.
- **A non-Steam shortcut run with Proton** also gets Proton's Steam shims (community reports, e.g. [itch.io thread](https://itch.io/post/7769949)). But Steam gives it a made-up app id, which our DLL overrides with 15370. Expected to work if the account owns ToE; **to test**.
- **Plain Wine or Lutris, outside Steam:** no bridge, so no Steam mode. That friend stays on ZeroTier or LAN.
- Pick an SDK version that the friends' Proton supports (SDK 1.62 or older for Proton 9).

### 3. Licence of `steam_api.dll` (plain summary, not legal advice)

- Anyone may download the SDK by accepting the [Steamworks SDK Access Agreement](https://partner.steamgames.com/documentation/sdk_access_agreement). The owner does that when he downloads it.
- Section 1.1(b) allows us to "reproduce and distribute" the files of `redistributable_bin` (that is, `steam_api.dll`) "along with the Licensee Software in object code form". "Licensee Software" means "a game or application software designed to take advantage of the Steamworks Services". Our add-on fits that description.
- Nothing in it limits the price (free is fine) or the channel (GitHub releases are not excluded).
- **Grey areas:**
  - **App id 15370 belongs to Ubisoft's game, not to us.** The agreement says nothing about whose app id may be used. Valve may end the licence "immediately upon written notice", and it ends by itself on any breach.
  - **The SDK source** may be used "solely to develop the Licensee Software". So we should **not** commit the SDK's headers to our public repo (our own rule already bans committing DLLs). We declare the dozen flat C functions we call ourselves and load `steam_api.dll` at run time.
  - **No trademark rights:** we write "Steam" as plain text only, no logos.
- **If we would rather not ship the DLL:** each player could copy it from the SDK themselves. That works but is clumsy.

### 4. Mixed groups and versions

- **A Steam player plus a GOG player.** The GOG player has no Steam licence for 15370, so `SteamAPI_Init` fails for them. Options:
  - (a) the whole group uses ZeroTier/LAN for that game, as today (simplest);
  - (b) use Spacewar 480 (any free Steam account works, so a GOG player could join);
  - (c) later, a "bridge" host that is in a Steam game and on ZeroTier at once and passes packets between them. That is complex, because every PC must reach every other PC.
- **Different add-on, MMH5.5 or exe versions.** The host's lobby carries them. A client's add-on doesn't send queries to a host it can't play with, so that game never appears in its LAN list. Our panel says why, for example "Pierre's game: needs H5 Together 0.3.1 (you have 0.3.0)" or "needs MMH5.5 RC19k". The game's own `NetGameVersion` check and our co-op guard stay as a second check.
- **One PC both on a real LAN and on Steam:** in Steam mode the PC advertises only its fake address, so a real-LAN player without Steam can't join that game. A game uses one transport for everybody (setting `keep_lan`, below, later).

## The design

```
 PC A (host)                                   PC B (client)
 game (unchanged)                              game (unchanged)
   | sendto(198.18.7.21:8889)                    ^ recvfrom -> from 198.18.40.3:8889
   v                                             |
 our Winsock hooks  ---- Steam message ---->   our Winsock hooks
 (fake address -> SteamID,                     (channel = destination port,
  channel = destination port,                   SteamID -> fake address)
  2-byte header = source port)
          \___ Steam relays (SDR) or a direct path, chosen by Steam ___/
```

### Fake LAN addresses

- Each player gets a **fake IPv4 address in `198.18.0.0/15`**. That range is reserved for network tests (RFC 2544), so it doesn't clash with real LANs, ZeroTier (`10.x`) or Tailscale (`100.64.x`).
  - It is computed from the player's SteamID: a hash, 17 bits, last byte kept in 1–254 so it never looks like a broadcast.
  - Every PC computes the same address for the same friend.
  - If two members of one lobby get the same address (about 1 in 5,000 for 8 players), the later one moves to the next free address and the lobby's member data records it.
- Our `gethostbyname` hook answers the **game's own host name** with only this fake address while Steam mode is on. Every other name goes to the real function, including ubi.com's library, which shares that import. Effects:
  - the address lists the game puts in its packets contain only the fake address, so **nothing needs rewriting**;
  - the `07` probes go only to fake addresses;
  - the game's classful broadcast becomes `198.18.x.255`, which our hook catches.
- Players keep the game's normal ports (8888/8889). In the robot's tests each copy keeps its own ports.

### Sending and receiving

- **`sendto` hook** (on the game's network thread):
  - Destination is a fake address: look up the SteamID and send a Steam message. The channel is the destination port, and a 2-byte header carries the source port. Return the length, as UDP does. Unknown fake address: drop it (the game resends).
  - Destination ends in `.255`, or is 255.255.255.255, and the packet is a `02` query: send one copy to each **known host** (below), to that host's discovery port.
  - Any other destination: in Steam mode, drop real-network discovery so that LAN games don't show up and then fail to join. Otherwise pass it to the real `sendto`.
- **`recvfrom` hook:**
  - For the socket's local port P (taken from `bind` or `getsockname` and cached), take the next Steam message on channel P.
  - Fill `from` with the sender's fake address and the source port from the header, and return it.
  - If there is none, call the real `recvfrom` (LAN traffic, loopback for the robot), or return `WSAEWOULDBLOCK`.
  - The game already polls each socket every 10 ms, so no extra thread is needed for packets.
- **Send flags:** `ReliableNoNagle | AutoRestartBrokenSession` (since the 4G test; `[online] reliable=0` gives the first choice, `UnreliableNoNagle`).
  - First unreliable, because the game already does its own acks and resends for its link. But not for everything: its "accepted" answer to a join request (`04`) goes once, and a duplicate request is ignored. Over 4G one was lost and the join failed ([online-steam-test2.md](online-steam-test2.md), run 2b). On a LAN nothing is lost, so reliable Steam messages give the game what it was made for. The game's own resends still run; a copy that arrives twice is dropped by its link, as on a LAN.
  - No Nagle, so packets are not held back.
  - We don't use `NoDelay`: it drops packets while the session is still being set up, and that would lose the first join packets.
- **Sizes:** game packets are 5–984 bytes (984: the game description once a player has joined; step 2) and the receive buffer is 2048. Steam's packet payload is around 1,200 bytes, so our packets fit in one Steam packet. The game sends at most one packet per link per send tick (now 10 ms with `net_tuning`), a few KB/s.
- **Order and loss:** the same as UDP, which the game's link layer already expects.
- **Disconnects:** when a friend leaves the lobby or the Steam session fails, packets simply stop. The game's own timeout (`net_timeout`, 20 s) drops the player, as on a LAN. We change nothing there.

### Lobbies: how a game is advertised and joined

- **Detecting that this PC hosts.** Once a second, while Steam mode is on, our `recvfrom` hook delivers a fake `02` query to the game's own discovery socket. The sender is a reserved fake address (`198.18.0.1`) that never goes on the wire.
  - If the game answers with a `03`, this PC is hosting. We swallow that answer.
  - This needs no game code hooks and no knowledge of the `03` layout.
- **The host's lobby.** The host's add-on creates a Steam lobby (friends only by default) when hosting starts, and leaves it when the `03` answers stop.
  - Lobby data: our protocol version, add-on version, MMH5.5 version (RC19d/RC19k), exe kind, and the host's discovery port. Later also the map and player count, once we decode the `03` packet.
- **Known hosts** (whom a client's queries go to):
  - the owners of lobbies of Steam friends who play 15370 (`GetFriendGamePlayed` gives the friend's lobby);
  - with `lobby=public`, also the lobby list filtered on our key.
  - Hosts with an incompatible version are left out.
- **In the LAN list:** the client's game broadcasts `02` as usual. The host's game answers `03` through Steam, and the client's game shows the host's game **in its own LAN list**, under the name the host typed.
- **Joining:**
  - The player clicks Join in the game's list. The game sends `01` to the host's fake address, and the add-on joins the host's lobby at that moment.
  - The `07` probes then go between fake addresses. The lobby's member list tells every PC which SteamID is behind each fake address, so the clients can reach each other too.
  - Each Steam message also says who sent it, so a PC learns the reverse direction on the first packet.
- **Session requests:** accepted only from members of our lobby, or from Steam friends running the add-on. Anything else is ignored (privacy).
- **Invites (step 4, built: [results](#step-4-the-online-ui-2026-11-03)):**
  - "Invite friends..." next to "Rules..." in the waiting room uses the overlay's invite dialog if it is there, or our own friend list with `InviteUserToLobby`.
  - An invite accepted while the game runs arrives as `GameLobbyJoinRequested_t`. The add-on adds that host to the known hosts and shows a banner on the main menu, and the pop-up and taskbar flash when the game is in the background: "Pierre invites you to play: open Multiplayer > LAN".
  - Started by Steam with `+connect_lobby` (game not running): this only works if the player's Steam launch option starts MMH5.5's exe. Steam's own `H5_Game.exe` is the 2025 rebuild, which our DLL doesn't know. For v1: invites work best while the game is running.

### Threads and timing

- **Packets:** on the game's network thread, inside our hooks (every 10 ms).
- **Steam callbacks** (lobby events, session requests, invites): **one thread of our own** runs `SteamAPI_RunCallbacks` (or the manual-dispatch API) every 20 ms. Valve recommends at least once a second.
  - Not in `Present`: the game shows no frames while a map loads, and a session request left unanswered for that long would stall a join.
- **One lock around every Steam call**, shared by the two threads, so we never depend on the library's own thread safety.
- **Steam starts lazily:** our DLL loads `bin\h5-together\steam_api.dll` with `LoadLibrary` when `[online] mode=steam`. Without the file (GOG, Linux without Steam) the add-on runs as today and says so in the log.
  - Init happens in `InstallHooksOnce` (first `DirectInput8Create`), never in `DllMain`.
  - Whether that comes before the D3D device (needed for the overlay outside Steam) is to check in the spike.

### Where to hook: Winsock (recommended) or `NNet`

| | Winsock imports (`sendto`, `recvfrom`, `bind`, `gethostbyname`) | The socket object's vtable (slots 9, 10, 11) |
|---|---|---|
| What we patch | import table entries (data), as `lan_discovery.cpp` and `robot_net.cpp` already do. `PatchImport` stacks hooks. | three vtable pointers (data), with `PatchPointer` checking that they still hold `0x00484d30` / `0x00484ea0` / `0x00484ac0` |
| Needs to know | nothing of the game's structs | the game's packet-buffer structs (sockaddr plus buffer object, used by slots 9 and 10) |
| Works on | every exe that imports WSOCK32 the same way (MMH5.5 and vanilla 3.1 have the same imports) | only exes with these addresses |
| Testable | the same code with a fake transport | the same |

**Recommendation: Winsock.** It is simpler, already proven in our DLL, and needs no address checks beyond "the import was found". Fallback: if the `gethostbyname` answer turns out not to be enough (some address comes from somewhere else), replace slot 11 (`0x00484ac0`, "my addresses") with a checked `PatchPointer`.

### Settings and UI

```ini
[online]
mode=off          ; off (default) | steam | loopback (robot tests only)
lobby=friends     ; friends (default) | public | private (invite only)
app_id=15370      ; only for tests: 480 (Valve's Spacewar)
keep_lan=0        ; later: also accept plain LAN/ZeroTier players in a Steam game
log=1             ; log lobby events and each new peer (fake address <-> SteamID)
```

Built so far (steps 2 and 3): `mode=off|steam|loopback`, `app_id` (a hidden key, shared with the spike), and for the loopback `loopback_port` / `loopback_peers`. Lobbies are always friends only (`lobby=` comes with the UI if ever needed). The shipped `h5-together.ini` has only `;mode=off`, in an `[online]` section of its own. The peer log is always on (a few lines per game), so `log` is not needed yet. Step 4: `mode` is a row of the Options page (Off / Steam; `loopback` stays a file value for the robot), and the hidden `ui_test=1` shows made-up friends for the robot's screenshots.

- **Off by default** at first. It changes no gameplay (only gameplay changes must default to the original), so it can be turned on by default once it has proven itself.
- **As built in step 4** ([details](#step-4-the-online-ui-2026-11-03)), which changed the first idea (an ImGui box on the LAN screen with a "Play over: Steam / LAN" choice):
  - **Picking Steam or LAN:** a row of the game's own Options screen (Options > H5 Together > Online play: Off / Steam), applied after a restart: the hooks and Steam start long before the LAN screen.
  - **The list:** the games stay in the game's own LAN list. Steam's status, invites and the games with problems are lines in the empty box under that list, in the game's colours.
  - **"Invite friends..."** sits next to "Rules..." in the waiting room, for the host.
  - **Reuse:** the overlay (`src/hook/overlay.cpp`), the "Rules..." button's way (`host_rules_panel.cpp`), the game's button and panel art (`rmg_builder_widgets.h`), the pop-up and taskbar flash (`notify.cpp`), our texts files (`src/mods/texts`).

## Testing

- **The robot can't use Steam:** it runs two game copies on **one** PC, and one PC runs only one Steam account. So the virtual LAN code gets a **transport interface** with two versions:
  - **Steam:** the real one.
  - **Loopback** (`mode=loopback`): the two copies exchange the same messages over a local UDP socket of our own (127.0.0.1, one port per robot instance), with made-up SteamIDs (built from that port: `LoopbackId`). A tiny fake lobby (every copy tells the others once a second whether it hosts) stands in for Steam's.
  - All the address mapping, broadcast fan-out, hosting detection and port handling is the **same code** in both.
- **Robot checks (loopback):**
  - the client finds the host's game through the virtual LAN only (real broadcasts off);
  - joins it; plays 8 AI turns;
  - world checksums all equal (`lan_report.py`);
  - AI move times no worse than plain LAN;
  - **no real IP address in any packet** (the robot's `net_log` extended to whole packets, searched for this PC's adapter addresses).
- **Unit tests** (`src/hook/test/`): address from SteamID, collisions, the 2-byte header, broadcast fan-out, version checks. The logic sits in its own file, as `host_rules_logic.cpp` does.
- **Real tests (the owner):** they need **two PCs and two Steam accounts that both own ToE and are Steam friends** (the owner plus a friend or his brother). Order:
  1. Windows to Windows;
  2. Windows to Linux (Proton);
  3. three PCs (checks the client-to-client paths);
  4. one player started from GameRanger or a shortcut instead of Steam.

## The first spike (as soon as the SDK is there)

**Question:** inside the game's process, under app 15370, does Steam start, accept a lobby and carry relayed messages between two PCs?

**What I do (about 1 session):**

1. Unpack `ref\steamworks_sdk.zip` (in `ref\`, gitignored). Note the SDK version and check that `redistributable_bin\steam_api.dll` is 32-bit.
2. Add a spike mode to the DLL, `[online] spike=1`, off by default, logging everything:
   - load `bin\h5-together\steam_api.dll`;
   - set `SteamAppId` and `SteamGameId` to `app_id` if Steam hasn't already;
   - `SteamAPI_Init` (log the error text);
   - log the SteamID, `GetAppID()` and our persona name;
   - `InitRelayNetworkAccess()`, then log the relay network status until it says "available" (or 30 s).
3. **PC A** (`spike=host`): create a friends-only lobby with `oh5=spike` and log the result and lobby id. Set rich presence `status`. Answer every message on channel 7.
4. **PC B** (`spike=join`): find A's lobby through A's friend entry (`GetFriendGamePlayed`), log it, join it, then send 100 unreliable messages of 300 bytes on channel 7, one every 50 ms. Log:
   - how many came back and the round trip;
   - whether the session is relayed or direct (`GetSessionConnectionInfo`).
5. Also log: whether the overlay appeared (Shift+Tab), and whether the game ran normally to the main menu and into a LAN screen.

**What the owner does:**

- Downloads the SDK with his Steam account (he accepts Valve's SDK agreement) and puts it at `ref\steamworks_sdk.zip`.
- Approves copying `steam_api.dll` into **his test copy** of the game (`bin\h5-together\`).
- With Steam open, starts the test copy on his PC with `spike=host`. A friend or his brother starts it on a second PC with his own Steam account (owning ToE, Steam friends with the owner) with `spike=join`.
- Runs three variants, about 10 minutes in all:
  - (a) both started from Steam;
  - (b) the joiner started directly (shortcut or GameRanger);
  - (c) the joiner on Linux with Proton.
- Sends both `h5-together.log` files.

**It worked if:**

- `SteamAPI_Init` succeeds with app 15370 in all three variants (or we know which variant fails);
- the lobby is created and B sees it;
- at least 95 of 100 messages come back with a round trip under 200 ms;
- the friends list shows both playing ToE;
- the game runs as usual.

**If 15370 is refused** (Init, lobby or relay fails): run the same spike with `app_id=480`. That tells us whether the problem is the app id or our code. Then choose a fallback (risks, below).

### What was built

- **`src/mods/steam_spike.cpp`**, setting `[online] spike`. Values: `0` (off, the default), `1` (solo, one PC), `host` / `join` (the two-PC test). Other keys: `app_id` (default 15370), `spike_seconds`, `spike_lobby`, `spike_pings`, `spike_ping_bytes`.
- **No SDK in the repo.** `src/mods/steam_api_flat.h` declares the ~40 flat C functions and structures we use. The DLL loads `bin\h5-together\steam_api.dll` with `LoadLibrary` and `GetProcAddress`. Without that file, nothing changes.
- **The declarations are checked against the SDK.** `src/hook/test/steam_abi_check.cpp` compares every size, offset, callback number and constant with Valve's headers. `build.ps1 -Test` runs it when `ref\steamworks_sdk_*` (or `STEAMWORKS_SDK`) is there, and skips it on GitHub. All checks pass for SDK 1.65.
- **The pure parts are unit-tested** (`steam_spike_logic.cpp`, `test/steam_spike_test.cpp`): the setting, the log names, the test messages, the round-trip statistics.
- **Threads:**
  - `SteamAPI_InitFlat` runs on the game's thread, at the first `DirectInput8Create`.
  - Everything else runs on a thread of our own, every 20 ms. Callbacks use Steam's manual dispatch, so they never run on a game thread.
  - We don't use the per-frame hook, because the game shows no frames while a map loads.
- **The robot:** `run_ai_test.py --online-setting spike=1` writes the `[online]` section.

### Solo spike results (2026-11-02)

The robot started the test copy (`C:\Games\H5_test`, MMH5.5 RC19d, `MMH55_Utility_64.exe`) directly, not through Steam. Steam was open on the owner's account. The run was single player on A1M4 for 150 s, and the spike ran 60 s. (`build\robot\20261102-115932\open-homm5.log`; SteamID and name left out here.)

| Question | Answer | Log line |
|---|---|---|
| Does Steam start in the game under 15370 when Steam didn't launch it? | **Yes**: setting `SteamAppId` in the process was enough, with no `steam_appid.txt` | `SteamAPI_InitFlat -> 0 (OK) in 1093 ms` … `Steam says our app id is 15370` |
| Relay network (SDR) for app 15370? | **Yes, OK after 6 s** | `"Attempt #1 to fetch config … GetSDRConfig/v1?appid=15370"`, then `relay network … OK (current) … "OK.  Relays: 24 valid, 5 great, 10 good+, 16 ok+, 7 ignored"` |
| Friends-only lobby? | **Yes**: created in 0.3 s; data set and read back | `CreateLobby(friends only) -> OK (EResult 1), lobby 1097…`; `read back oh5_spike="solo" oh5_version="0.2.7"; members 1, owner us` |
| Lobby list search | 0 lobbies, as expected: friends-only lobbies never show in searches. The two-PC test finds the host through the friends list instead. | `lobby list -> 0 lobbies` |
| Messages (`ISteamNetworkingMessages`) | **Sending works.** Messages to ourselves (4 unreliable, 1 reliable) all came back in 16–32 ms (the first in 391 ms, while the relays were still being measured). | `message #1 … to ourselves -> OK`, `message #2 came back from ourselves after 32 ms` |
| ↳ but | A message to ourselves goes through an internal "pipe" (flags 12 = fast + loopback, not relayed). Each message also showed "session request from ourselves" and "session FAILED … closed by peer", which looks harmless. **This proves nothing about the relays between two PCs**: that is the two-PC test. | `session with ourselves: closed by peer (4) … not relayed (flags 12 …) "#… pipe"` |
| Overlay | `GameOverlayRenderer.dll` loaded at init. "Enabled" came later (no at init, yes at 45 s). Steam's own log: `GameID = 15370, OverlayGameID = 15370`, plus a notification toast for 15370. | `overlay at 45 s: IsOverlayEnabled yes` |
| "Now playing" | **Yes**: Steam's `content_log.txt` says `AppID 15370 state changed : Fully Installed,App Running` at our init, and `Fully Installed` when the game closed | |
| Did the game run normally? | **Yes**: 19 AI turns, 180 AI steps in 150 s, ended by the time limit; the player list overlay drew as usual | `robot: time limit of 150 s reached after 19 AI turns` |
| Init before the Direct3D device? | **No**: `DirectInput8Create` (our init) comes after the game's first device (4.9 s). The game makes three more devices after it, and the overlay worked anyway. | `d3d9: device … hooked` at 4890 ms, `game build` at 4922 ms |
| Leaving the lobby | `LeaveLobby` at the end of the 60 s. If the game closes earlier, Steam drops the lobby when the process ends. | `left lobby 1097…` |

No need for the 480 retry: 15370 worked.

**Side finding:** Steam has its own "FakeIP" feature (`steamnetworkingfakeip.h`: `ISteamNetworkingSockets::BeginAsyncRequestFakeIP`). It gives a Steam-managed fake IPv4 address and port for porting UDP code. Our own `198.18.x.y` mapping stays simpler, because the game's packets go through our Winsock hooks anyway. But FakeIP is worth a look in step 2.

### Two-PC test results (2026-11-02)

The owner ran it with two of his own Steam accounts, Steam friends with each other:

- **Host:** the desktop, Windows, the test copy `C:\Games\H5_test` (MMH5.5 RC19d, `MMH55_Utility_64.exe` started by double-click, not through Steam), home internet. `[online] spike=host`.
- **Joiner:** a laptop running **Linux, the game under Proton**, started from the Steam library. It has the **plain Steam game** (Steam's 2025 `H5_Game.exe`, no MMH5.5): the spike also runs on a build we do not know (`hooks::InstallUnknownBuild`), since it never touches the game's code. Internet through **a phone's 4G connection share**. `[online] spike=join`.
- The kit: `h5-together-steam-test2.zip` (scratchpad; our DLL, Valve's `steam_api.dll` from SDK 1.65, a one-line ini, a README).

| Question | Answer | From the logs |
|---|---|---|
| Steam starts under Proton? | **Yes**, at once: our Windows `steam_api.dll` reached the Linux Steam client through Proton | joiner: `SteamAPI_InitFlat -> 0 (OK) in 10 ms`; `Steam says our app id is 15370` |
| Joiner finds a friends-only lobby? | **Yes**, through the friends list, the moment the game started (no public lobby needed) | `friend … plays app 15370, lobby 1097…`; `joining lobby … (found through the friends list)` |
| Joins it? | **Yes**, in 0.4 s; the lobby data came along | `lobby entered (join call) … response 1 (success)`; `lobby oh5_spike="host", oh5_version="0.2.7" (ours 0.2.7); members 2` |
| Relay network | Up on both: host 6.2 s, joiner 7.5 s (24 relays known) | `relay network OK after 7454 ms` |
| Messages between the two PCs | **100 sent, 100 echoed back** | `100 pings sent (0 refused), 100 echoes back` |
| Round trip | min 60, **median 82**, max 1813 ms (the slow ones while the session was set up) | `round trip min 60, median 82, max 1813 ms` |
| Relayed or direct? | **Direct**: Steam's NAT traversal (ICE) got through, even on 4G; ping 49 ms | `connected (3), ping 49 ms, not relayed … P2P ICE` |
| Whole join, game start to done | about 12 s (most of it the relay network starting) | joiner log 4.0 s → 16.5 s |
| Overlay | Not enabled at init in either game (`IsOverlayEnabled no`; only checked then) | not needed for the virtual LAN; matters later for Steam's invite button |

What this settles:

- **App 15370 works between two accounts**, Windows and Linux, a game started by Steam and one started outside Steam. No Spacewar 480 needed.
- **Friends-only lobbies found through the friends list** are enough to find a friend's game: no public lobby, no search.
- **Linux friends start the game from Steam with `WINEDLLOVERRIDES="dinput8=n,b" %command%`**, as for the add-on today; nothing more for Steam's networking.
- **Not yet seen:** a relayed connection (both ends got a direct path), 3 PCs, a long session, and the overlay inside the game.

## Steps 1 and 2: the packet map and the virtual LAN core (2026-11-02)

**Step 1, the packet map:** see [the packet table above](#the-packets-when-you-join-a-lan-game) and [multiplayer-sync.md](../subsystems/multiplayer-sync.md#addresses-inside-the-packets-2026-11-02). Every address the game puts in a packet comes from `gethostbyname(gethostname())`, so the design's single `gethostbyname` answer is enough: no packet is rewritten, and no fallback (vtable slot 11) is needed.

**Step 2, what was built** (`[online] mode=loopback`; `mode=off`, the default, hooks nothing):

- `src/mods/virtual_lan.cpp`: the hooks on the exe's `gethostbyname`, `sendto` and `recvfrom` imports, as in "The design" above. They go in when the DLL loads, before the robot's (`hooks::InstallNetEarly`), so they sit **under** `lan_discovery`'s and the robot's hooks: the robot's network log shows the packets exactly as the game sends and gets them. (A Steam-wrapped exe, which can't be recognized at that time, gets them from `InstallAll`.)
- `src/mods/virtual_lan_logic.cpp`: the pure parts, with 87 unit checks in `src/hook/test/virtual_lan_test.cpp`: the setting, fake addresses (all valid for 200,000 ids, never `.0`, `.255` or `198.18.0.1`), clashes, three PCs agreeing, the 2-byte header, where each packet goes, the hosting probe (with `GetTickCount` wrapping), known hosts, the query copies, the loopback datagrams.
- `src/mods/virtual_lan_transport.h`: the transport interface, shaped like `ISteamNetworkingMessages` plus a lobby (`SelfId`, `Start`, `Poll`, `Send(id, channel, message)`, `Receive(channel)`, `SetHosting(port)`, `Hosts`, `Members`). `virtual_lan_loopback.cpp` is its first version: a UDP socket of its own on 127.0.0.1 per copy, ids made up from its port, and an "I host on port P" message to the other copies once a second.
- Details that differ from or add to the design: the probe is a copy of the game's **own** last query (so we never build a `02` ourselves), and any `03` the game sends also counts as "hosting"; packets from the real network are read and dropped (as `keep_lan=0`); a message older than 3 s when the game asks for it is dropped (removed in 0.3.5's test9: it lost a Linux joiner's one `04` answer; a socket now starts empty at its bind and keeps everything after); the game's two copies of each query (classful broadcast and 255.255.255.255) go out once; until the transport knows our id, the game's own-address question gets no answer rather than a real address.
- The robot: `run_lan_test.py --net-log-bytes 2048 --net-scan --vlan`, and `tools/robot/packet_map.py` ([tools/robot/README.md](../../tools/robot/README.md)).

**The robot's proof.** Two copies of the game on this PC (test copy `C:\Games\H5_test`, MMH5.5 RC19d), A1M4, 8 AI turns, link tuning on, same settings both times; the only difference is `--vlan`. The virtual LAN ran twice. Runs in `build/robot/` of the branch's worktree.

| | Game's own LAN (`20261102-192943-lan`) | Virtual LAN (`20261102-202355-lan`) | Virtual LAN again (`20261102-203056-lan`) |
|---|---|---|---|
| Client finds the host's game | yes, at 127.0.0.1:8889 (the robot's broadcast copies) | **yes, at 198.18.161.134:8889**: the host was detected as hosting 1 s after creating its game (the next probe), its announcement reached the client, the client's queries went to it (10 queries, one per second, each sent once) | yes, the same way |
| Joins, game starts | yes | **yes** | **yes** |
| AI turns, AI steps (host) | 8, 94 | 8, 99 | 8, 87 |
| World checksums | 1,461 checks, all equal | **1,359 checks, all equal (no desync)** | **1,362 checks, all equal** |
| Wait before an AI step | median 204 ms (p90 402) | median 200 ms (p90 839) | median 206 ms (p90 433) |
| AI hero move | median 154 ms | median 174 ms | median 153 ms |
| Host's packets (sent + received) | 6,012 + 4,780 | 5,775 + 4,393 | 5,786 + 4,392 |
| **Packets holding an address of this PC** (scan of every packet, both copies) | host 119, client 119 | **0 and 0** | **0 and 0** |
| Virtual LAN counters (client, end) | | 4,476 packets to the host, 5,568 from it; 0 that could not be sent, 0 stale, 0 to unknown addresses; 144 real broadcasts (`lan_discovery`'s copies) dropped | 4,470 to the host, 5,579 from it; the same zeros |

- **No real address in any packet**: the address scan looked at all 20,513 packets of the two copies (20,934 in the second run) for the PC's 10 addresses. None held one in network byte order. A few per copy (3, then 7) held `01 00 00 7f` (127.0.0.1 backwards) by chance, as without the virtual LAN, inside acknowledgement bits and AI command data; the robot logs those packets whole. The packets show the fake addresses exactly where the packet map has the real ones (`03` bytes 355, 375, 772, 792; `01` and `04` byte 13; the lobby's records).
- **The game sent no `07` address probes at all** (24 in the run without it). It seems to probe only when the address its partner's packets come from is not in that partner's list; with the virtual LAN, it always is. One less thing to carry.
- **The host keeps answering queries during the game** (80 of 90 probes answered by the end), so "hosting" lasts as long as the game: the Steam lobby (step 3) can follow it.
- Once the client joined, the host's game description grew to **984 bytes** (it then lists the client too). Still under one Steam packet (~1,200 bytes).
- Timing: **no difference**. The second virtual LAN run matches the run without it (median wait 206 vs 204 ms, p90 433 vs 402, AI hero move 153 vs 154 ms, 0.21 vs 0.20 s per AI step). The first run's higher p90 was one slow AI turn: the AI does not play the same game twice (an earlier run without the virtual LAN had a p90 of 638 ms). Each message adds one hop through a local socket, read every time the game polls (every few ms).

**What step 3 (the Steam transport) needs from this:**

- A second `vlan::Transport`, on `ISteamNetworkingMessages`: `Send` = `SendMessageToUser` on channel = port with `UnreliableNoNagle | AutoRestartBrokenSession` (now `ReliableNoNagle`: see "Send flags" above); `Receive(channel)` = `ReceiveMessagesOnChannel(channel)`; `SelfId` = our SteamID (only known after Steam's start: the core already waits for it, and gives the game no address until then); `SetHosting(port)` = create or leave the friends-only lobby and put the discovery port and our versions in its data; `Hosts` = friends' lobbies whose data says they host (`GetFriendGamePlayed`), refreshed every few seconds; `Members` = the lobby's members, **in the lobby's order**, so every PC gives the same addresses when two ids clash (the core adds them in that order).
- The session requests (accept only lobby members and friends with the add-on), Steam's callback thread, and the same lock as the hooks (the core's `g_lock`).
- `[online] mode=steam` in `InstallVirtualLan` (today it logs "not built yet"), loading `steam_api.dll` as the spike does.
- Keep the lobby while the game runs, even if a query goes unanswered for a moment.

**Not covered yet:** three or more PCs (the runner starts two copies; the unit test checks that three PCs agree on the addresses, and the transport's member list gives every PC every address before a first packet), a saved game loaded in multiplayer, other maps, and a real network between two PCs (step 5).

## Step 3: the Steam transport (2026-11-02)

**What was built** (`[online] mode=steam`; `mode=off`, the default, still hooks and loads nothing):

- `src/mods/virtual_lan_steam.cpp`: the second `vlan::Transport`, on `ISteamNetworkingMessages`:
  - **our id** is our SteamID; Steam starts at the game's first DirectInput call (`StartVirtualLan` in `InstallAll`), long before the LAN screen. If it cannot (no `steam_api.dll`, Steam not running), the hooks stand aside and the game plays its own LAN, with a log line;
  - **messages**: `SendMessageToUser` on channel = the game's port, `ReliableNoNagle | AutoRestartBrokenSession` (unreliable until the 4G test); `ReceiveMessagesOnChannel` when the game reads that socket; a message's age comes from Steam's own clock, used only to count and log late messages since 0.3.5's test9 (the 3-second staleness rule is gone); a message to ourselves stays in this PC;
  - **the host's lobby**: friends only, made when the core says the game hosts, kept while it hosts and **a minute after** (the step 2 runs showed the host's answers have short gaps), then left. Its data: `h5t_vlan` (protocol 1), `h5t_version`, `h5t_game` (the game build and a hash of MMH5.5's `55u.dll`: `mmh55/276f239a` for RC19k), `h5t_host` (the host's SteamID), `h5t_port` (discovery port), `h5t_ip` (its fake address), `h5t_ips` (the fake addresses in its game). Every member also puts its own fake address in its member data (`h5t_ip`);
  - **known hosts**: every 2 s, the Steam friends playing app 15370 in a lobby (`GetFriendGamePlayed`); the lobby's data is asked for (`RequestLobbyData`, every 10 s) and read without joining. A host is a friend whose lobby says that friend hosts the same protocol and game. Other cases are logged once (another game build, another protocol, the friend in somebody else's game, no longer hosting). **If the data never comes** (Steam's documents say friends can read it; untested), the friend's game is tried anyway on the usual port 8889 after 6 s;
  - **joining**: when the game sends a known host more than queries (its join request), the add-on joins that host's lobby, keeps it while packets go either way and a minute after, and leaves when the host leaves. `Members` = the lobby's members in the lobby's order, with their declared addresses;
  - **sessions**: accepted only from Steam friends and members of our lobby; others are ignored (logged once each);
  - **one thread of ours** runs Steam's callbacks (manual dispatch) every 20 ms under the core's lock, as every hook does;
  - **logs** `vlan steam: …`, with no SteamID or name: players and lobbies are short hashes (`#3f2a`).
- `src/mods/steam_runtime.cpp`: loading `steam_api.dll` and starting Steam, now shared by the spike and the transport (the spike stays off with `mode=steam`).
- The core (`virtual_lan.cpp`) changed in two ways:
  - **Declared addresses.** The header grew from 2 to 6 bytes: the source port and the **sender's own fake address**. A receiver takes the address a peer says its game uses; a computed address gives way to a declared one. Our own address moves off an address a known host's game uses (`h5t_ips`) or that a peer declares, but only until our game has told it to anybody (its first packet to a peer, or when it starts hosting); after that it is final. Before, each PC put itself first in its table, so two players whose ids hash to the same address (about 1 in 20,000 for 4 players, but then always for that pair) would have disagreed.
  - Short ids in the log instead of whole ids.
- The steps table's order for the lobby ("members in the lobby's order") is kept, but the declared addresses make the order matter only for computed (undeclared) peers.
- Tests: `src/hook/test/virtual_lan_test.cpp` has **161 checks** (87 before): declared addresses, moves and clashes, the 6-byte header, lobby data in and out, the host checks, the lobby plan (create, keep a minute, leave; join, keep, leave; retry after a refusal; GetTickCount wrapping). `steam_abi_check.cpp` now also compares the **shape of every flat function** we call (return and argument sizes) with Valve's declarations, plus the new constants and offsets: all pass with SDK 1.65.
- The robot: `run_ai_test.py --online-setting mode=steam`, and `--trace-crc`, `--net-log`, `--net-scan` for a single copy (the two-PC test's host).

**Robot runs** (test copy `C:\Games\H5_test_k`, MMH5.5 RC19k; results in the branch worktree's `build/robot/`):

| Run | What | Result |
|---|---|---|
| `20261102-225858-lan`, loopback (`--vlan --net-scan`), two copies, A1M4, 8 AI turns | nothing regressed with the 6-byte header and declared addresses | client found and joined the host through the virtual LAN; 9 AI turns, 105 AI steps; **1,568 world checksums, all equal**; median wait before an AI step 199 ms (p90 620); **0 of 22,722 packets** (host 6,241 sent + 4,830 received, client 5,486 + 6,165) hold an address of this PC; 0 messages that could not be sent, 0 stale, 0 to unknown addresses |
| `20261102-230235`, `mode=steam`, one copy, the robot hosts a LAN game | Steam in the game, the lobby, the LAN screen | `SteamAPI_InitFlat` OK in 94 ms (app 15370, the robot started the game, not Steam); relay network OK after 6.4 s (25 relays); the friends list read (0 of 85 friends playing); the LAN list opened, queries went nowhere (no friend hosting) and nothing went to the real network; when the game created its game, **the lobby was made in 0.3 s** and its data set (protocol 1, discovery port 8889, our fake address); the host answered 169 of 180 hosting probes and the lobby stayed for the whole run. (The game itself cannot start a LAN game with one human: "can start 0", the game's rule.) |
| `20261102-231509`, single player A1M4, `mode=steam` **without** `steam_api.dll` | the fallback | `cannot load …steam_api.dll (error 126)`, then `the steam transport cannot carry the game's packets: the game's own LAN play instead`; the game ran as usual: 3 AI turns, 22 AI steps, no crash |
| `20261102-231537`, single player A1M4, `mode=steam` with `steam_api.dll` | a normal game with Steam on | Steam OK in 109 ms, relays OK at 6.2 s; 3 AI turns, 25 AI steps, as without the add-on's online part |

**Not covered yet:** the two-PC test (the real game between two Steam accounts: [online-steam-test2.md](online-steam-test2.md)); whether Steam shows a friends-only lobby's data to a friend outside it (else the fallback above); three PCs; a relayed path.

## Step 4: the online UI (2026-11-03)

The owner's rule for any UI in the game: as close to the game's own look as possible (its art, frames, fonts and buttons; its own screens as the entry points; no modern windows). So each piece sits on the game screen where a player looks for it.

### What the player sees, screen by screen

| Where | What | How it is made |
|---|---|---|
| **Options > H5 Together**, page 2, first group "Online play" | A row **"Online play \*"** with a list: **Off (local network)** (the default) / **Steam**. Under it one line: "Friends' games: Multiplayer, LAN", or, when `bin\h5-together\steam_api.dll` is missing, "No steam_api.dll: Steam not available". The tooltip says what Steam mode does and that it needs Steam and the game bought on Steam. | The settings menu's own rows (the game's Options screen, served `uiconsts` file: `settings_table.cpp` ROW, group `kOnline`, `settings_menu_data.cpp`). The "\*" means "after a restart" (below). Greyed out during a multiplayer game, like the host's and the advanced rows. |
| **Multiplayer > LAN** (`NUI::CLobbyScreen`) | In the big empty box under the games list (the ubi.com chat's box, unused in LAN play): "Online play over Steam", then "Steam: 2 friends are playing with H5 Together." (or none), "1 more is playing Heroes V without H5 Together's online play.", the invites ("Lucas invites you: their game is in the list above."), and the friends' games this PC **cannot** join ("Pierre hosts a game you cannot join: another version of MMH5.5 or of the game.", "Marie hosts with H5 Together 0.4.0, which cannot play with yours (0.3.0): update."). Steam not available: "Steam is not available: Valve's steam_api.dll is missing from bin\h5-together." (or: Steam is not running; must be updated; refused; did not answer), then "The list shows the games of your local network, as without H5 Together." The games themselves stay in the game's own list above. | Drawn by our overlay (`online_ui.cpp`) in the game's colours and with the shadowed text of the waiting room's versions line. |
| **The waiting room** (`NUI::CMPWaitScreen`), host only | **"Invite friends..."** next to "Rules...", the game's own button picture. It opens **Steam's own invite dialog** (the overlay) when the overlay is enabled in this game; else, or if the overlay did not open within 2.5 s, **our list**: a panel in the game rules panel's look, "Invite friends", the Steam friends who are online (in Heroes V with H5 Together first, then in Heroes V, online, busy, away), each with an "Invite" button that becomes "Invited". | The Rules button's way (overlay, the game's button art, `NineSlice` so the wider button keeps its ends); the list is a modal overlay panel like the Rules panel. Shown once our Steam lobby exists (a second after the game is created). |
| **An invite** (accepted in Steam, "Join game" in Steam's friends list, or Steam starting the game with `+connect_lobby <id>`) | On the main menu, for 20 s, a banner above the menu's scroll: "Lucas invites you to play: open Multiplayer > LAN." When the game is not the window in front: the taskbar button flashes, the sound and the Windows pop-up of the "your turn" notification (with `[notify] sound=` and `popup=`). The inviting friend's game is looked for (its lobby's data, without joining) and goes into the known hosts, so it shows in the LAN list even if Steam's friends list does not show its lobby. **Then the game joins by itself** (the evening's follow-up, [below](#follow-ups-2026-11-03-evening-auto-join-and-the-games-message-boxes)): it opens Multiplayer > LAN, waits for the host's game in the list, chooses it and presses Join. | Overlay banner (the update notice's look); `notify.cpp`'s flash and pop-up (`NotifyInvite`); the game's own menu command and Join (`online_join.cpp`). |

French words: every text is in `src/mods/texts/en.txt` and `fr.txt` (`[online]` and `[settings]`); the French uses the game's own names "Multijoueur > Réseau local" (from the game's `texts.pak`). The robot's screenshots are French because the test copy's game is French and `[ui] language=auto` follows the game's texts ("texts: fr, from the game's texts (the red player is "Joueur rouge")").

### Why these techniques

- **The Options row** is fully native (the game's own list control), as the rest of our settings menu.
- **The LAN screen's lines** are drawn, not served. A served text file (as the Options page does) is read by the game once, when it builds the screen. These lines change while the screen is open: a friend starts hosting, an invite comes, a friend's lobby data arrives two seconds later. Drawing also needs no new knowledge of the screen's code. They use the game's colours and the shadowed text of the "Rules..." button and the waiting room's versions line, which the owner already accepted. The font is the overlay's (Tahoma Bold), as in our other panels: the game's own italic serif is not a font file our overlay can load today (the game's data has no `.ttf`; a closer font would be a change to the shared overlay, for every panel at once).
- **"Invite friends..."** copies the "Rules..." button exactly (same picture, same row, same input handling: it reads the mouse itself, the game behind gets nothing to click there).
- **Steam's dialog first**: it is Steam's own window, which players know, and it shows every friend. Our list is the fallback when the overlay is off.
- **Joining by itself after an invite**: first left out (a hook in the main menu's pages, and moving the player unasked). The owner tested invites on 2026-11-03 and asked for it, so it was added in the evening ([below](#follow-ups-2026-11-03-evening-auto-join-and-the-games-message-boxes)): the game's own commands, as if the player clicked.

### When the setting applies, and the no-Steam case

- **After a restart** (the "\*" mark): the Winsock hooks go in when the DLL loads and Steam starts at the game's first DirectInput call, both long before the LAN screen; with `mode=off` nothing is hooked or loaded at all (the promise of the default). Making it apply at the next LAN screen would mean hooking with `mode=off` too, and starting Steam in the middle of a session; not worth it for a setting a player sets once.
- **No `steam_api.dll`** (a GOG copy, or the file not installed): the Options row still shows, with "No steam_api.dll: Steam not available" under it. With `mode=steam` the add-on logs it, the LAN screen says why in its first line, and the game plays its own LAN, as with `off`. Nothing else changes. The other reasons (Steam not running, Steam too old, Steam refused, Steam did not answer) are told the same way (`steamrt::LastFailure`).

### How it works inside

- **Friends who play with H5 Together**: when Steam starts, the add-on sets our **rich presence** `h5t_version=<our version>`. Steam passes rich presence to friends playing the same game. A friend playing app 15370 whose `h5t_version` is set has online play on; one without it plays Heroes V without it (or an older H5 Together). The add-on asks Steam once per friend (`RequestFriendRichPresence`).
- **The status**: the Steam transport's thread fills an `online::Status` at every tick (20 ms) under the core's lock: Steam's state, the relay network, the friends' counts, the known hosts, the games that cannot be joined (from `HostCheck`: another game build, another protocol), the invites (newest first), whether our lobby exists, whether the overlay is enabled. The UI copies it each frame (`mods::OnlineStatus`).
- **Steam calls stay on the transport's thread**: the UI only asks (`OnlineInviteDialog`, `OnlineInvite`, `OnlineWantFriends`); the thread makes the calls (`ActivateGameOverlayInviteDialog`, `InviteUserToLobby`, the friends list) on its next tick. The pop-up and the flash of a new invite are called by that thread after it has left the lock.
- **Invites**: `GameLobbyJoinRequested_t` (callback 333) and `+connect_lobby` both add the lobby to a list kept 10 minutes. Every 2 s the lobby's data is asked for (`RequestLobbyData`, without joining) and checked like a friend's lobby (`CheckHost`); a joinable host goes into the known hosts, so our queries reach it and its game shows in the LAN list. When the player then joins from the list, the usual path (step 3) joins the host's lobby.
- **New flat functions** (`steam_api_flat.h`, checked by `steam_abi_check.cpp` against SDK 1.65): `SetRichPresence`, `GetFriendRichPresence`, `RequestFriendRichPresence`, `GetFriendPersonaState`, `ActivateGameOverlayInviteDialog`, `InviteUserToLobby`; the callback `GameLobbyJoinRequested_t`; `GameOverlayActivated_t`'s fields; `EPersonaState` values.
- **Logs** (no names, no SteamIDs): `online: ...` lines (the UI), `vlan steam: our rich presence ...`, `Steam's overlay is enabled now, N s after Steam's start`, `Steam's invite dialog asked for`, `Steam's overlay opened`, `an invite ...: lobby #…`, `the invite's lobby #…: ok: its host gets our queries`, `invited friend #… -> sent`.
- **Tests**: `src/hook/test/online_ui_test.cpp` (the LAN screen's lines in every state, the banner's words in English and French, Steam names cut for the screen, the list's order, `+connect_lobby` parsing, the made-up friends, every word in both files). `settings_test.cpp` checks the new row with the others. **For screenshots with one PC**: the hidden setting `[online] ui_test=1` adds made-up friends (two with H5 Together, a game of another MMH5.5, one of another H5 Together, a list of six friends) and the robot's `panel|online test_invite`; inviting a made-up friend calls no Steam function. Robot steps: `panel|online invite` (as a click), `picker`, `close`, `pick <row>` (ui_test only: a robot never invites a real friend).

### IsOverlayEnabled

| Case | Result |
|---|---|
| The robot started the game (not Steam), 3 runs on 2026-11-03 | **not enabled at Steam's start, enabled 5 s later** (`Steam's overlay is enabled now, 5 s after Steam's start (started without Steam)`). "Invite friends..." then asked for Steam's dialog and **the overlay opened** 16 ms later (`vlan steam: Steam's overlay opened`). |
| The spike (2026-11-02), robot-started | the same: not at init, yes at 45 s (then checked only once more) |
| Steam started the game | **not checked yet** (the test copy is not Steam's game folder). The log line says it: `Steam's overlay ... at Steam's start (Steam started the game)` and later changes. The laptop of the two-PC test starts the game from Steam: its log will tell. |

So the add-on checks again every 5 s and decides at the click; if the dialog does not open within 2.5 s, our own list opens.

### Robot runs (test copy `C:\Games\H5_test_k`, MMH5.5 RC19k, French; results in the branch worktree's `build/robot/`)

| Run | What | Result |
|---|---|---|
| `20261103-074333`, `mode=steam`, `ui_test=1` | the whole UI with made-up friends | the banner on the main menu, the Options row ("Jeu en ligne \*: Steam"), the LAN screen's lines (2 friends with H5 Together, 1 without, an invite, two games that cannot be joined), the waiting room's "Inviter des amis..." next to "Règles...", our list (6 made-up friends), two of them "Invité" (no Steam call: `ui_test: a made-up friend marked as invited (nothing sent to Steam)`); the lobby was made as in step 3 |
| `20261103-074550`, `mode=steam` | the real thing (0 of 85 friends playing) | "Steam : aucun de vos amis ne joue avec H5 Together pour l'instant."; the button; Steam's invite dialog asked for, the overlay opened (no invite sent: nobody clicked in it) |
| `20261103-074712`, `mode=steam` **without** `steam_api.dll` | the no-Steam case | the Options line "Pas de steam_api.dll : pas de Steam"; the row set to "Désactivé (réseau local)" as a click would; the LAN screen: "Steam n'est pas disponible : le fichier steam_api.dll de Valve manque dans bin\h5-together." and "La liste montre les parties de votre réseau local..."; the game's own LAN as before |

**Not covered yet** (needs two PCs and two Steam accounts, the evening test): an invite received for real (`GameLobbyJoinRequested_t`) and from the friends list's "Join game"; `+connect_lobby` when Steam starts the game (only parsing is unit-tested); the pop-up of an invite while the game is in the background; "N friends playing with H5 Together" counting a real friend (rich presence between two accounts); a friend's game of another MMH5.5 for real; `IsOverlayEnabled` when Steam started the game (and under Proton).

**The owner's hand test (2026-11-03 evening, package dev.201):** the desktop hosted and clicked "Inviter des amis...": Steam's overlay dialog opened. The laptop's game was not running: accepting the invite in Steam started it with `+connect_lobby`, the main menu's banner showed, and the host's game was in the LAN list at once. "It works extremely well." Two follow-ups came out of it (next section).

### Follow-ups (2026-11-03 evening): auto-join, and the game's message boxes

**1. Joining by itself after an invite** (`src/mods/online_join.cpp`, the owner's answer to question 3). When an invite comes (Steam's, or `+connect_lobby` at launch) and the player is on the main menu (or comes back to it within 10 minutes), the add-on does what the player would click, with the game's own code:

| Step | How | Robot finding |
|---|---|---|
| Main menu → Multiplayer > LAN | the Multiplayer page's own command `"lan"` (`NUI::CMultiPlayerSubmenu::OnCommand`, `0x00699730`, slot 1 of vftable `0x00e318b8`); the page object comes from a hook on slot 0 (`SetOwner`, `0x006994a0`, called when the main menu is built), stacked under the robot's own hook of that slot | the LAN screen opens in the same frame |
| Wait for the host's game in the list | every 250 ms, the list's rows (as `robot_lobby.cpp`'s `PickGame`); a row's game data (`NUI::CGameData`) holds the host's server address at **+0x18** (its `sockaddr_in` at +0x14: the join code `0x006fb9a0` reads data + 0xc onwards). The virtual LAN gives each player one fake address (the host's: from its lobby data, or from the peer table once heard), so the right row is the one whose +0x18 is the host's | the host's 198.18.161.134 at +0x18 (`its data +0x18`) |
| Choose it, then Join | the list's own "highlight" (`IListControl` slot 10, as a click), then, **on a later frame** (0.6 s), the LAN screen's own `"join"` (its `IProgrammedReactionsAndChecks`, found from the object's RTTI; React `0x006fd4f0`, which passes `"join"` to its base's `0x006f7a00`) | Join pressed in the same frame as the highlight did nothing: the screen takes the highlighted row as its choice in its own update |
| Done | the waiting room (`NUI::CMPWaitScreen`) on top | 2 s from the invite to the waiting room, and the game played |

What the LAN screen says meanwhile, in the box under the list: "Lucas vous invite : en attente de sa partie dans la liste ci-dessus, puis connexion automatique.", then "Lucas vous invite : connexion à sa partie...". If the host's game does not show in 20 s: "... mais sa partie n'est pas apparue dans la liste : choisissez-la quand elle apparaît, puis Rejoindre." If Join does not lead to the waiting room (a message box, or 30 s): "... mais la connexion à sa partie n'a pas marché : choisissez-la dans la liste et cliquez à nouveau sur Rejoindre." The player stays on the LAN screen in both cases.

Safety: it acts only on the main menu and the LAN screen (never in a game, the waiting room, Options...), never while a message box or the name box is open over the LAN screen (it waits, and its 20 s start again), once per invite, and an invite older than 10 minutes is dropped. If the player leaves the LAN screen, it stops. If the game's code is not as expected (the vftable slot, the screen's buttons), it does nothing and the banner remains.

**The player's name:** joining does not need the LAN screen's name to be typed: the game takes it from the profile (`lan_last_login`, set the first time the player enters a name). If the name box (`NUI::CSimpleEditBox`, a screen over the LAN screen) is open, the auto-join waits until it closes. (An empty name could not be made in a robot run: the game keeps the last one; a host with a broken name could not create its game at all, `20261103-193931-lan`.)

**2. Our lines were drawn over the game's message boxes** (the laptop's screenshot: "Timeout. May be no more free slots available in the game. Error code 0.18.0" with our lines over its left edge). What the robot found:

| Box | What it is | Seen by TopScreen? |
|---|---|---|
| the exit question (main menu, `msg|main_menu_exit`) | `NUI::CExitGameQuestionScreen`, a screen pushed on the list | yes |
| the LAN screen's name box (`ui|NUI::CLobbyScreen changename`) | `NUI::CSimpleEditBox`, a screen | yes |
| the join's "Temps écoulé... Code d'erreur 0.18.0" (reproduced with the new hidden `[online] test_drop_join=1`: the virtual LAN loses our join request) | `NUI::CCommonMsgBox`, **not in the screen list**: TopScreen still says `NUI::CLobbyScreen` | **no**: hence the bug |

So the add-on now watches the game's common message boxes itself: a hook on the call sites of `NUI::CCommonMsgBox`'s constructor (`0x0067a730`, 9 sites; it also builds `CCommonMsgBoxWithUpdatableText`) remembers each box; a box lives until its destructor changes its vftable. Not every box is shown: after the error box closed, another one was made (at `0x0067af1b`) and never shown; comparing the two objects in the log gave the test: a shown box has its fields +0x14 and +0x18 set (+0x18 looks like its parent window), the never-shown one has both 0. While a box is shown, nothing of ours is drawn (the LAN screen's lines, the banner, the button), and the auto-join waits. Log: `online: game message boxes shown: 1; new: .?AVCCommonMsgBox@NUI@@, made at 0067919d; one shown`.

**Robot runs** (two copies, the loopback virtual LAN, `run_lan_test.py --vlan --client-autojoin 20`; French; screenshots in the session's scratchpad `online-ui-shots\autojoin\`):

| Run | Result |
|---|---|
| `20261103-194501-lan` | the client gets a made-up invite on the main menu; the LAN screen opens, "Lucas vous invite : en attente...", the host's game shows 1 s later, chosen, Join; the waiting room 2 s after the invite; the game started and played |
| `20261103-194549-lan` (`--client-online test_drop_join=1`) | the join request is lost: the game's error box after 6 s, with **nothing of ours over it**; after the box closed, "Un ami vous invite, mais la connexion à sa partie n'a pas marché : ..." |
| `20261103-191759-lan` (before the fix) | the same box with our line over it, as on the laptop |
| `20261103-194255-lan` | the name box opened over the LAN screen before the host's game showed: the auto-join waited (no "stopped") |

New robot tools: `panel|online test_autojoin [name]` (an invite to the first host this PC hears of), `panel|online games` (each row's game data, and where a fake address sits in it), `run_lan_test.py --client-autojoin N`, `--host-online` / `--client-online KEY=VALUE`, `--client-setvar`.

**Not covered yet:** the auto-join after a real Steam invite between two PCs (the robot's invites are made up; the real path only adds the invite's host and lobby), and after `+connect_lobby` (the lobby's data must come first: then the host is known).

### Follow-up (2026-11-04): a loaded game's waiting room

In 0.3.0 a host who loaded a saved game had no "Invite friends..." button (a known issue of the 0.3.0 release, from the owner's game of 2026-11-03). The owner's log said why: a loaded game's waiting room is another screen, `NUI::CMPUbiComLoadScreen` (over the setup screen), not `NUI::CMPWaitScreen`. The way there: setup screen, **Load** (`NUI::CLoadScreen<SLanSaveHeaderInfo>`, the LAN saves), then `NUI::CUbiComLoadScreen<SLanSaveHeaderInfo>` for a moment, then the waiting room. The friend who joins a loaded game gets the same screen, over the LAN screen. The Steam lobby is made as for a new game (the game hosts as soon as the waiting room opens).

- `online::WaitingRoom(screen)` (`online_ui_logic.cpp`, unit-tested) says whether a screen is a waiting room, a new game's or a loaded game's. The button and our friends list use it, and so does the auto-join's last step (before, a friend's auto-join into a loaded game ended after 30 s with "another screen" instead of "done"; harmless, but the log was wrong).
- The button is at the same place: a loaded game's waiting room has the same title row, without "Rules..." (a loaded game keeps its rules).
- The robot's `save|<name>` step now also picks a save on the LAN load screen. Its entries are bigger than single-player ones; the row's place in them (+0x298 in RC19k) is found from the row the list highlights when it opens.

Robot run `20261104-074436` (test copy `C:\Games\H5_test_k`, French, `mode=steam`, `ui_test=1`; the host's multiplayer autosave "Sauvegarde auto 1" copied in): Multiplayer, LAN, Create, Load, the save, Load. The log says `our Invite friends... button is drawn in the waiting room of a loaded game`; the screenshot shows "Inviter des amis..." on the title row of "Options du mode Standard"; `panel|online invite` opened our list of made-up friends, and Close closed it.

## Steps and effort

A session is one focused working block of a few hours.

| Step | What | Sessions |
|---|---|---|
| 0 | The spike (above), then reading the logs. **Done 2026-11-02** (solo and two PCs) | 1 + 0.5 |
| 1 | Packet map: the robot's `net_log` logs whole packets; find the address lists inside `01` and `03`; write it up in [multiplayer-sync.md](../subsystems/multiplayer-sync.md). **Done 2026-11-02** ([results](#steps-1-and-2-the-packet-map-and-the-virtual-lan-core-2026-11-02)) | 0.5 |
| 2 | Virtual LAN core with the loopback transport: Winsock hooks (stacked with `lan_discovery`), fake addresses, `gethostbyname` answer, hosting detection, unit tests, robot LAN game (checksums, no real IP in packets). **Done 2026-11-02** ([results](#steps-1-and-2-the-packet-map-and-the-virtual-lan-core-2026-11-02)) | 2 |
| 3 | Steam transport: lazy loading, init, our callback thread, lobbies, known hosts, sessions, version data; logs. **Built 2026-11-02** ([results](#step-3-the-steam-transport-2026-11-02)); the two-PC test of the real game is [online-steam-test2.md](online-steam-test2.md) | 2 |
| 4 | UI: the Options row (Off/Steam), Steam's lines on the LAN screen (status, games with problems, invites), "Invite friends..." (Steam's dialog or our list), the invite's banner and pop-up, French texts. **Built 2026-11-03** ([results](#step-4-the-online-ui-2026-11-03)) | 2 |
| 5 | Real tests with the owner and friends (Windows, Linux, 3 PCs) and fixes | 1–2 |
| 6 | Package: `steam_api.dll` in `bin\h5-together\`, install guide, release notes | 0.5 |

About **10 sessions** after a successful spike. Steps 1 and 2 need no SDK and could start before it arrives.

## Risks and fallbacks

| Risk | What happens | Fallback |
|---|---|---|
| Steam refuses app 15370 (Init, lobbies or relay) | No Steam play under the game's own id | (a) **Spacewar 480**: works for anyone, shows "Spacewar" in the friends list, and its lobby pool is shared with the world (filter by our key). Valve means it for testing, but mods use it. (b) Our own Steam app id: a $100 Steam Direct fee, and as a tool for Ubisoft's game it would need their permission, so unlikely. (c) **Our own lobby server plus [GameNetworkingSockets](https://github.com/ValveSoftware/GameNetworkingSockets)** (open source): direct connections through routers (ICE/STUN), but a relay (TURN) server costs money; this was the original phase 6 plan. (d) Keep ZeroTier, which works today. |
| Valve or Ubisoft object to using 15370 | We must stop using it | The transport is swappable: 480, our own server, or ZeroTier. |
| `SteamAppId` doesn't work when the game isn't started by Steam | Shortcut and GameRanger players get no Steam | Start the game from Steam (launch option to MMH5.5's exe), or a `steam_appid.txt` in the player's `bin\` (one small file; Valve says not to ship it). |
| A Linux friend outside Steam (Lutris, plain Wine) | No Steam | ZeroTier, as today. |
| Some packet carries real addresses we didn't catch | The `07` probes go nowhere for those; joining still works through the fake address, but a real IP could leak to friends | Checked in steps 1 and 2: every address comes from `gethostbyname`, and with the virtual LAN on the robot's scan found none of this PC's addresses in any packet. Left to watch: other maps, save games loaded in multiplayer, 3+ players (not run yet). The robot's `--net-scan` checks again; the fallbacks stay the vtable slot 11 or a packet rewrite. |
| The overlay and our ImGui fight over `Present` | Crashes or flicker | Test in the spike; the overlay is optional (our own invite list). |
| Licence grey area | Valve may end the licence | Players copy `steam_api.dll` themselves; or fallback (c). |

## Questions for the owner

1. OK to try the game's own app id 15370 first, with Spacewar 480 only as a test if 15370 fails?
2. ~~Who can be the second PC for the spike?~~ Answered: the owner's Linux laptop with a second Steam account (2026-11-02).
3. When a game is shown to others: **friends only** (recommended), or public for anyone with the add-on?
4. Does anyone in the group play from a GOG copy? Then that group's games stay on ZeroTier, or we use Spacewar 480.
5. OK to ship Valve's `steam_api.dll` inside our package (allowed by the SDK agreement as we read it, but a grey area because the app id isn't ours)? Or should each player copy it?
6. How do the Linux friends start the game: as the Steam game with a launch option, as a non-Steam shortcut, or with Lutris/Wine? (The Steam game with the launch option works for Steam's networking: the two-PC test.)
7. ~~Steps 1 and 2 need no SDK. Start them before the spike?~~ Done after the spike (2026-11-02).
