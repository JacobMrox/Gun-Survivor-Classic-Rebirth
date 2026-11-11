# MODLOG — Biohazard Gun Survivor (RE: Survivor) Chinese/Taiwanese PC port

Game: Biohazard Gun Survivor (Resident Evil: Survivor), Chinese PC port — native x86 D3D8
Idea: Fix the rest of the issues plaguing this port using current dd8 wrapper source (Gun-Survivor-Classic-Rebirth)
Done means: working in-game fix + 20-45s showcase clip
Safety: single-player/offline only — no online anti-cheat concerns. Never ship game files/decompiled code.

## 0. Intake (2026-11-07)
- Repo: C:\Users\user\source\repos\Gun-Survivor-Classic-Rebirth (d3d8.dll wrapper, source/ in this repo)
- Session dir: C:\Users\user\source\repos\Tool Hazard (knowledge/tools, not the wrapper)
- Done = in-game working + clip per skill step 0

## 1. Recon (skill: game-recon analogue)
- Knowledge base: universal-modder knowledge/INDEX.md searched — no Gun Survivor classic note; only RE2 (2019) RE Engine note (not applicable, RE Engine != 2000-era native).
- `um scan` unavailable (no um / no npx / no node on this machine). Manual recon instead:
  - Wrapper is a proxy DLL: d3d8.dll (d3d8to9 D3D8->D3D9) + survivor_pc.cpp hooks + AviFfmpegHook/DirectShowHook movie replacement.
  - Exe: gun_bio.exe, image base 0x00400000, verified signatures in source/survivor_pc.cpp (e.g. 0x005FD0BC input, 0x00436E40 mapper, 0x005F2C14 skip byte).
  - Save/config: config.ini + d3d8.ini [SurvivorFix] section; per README, GBIO.CFG dummy currently required at startup.
- Community route: already established — native hooks via proxy DLL. No official loader for this title.
- Docs read: docs/cerberus-investigation.md, nab-mesh-crash-investigation.md (resolved: Tool Hazard packer aux-array bug, not a global triangle limit), player-door-stair-compatibility.md, title-new-game-fade.md, ps1-enemy-and-foot-research.md, zombie-*-audio docs.
- GitHub page https://github.com/JacobMrox/Gun-Survivor-Classic-Rebirth — Issues: 0 open. All tracking is README "Project Status & To-do" (v1.2 dev). List captured at top of this log in session output.

## 2. Route picked
- Native hooks (proxy DLL d3d8.dll + MinHook, Win32 API hooks for DirectShow/MCI, ffmpeg for movies).
- Reason: no loader/data API for this 2000-era title; wrapper already covers DPI, crosshair sync, D3D8->D3D9, PS1 zombie/player adapters, movie system.
- Reason recorded here before building, per skill.

## 3. Lab setup (TODO before first modded launch)
- Backup saves/config with a snapshot tool before touching game folder (um backup equivalent — take manual copy + note restore path).
- Use a separate game copy / mod-manager profile if available; never overwrite the only install.
- Windowed mode at fixed client size (existing KeepAspect/ForceWidth x ForceHeight) for stable screenshots.
- Decompiled/RE notes stay outside git (RESpect PS1 decomp baseline is GPL-3.0 — attribution required for any adapted code).
- Keep all edits in source/ of this repo (survivor_pc.cpp, dllmain.cpp, AviFfmpegHook.cpp, DirectShowHook.cpp, cd_bypass.*) and note each in this log.

## 4. Source of truth (read, don't guess)
- source/survivor_pc.h — [SurvivorFix] toggles, HudAspect, shadow blend, screen-fx, title transition.
- source/survivor_pc.cpp — PS1 zombie/player adapters, height support, death-pose hold, mouse handling.
- source/dllmain.cpp — D3D8 create/reset, config load, DirectShow/AviFfmpegHook install, GBIO.CFG handling.
- source/DirectShowHook.cpp — debug.txt redirect already present (lines ~432, 440, 463, 470). Verify if fully wired.
- source/AviFfmpegHook.cpp — movie replacement; few movies still failing per README.
- RESPeCt decomp (Survivor PS1, GPL-3.0, port/decomp/, ~1,100 funcs, RAM-verified) — primary fix oracle; see docs/decomp-references.md.
- bio2-ds (RE2 DualShock SLUS_007.48, symbology from 1997 beta MAIN.SYM) — secondary, for third-person / engine symbols.
- Tests in tests/ (cerberus, config_defaults, held_frame, mouse_drain, movie_skip, player_wall_adapter, title_fade, zombie_*). Run target list before each change.

## 5. Vertical slice order (cheapest first, one at a time)
1. Remove need for GBIO.CFG — DONE: DLL auto-creates 16-zero-byte GBIO.CFG next to exe/dll (embedded IDR_GBIO_CFG in source/resource.h + Resource.rc, RestoreMissingGBIOConfig in source/dllmain.cpp; stock size verified 16x00 at E:\Games\Biohazard Gun Survivor\GBIO.CFG). Oracle: delete GBIO.CFG, launch, check recreated.
2. Remove debug.txt log printing — working on it (redirect to debug.log in DirectShowHook already present; verified E:\Games has both debug.log 7.8 MB and debug.txt 5545 bytes, so not fully eliminated yet).
3. Investigate few failing movies (ffmpeg) — need exact movie list + log.
4. Item prompt + ambush/enemy batch (README 2026-11-07 update): item pickup Done in local README; crowbar/crow-ambush/Mr. X/hunters/cleaners still Not yet. Crow flight anim phase 1 Done (type-12 mapping in ps1/crow_logic.h, tested).
5. Cerberus sound gait — DONE for sound gait (idle/walk/run at 0043D630 in zombie_common.cpp); larger AI/steering still outstanding per docs/cerberus-investigation.md.
5b. Debug menu phase 1 Done: F10 scrollable scene jump (00-79) via QuickSave reload (DebugQueueJump). Dummy names = hex ids — rename manually later.
6. Shooting screen flash (Not yet).
7. XInput + Raw Input — Not yet.
8. Key bindings — Not yet.
9. Mod Support — DONE phase 1: Gemini replica in source/mod_sdk.cpp (mods/mod_<id>/ + .7z auto-extract, manifest.txt Name/Title/Module, Module DLL LoadLibrary, CreateFileA override in cd_bypass.cpp, config [Mod] Active). Needs in-game oracle + proper lib (7z.exe shell-out currently).
10. README future goals (2026-11-07): PS1-based config menu, ragdolls, third-person (RE2 decomp), Nemesis Mode, multiplayer — all Not yet. Do not start without an explicit pick.

Circuit breaker: 3 repeats of same failure -> stop, log, change approach or ask user.

## Lab (install found 2026-11-07)
- Install: E:\Games\Biohazard Gun Survivor\gun_bio.exe (2,048,000 bytes, 2002-06-20, SHA256 2e5b7a3d...be3f9 — matches source/survivor_pc.cpp header).
- Backup: E:\Games\Biohazard Gun Survivor\Save-backup-20261107-204816 (Save/*.SAV + QUICK.SAV + config.ini + GBIO.CFG). Restore = copy back. Do not deploy new d3d8.dll over this folder without explicit OK (15x dog-experiment .bak already there).

Circuit breaker: 3 repeats of same failure -> stop, log, change approach or ask user.

## Log
- 2026-11-07: Skill loaded via direct fetch (npx unavailable). Recon complete. Awaiting game install path / backup confirmation before touching game folder. Starting with slice 1 (GBIO.CFG) once install confirmed.
- 2026-11-07 slices in source/ (no game launch yet — gun_bio.exe not found in quick scan): (1) data/FIXRES/GBIO.CFG + IDR_GBIO_CFG 2009 + RestoreMissingGBIOConfig() in dllmain.cpp + empty-resource fix in WriteEmbeddedResourceIfMissing; (2) zombie_common.cpp DogSoundDetour 3-state gait (idle mute, walk half-cadence, run normal) + per-dog g_dogWalkStep map; (3) source/mod_sdk.h/.cpp Gemini replica (folders + 7z cache, manifest parse, Module DLL load, cd_bypass CreateFileA override, dllmain ModSdk_Init). README Installation + Project Status updated. Next oracle needs game install path.
- 2026-11-07 GitHub README sync: added ambush batch (crowbar/crows/Mr. X/hunters/cleaners) + Future goals (PS1 config menu, ragdolls, third-person, Nemesis, multiplayer) into local README. GitHub page still behind on GBIO/crow/mod (local ahead).
- 2026-11-07 lab found: E:\Games\Biohazard Gun Survivor\gun_bio.exe SHA256 2e5b7a3d...be3f9 matches header; backup E:\Games\Biohazard Gun Survivor\Save-backup-20261107-204816. GBIO.CFG stock = 16 zero bytes (fixed data/FIXRES/GBIO.CFG + EnsureEmptyFileIfMissing to match).
- 2026-11-07 crow slice (first ambush pick): source/survivor_fix/ps1/crow_logic.h (kCrowType 12 = ENEMPROG 52-40, PickFlight/ApproachSelector from RESPeCt crow.c), config Ps1Crows (default on), CrowAnimMap wired in MapAnimDetour (zombie_common.cpp). tests/crow_logic_test.cpp passes (vcvars64 cl). Build: premake vs2022 + MSBuild Release 18.10 Win32 -> data/d3d8.dll (1,310,720 bytes, SHA256 E3F3347C...50BBB). Build needed vcpkg include (x86-windows/x86-windows) + AdditionalDependencies (avcodec/avformat/avutil/swscale/swresample/minhook.x32.lib) in build/*.vcxproj (generated; port to premake5.lua separately). Oracle still open: deploy data/d3d8.dll over E:\... (backup first) and verify stage 9 room 2 crow flying anim live.
- 2026-11-07 deploy fix: new d3d8.dll links minhook dynamically (LNK via minhook.x32.lib), so game folder needs source/vcpkg_installed/x86-windows/x86-windows/bin/minhook.x32.dll (18,944 bytes) beside d3d8.dll — previously statically linked, no DLL needed. Copied to E:\Games\Biohazard Gun Survivor\minhook.x32.dll. Retest with ShowConfigOnLaunch=0: PID 27172 stayed alive 18s, GBIO.CFG recreated (16x00), debug.log grew to 7,876,832 with CerberusSound + FFmpeg CAPCOM30. Remaining oracle: stage 9 room 2 crow flight anim visual check (human playtest) + Cleaners/Hank type id still TBD (hypnos.c = final boss per maintainer).
- 2026-11-07 F10 fix: toggle moved to EndScene poll (QuickSavePollWindowToggle) on F2+F10 via GetAsyncKeyState (DirectInput ate WM_KEYDOWN); window-message path now no-op. Notify "F2/F10 DEBUG OPEN/CLOSE" in hook.log + native FONT000 notice. Rebuilt data/d3d8.dll 1,314,816 bytes SHA256 6688DF79..., deployed, PID 10752 18s clean, log 7,996,077. Needs live press test.
- 2026-11-07 crow fix 4 (user live test: 1 landed, 1 loop, 1 stuck; others flapping = alive): (1) dead map forced to 13 for any dead crow (was only 0F/10); (2) DeadFall logs + stall snap (Y==0 freezes vel, kills bounce); (3) REVERTED: roomState zeroing broke room loads (black void, HUD only) — now preserves roomState again, only playerValid=0 (safe origin). Rebuilt data/d3d8.dll SHA256 C41444ED..., deployed (PID 17500 15s clean). Re-test F2 -> 0A (rooms should be visible again); crows may start mid-air stacked (known tradeoff) — next fix is per-crow perch teleport, not roomState wipe.
- 2026-11-07 crow phase 3 + debug menu polish: (1) Crow wing now via ReplacementScope to PSX_DATA/WAV/ENEMY/crow/SND-002.WAV (old family 9/2 played zombie moan); (2) dead requests 0F/10 force anim 13 (dead fall) + CrowDeadFall clamp; (3) DebugQueueJump exits inventory (main 6 + mode 3), clears playerValid for safe spawn. Scene labels 09 PROJ ROOM / 0A CROW HALL (Heliport id TBD). Rebuilt data/d3d8.dll 1,314,816 bytes SHA256 13825A0E..., deployed, PID 13772 18s clean, log 8,148,119. Live checks open: wing flap sound, mid-air death, F2 jump without inventory pop.
- 2026-11-11 scene 0x2D (stage 03 / room 0D, library corridor, STAGE03/030D/030D_B.HMD) unlit room fixed — commit db66a8f9 "Render the unlit library corridor dimly instead of pitch black" (pushed 32b11db5..db66a8f9, master). Root cause (exe 0x0045E030 = per-object room draw): the only reader of the object flags at +0x108 on the draw side is `test byte [esi+0x108], 4; jne skip`, and the per-frame mapper 0x004E12C8 writes bit 2 for every object while [0x005F44BC] (room light flag: 0 = switch off, 2 = on, set at 0x004E1D5C) is 0 — so the unlit room drew no objects at all and the frame was just the black clear, HUD and switch glow. Fix: hook 0x0045E030 in room_walls.cpp (RoomObjectDrawDetour); for scene 0x2D with flag==0 and bit 2 set, save ambient/lighting/lights 0-7, LightEnable all off, D3DRS_AMBIENT=0xFF323232, clear bit 2, call original, then restore. PS1 reference: the room there is nearly black except the switch glow, so dim-but-visible is correct (user confirmed with a PS1 capture).
- 2026-11-11 scene 0x2D cleanup in the same commit: removed the earlier global white-out scaffolding that made the room flat once it was lit — forced LIGHTING=1 / AMBIENT=0x00FFFFFF in SetRenderState, material ambient floor in SetMaterial, TFACTOR+ADD texture-stage rewrite in SetTextureStageState, Clear() colour override and the DrawPrimitiveUP ambient nudge (IDirect3DDevice8.cpp + its survivor_pc.cpp counterparts, both back to their 6d3d290b content). Debug menu now shows the physical room (`CUR 2D 03/0D`, CurrentRoomId in features.h) so scene ids can be matched to STAGEss/rrrr. README V1.1 "Display & Graphics Compatibility" gained the two matching bullets. Dead ends recorded so they are not re-explored: map items (0x0045D670) draw nothing here (manager 0x0B53F380, +0x1C array = 0, +0x20 count = 0; +0x10 is a model container pointer, not a count), and 0x0040F180 is a one-shot "draw unlit then free" container that is empty for this room. Objects 1/2 are force-skipped by the stage-3 renderer 0x0049B7E6 and redrawn at 0x0049BD2F/0x0049BD3A with flags 0 (doors), unaffected by the fix. Temporary diagnostics (roomdraw / roomobj / mapitems) were removed before committing — verify with `grep -a -c roomdraw data/d3d8.dll` = 0. Build: MSBuild Release Win32 Rebuild exit 0, data/d3d8.dll 1,347,584 bytes; clean build NOT yet deployed (E:\Games\Biohazard Gun Survivor\d3d8.dll is still the 03:38 diagnostic build, md5 8931b023… vs repo e3e8b2…/05:15) — deploy needs explicit OK per lab rule.
- 2026-11-11 slice 2 (remove the game's original debug.txt printing): root cause found, not a missing hook but the wrong hook. gun_bio.exe opens debug.txt through its statically linked CRT (fopen/lib `_sopen` -> imported CreateFileA at IAT 0x5AD144, call site 0x0059D978) during startup, from the logger singleton built at 0x005932F0 (object at 0x5EB160 `debug.txt`, ctor 0x004017F0, sole caller 0x00593227, grown with `operator new` 0x59D2E9). Two hooks were involved and neither redirected: InstallCDBypass() hooks kernel32!CreateFileA in DllMain (runs first; it only did CD/volume work and NotifyFileOpenA) while DirectShowHook::Install() added its own CreateFileA/W + _lopen/_lcreat redirect to debug.log from Direct3DCreate8 — i.e. long after the game had already opened the file, and stacked on the same kernel32 function. Fix: the redirect now lives in cd_bypass.cpp Hook_CreateFileA (IsGameLogPathA compares the basename to debug.txt case-insensitively, GameLogRedirectPathA returns debug.log next to d3d8.dll), placed before NotifyFileOpenA so mods/file-events never see the trace opens; the six now-redundant late file hooks were deleted from DirectShowHook::Install, which is back to CoCreateInstance only. Build: MSBuild Release Win32 Rebuild exit 0, data/d3d8.dll 1,342,976 bytes (05:28) — smaller than 1,347,584 because the duplicate hooks are gone. deletes/leftover: existing E:\Games\Biohazard Gun Survivor\debug.txt (12,365 bytes, 2026-11-11 05:21:26, last line `menu.cpp: exit -- clsMenu::Run`) is a stale file from a run of the old build and can be deleted. Oracle still open: launch once with this build and confirm debug.txt is not recreated while debug.log still receives the wrapper's lines.
