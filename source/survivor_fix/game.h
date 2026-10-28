// Addresses and layouts of gun_bio.exe (Resident Evil Survivor PC, base 0x00400000).
//
// Rule: only what is verified in the assembly goes here. Before hooking a function,
// game::CheckBytes() compares the expected bytes in memory: if the executable is different
// (another build, a patch) the fix stays off instead of crashing.
#pragma once

#include <cstddef>
#include <cstdint>

namespace game {

struct ByteSignature {
  uintptr_t address;
  const uint8_t* bytes;
  size_t size;
};

bool CheckBytes(const ByteSignature& sig);

// Movie init 00580B80: ECX=movie, one path argument, RET4; completion 005818A0: no arguments, AL, RET.
// The original loops call completion, draw while false, then 00580A30 stops and releases the graph.
constexpr uintptr_t kMovieInit = 0x00580B80;
inline constexpr uint8_t kMovieInitBytes[] = {0x83, 0xEC, 0x40, 0x55, 0x56, 0x57, 0x68, 0xA4, 0, 0, 0};
constexpr uintptr_t kMovieDone = 0x005818A0;
inline constexpr uint8_t kMovieDoneBytes[] = {0xA1, 0x58, 0x4F, 0x5F, 0, 0x83, 0xEC, 0x10, 0x8D, 0x54, 0x24, 0};

// 00406C70 __thiscall(input,index), RET4: mouse type2 reads at most16 events per call.
constexpr uintptr_t kReadInputDevice = 0x00406C70;
inline constexpr uint8_t kReadInputDeviceBytes[] = {0x81,0xEC,0x54,0x02,0,0,0x53,0x55};
constexpr size_t kInputDeviceTypes = 0xF0;
constexpr size_t kInputDeviceCountOffset = 0x1A4;
constexpr unsigned char kInputDeviceMouse = 2;

// Native gameplay wait (46A5DD) and shadow-only push (43C924), read-only assembly audit of 2026-10-21.
constexpr uintptr_t kPcCadenceWait = 0x0046A5DD;
inline constexpr uint8_t kPcCadenceWaitBytes[] = {0xFF, 0xD7, 0x2B, 0x05, 0x60, 0xE0, 0x5B, 0x00, 0x83, 0xF8, 0x22, 0x73, 0x0D};
constexpr uintptr_t kPcCadenceAfterWait = 0x0046A5F7;
inline constexpr uint8_t kPcCadenceAfterBytes[] = {0xFF, 0xD7, 0x8B, 0x54, 0x24, 0x60, 0x8B, 0x0D, 0xDC,
                                                   0xD0, 0x5F, 0x00, 0xA3, 0x60, 0xE0, 0x5B, 0x00};
constexpr uintptr_t kZombieShadowBranch = 0x0043C903;
inline constexpr uint8_t kZombieShadowBranchBytes[] = {0x8B, 0x48, 0x5C, 0x85, 0xC9, 0x75, 0x47, 0x8B, 0x58, 0x0C, 0x8B, 0x40, 0x10};
constexpr uintptr_t kZombieShadowPush = 0x0043C924;
inline constexpr uint8_t kZombieShadowPushBytes[] = {0x50, 0x68, 0x00, 0x01, 0x00, 0x00};
constexpr uintptr_t kZombiePoolPush = 0x0043CB06;
inline constexpr uint8_t kZombiePoolPushBytes[] = {0x51, 0x68, 0x00, 0x01, 0x00, 0x00};

// ---------------------------------------------------------------------------
// FUN_00436B40: animation request and application.
// ABI: ECX = S, two stack arguments (request, flag), RET 8 -> __thiscall.
// Expected bytes:
//   00436B61: 89 46 1C        mov [esi+1C], eax   (writes S+1C)
//   00436B64: E8 D7 02 00 00  call 00436E40       (mapper)
constexpr uintptr_t kRequestAnim = 0x00436B40;
inline constexpr uint8_t kRequestAnimStore[] = {0x89, 0x46, 0x1C};
inline constexpr uint8_t kRequestAnimCall[] = {0xE8, 0xD7, 0x02, 0x00, 0x00};
// Native S initialisation (4361B0): CALL 436B04 -> 436B40 in the original executable. The notification
// comes before the mapper; no guessed SEH initializer ABI and no elapsed-time proxy for the object lifetime.
constexpr uintptr_t kZombieInitAnimCall = 0x00436B04;
constexpr uintptr_t kZombieInitAnimReturn = kZombieInitAnimCall + 5;
inline constexpr uint8_t kZombieInitAnimCallBytes[] = {0xE8, 0x37, 0x00, 0x00, 0x00};

// ---------------------------------------------------------------------------
// FUN_00436E40: request -> selector mapper.
// Ghidra 2026-10-15: ECX = S (MOV ESI,ECX), one stack argument (request), all 211 RETs are RET 4 ->
// __thiscall. It returns the selector, which 00436B40 applies (S+28, then 00412B90 for every track) only
// if different from the current one or if S+24 != 0. For type 0 with requests 2/3/4 it "keeps" the
// selector until the sequence is complete.
// Entry of 0043A8A0 (0043A8CD-0043A930, 2026-10-21): the zombie enemy logic runs only with mode [[5FD0CC]+41] == 3,
// or 4 if scene [5F3DAC] (FUN_0046D500: mov eax,[5F3DAC]; ret) is neither 4 nor 0x5B, or 6 if the scene is
// 0x5B; and with S+68 != 0. Otherwise RET -1.
constexpr uintptr_t kGameState = 0x005FD0CC;  // ptr; +0x41 byte mode (3 = in game)
constexpr size_t kGameModeOffset = 0x41;
constexpr uintptr_t kSceneIdGetter = 0x0046D500;
inline constexpr uint8_t kSceneIdGetterBytes[] = {0xA1, 0xAC, 0x3D, 0x5F, 0x00, 0xC3};
constexpr uintptr_t kSceneId = 0x005F3DAC;
// Native room enemy construction: 00430F90 has three stack arguments, ignores incoming ECX,
// and returns at004346B4 with RET0C. Read-only C/ASM export pc-enemy-room-loader, 2026-10-23.
constexpr uintptr_t kBuildRoomEnemies=0x00430F90;
inline constexpr uint8_t kBuildRoomEnemiesBytes[]={0x51,0x8B,0x0D,0xC4,0xD0,0x5F,0x00,0x53,0x6A,0x37};
constexpr uintptr_t kBuildRoomEnemiesRet=0x004346B4;
inline constexpr uint8_t kBuildRoomEnemiesRetBytes[]={0xC2,0x0C,0x00};
constexpr uintptr_t kSkipRoomEnemyBuild=0x005F2C14;
constexpr uintptr_t kBuildRoomEnemyGate=0x00430FB2;
inline constexpr uint8_t kBuildRoomEnemyGateBytes[]={0x38,0x1D,0x14,0x2C,0x5F,0x00};
constexpr uintptr_t kUpdateGateCall = 0x0043A8D3;  // call 0046D500
inline constexpr uint8_t kUpdateGateCallBytes[] = {0xE8, 0x28, 0x2C, 0x03, 0x00, 0x83, 0xF8, 0x04};
constexpr size_t kEnemyActive = 0x68;  // S+68 byte

// Native mesh sweep: ECX=O, stack (origin*, radius, displacement*, face*, response*), RET 14.
// Original 004612B7/004616F4 calls and 0045F090 assembly verified; no new hook is installed here.
constexpr uintptr_t kNativeMeshSweep = 0x0045F090;
inline constexpr uint8_t kNativeMeshSweepBytes[] = {0x81,0xEC,0x40,0x02,0x00,0x00,0x53,0x55,0x8B,0xE9};

constexpr uintptr_t kMapAnim = 0x00436E40;
// FUN_00412B90: applies the sequence to a track. __thiscall(M, track, selector, transition, speed),
// RET 10 (00412C78, 00412C84). The only call of 00436B40 (00436C0F: push eax(S+30); push ebx(transition);
// push edi(selector); push ebp(track); call) passes transition 10 (2 for type 0x1E).
constexpr uintptr_t kApplySequence = 0x00412B90;
inline constexpr uint8_t kApplySequencePrologue[] = {0x53, 0x55, 0x8B, 0x6C, 0x24, 0x0C, 0x56, 0x57, 0x8B, 0xF9};
constexpr uintptr_t kApplySequenceCall = 0x00436C0F;
inline constexpr uint8_t kApplySequenceCallBytes[] = {0x50, 0x53, 0x57, 0x55, 0xE8, 0x78, 0xBF, 0xFD, 0xFF};
inline constexpr uint8_t kMapAnimPrologue[] = {0x83, 0xEC, 0x40, 0x53, 0x55, 0x56, 0x8B, 0xF1};

// PC 0047B020: exit 0C of physical room 07/00, cdecl(C*), original executable verified.
// Geometry data only; no native event function is called from here.
constexpr uintptr_t kNative0700DoorCheck = 0x0047B020;
inline constexpr uint8_t kNative0700DoorCheckBytes[] = {0x83, 0xEC, 0x18, 0x56, 0x8B, 0x74, 0x24, 0x20};
constexpr uintptr_t kNative0700DoorMinX = 0x005AD65C;
constexpr uintptr_t kNative0700DoorMaxX = 0x005AD658;
constexpr uintptr_t kNative0700DoorZ = 0x005AD654;
inline constexpr uint8_t kNative0700DoorMinXBytes[] = {0x00, 0x10, 0xE4, 0x45};
inline constexpr uint8_t kNative0700DoorMaxXBytes[] = {0x00, 0xB8, 0x01, 0x46};
inline constexpr uint8_t kNative0700DoorZBytes[] = {0x00, 0x70, 0xB6, 0xC5};
// PC 004941F0: exit 0D Storage, physical room 08/01, verified original assembly and data.
// PS1 access observed 08/01 -> 08/02 at the ROOMENV wall; no key bypass or event call.
constexpr uintptr_t kNative0801DoorCheck = 0x004941F0;
inline constexpr uint8_t kNative0801DoorCheckBytes[] = {0x83, 0xEC, 0x18, 0x56, 0x8B, 0x74, 0x24, 0x20};
constexpr uintptr_t kNative0801DoorMinX = 0x005AD648;
inline constexpr uint8_t kNative0801DoorMinXBytes[] = {0x9A, 0x81, 0x51, 0xC5};
constexpr uintptr_t kNative0801DoorMaxX = 0x005AD8E8;
inline constexpr uint8_t kNative0801DoorMaxXBytes[] = {0x00, 0xA0, 0x50, 0xC5};
constexpr uintptr_t kNative0801StorageMinZ = 0x005AD8E4;
inline constexpr uint8_t kNative0801StorageMinZBytes[] = {0x3D, 0xB2, 0x9C, 0xC5};
constexpr uintptr_t kNative0801StorageMaxZ = 0x005AD8E0;
inline constexpr uint8_t kNative0801StorageMaxZBytes[] = {0x29, 0xFC, 0x35, 0xC5};

// FUN_00475530: builds the path of the room HMD PSX_DATA/stage/STAGEss/rrrr/rrrr_A.HMD (or _B) from
// (stage, room, variant). Ghidra 2026-10-15: __cdecl (out, stage, room, variant), plain RET.
constexpr uintptr_t kRoomPath = 0x00475530;
inline constexpr uint8_t kRoomPathPrologue[] = {0x83, 0xEC, 0x14, 0xA1, 0x28, 0xF6, 0x5B, 0x00};

// FUN_0045FD70: accumulation of O's local movement.
// Ghidra 2026-10-15: ECX = O, three floats on the stack (x, y, z), RET 0xC;
// accum(O+D4/D8/DC) += xyz * (1.0 / O+120), clears O+104/O+105.
constexpr uintptr_t kAccumulate = 0x0045FD70;
// FUN_0043BC70: the enemy's hit on the player (sound, T+88 = 1, damage by type and selector).
// __thiscall(S), no arguments, plain RET at 0043C1DF; the only call is 0043AB6B MOV ECX,ESI / CALL,
// after distance, side and a wait of 0xC00 ms.
constexpr uintptr_t kHitPlayer = 0x0043BC70;
// FUN_0045FE30: view rotation towards a point. __thiscall(object 0, float x, float y, float z), RET 0xC
// (004600B8). If O+A1 is already 1 it does nothing; otherwise it sets O+A1 = 1 and the angles in degrees in
// O+A4/O+A8 (arc cosine, signed). Every tick the physics (0046051A) applies 20% of them (O+A4 * 0.2, then
// O+A4 = step * 4) to the view direction 005F2C4C and to the object (0045E110). 0043BC70 calls it with
// the zombie's (x, y - [005AD46C] = 1590, z): it is the view snap on the bite.
constexpr uintptr_t kTurnToward = 0x0045FE30;
inline constexpr uint8_t kTurnTowardPrologue[] = {0x83, 0xEC, 0x3C, 0x56, 0x8B, 0xF1, 0x80, 0x3E, 0x00};
constexpr float kAttackHeadOffset = 1590.0f;  // 005AD46C
constexpr float kTurnApplyFraction = 0.2f;    // 005AD628
inline constexpr uint8_t kHitPlayerPrologue[] = {0x83, 0xEC, 0x1C, 0x53, 0x56, 0x8B, 0xF1, 0x57};
inline constexpr uint8_t kAccumulatePrologue[] = {0x83, 0xEC, 0x0C, 0xD9, 0x81, 0x20, 0x01, 0x00, 0x00};

// 0043A8A0, attack event: after the dot product between the player->monster direction and the player's
// front (0043AB5E FCOMP 0.0), 0043AB69 JNZ +7 skips CALL 0043BC70 when the monster is behind.
constexpr uintptr_t kBiteFacingCheck = 0x0043AB5E;
inline constexpr uint8_t kBiteFacingCheckBytes[] = {0xD8, 0x1D, 0xA0, 0xD2, 0x5A, 0x00, 0xDF, 0xE0, 0xF6, 0xC4,
                                                    0x01, 0x75, 0x07, 0x8B, 0xCE, 0xE8, 0xFE, 0x10, 0x00, 0x00};
constexpr uintptr_t kBiteFacingJump = 0x0043AB69;

// 00406500, setup of the DirectInput devices (EnumDevices with type 0: all of them). 00406730 sets the
// type only for mouse, keyboard and joystick (dwDevType & 0xFF = 2/3/4); for the others (e.g. "vendor
// defined" HID) it leaves without writing it. At 00406590 the number of objects is set for types 1/2/3
// (0x100, 4, 0x20); for the others the JNZ at 0040659F skips the assignment and the loop at 004065E9 uses
// uninitialised memory: endless 0x50-byte allocations (game hung in ntdll, stack in 0059CCBC and
// GetObjectInfo) or count*8 overflowing and a crash at 0040664A.
constexpr uintptr_t kInputDeviceCount = 0x00406590;
inline constexpr uint8_t kInputDeviceCountBytes[] = {
    0x0F, 0xBE, 0x8C, 0x18, 0xF0, 0x00, 0x00, 0x00,        // movsx ecx, byte [eax+ebx+F0]
    0x49, 0x74, 0x18, 0x49, 0x74, 0x0C, 0x49, 0x75, 0x1E,  // type 1 / 2 / others
    0xC7, 0x04, 0x07, 0x20, 0x00, 0x00, 0x00, 0xEB, 0x10,  // type 3: 0x20
    0xC7, 0x04, 0x07, 0x04, 0x00, 0x00, 0x00, 0xEB, 0x07,  // type 2: 4
    0xC7, 0x04, 0x07, 0x00, 0x01, 0x00, 0x00};             // type 1: 0x100 -> 004065BA
// The same values for 1/2/3, 0 for the others (the loop skips them), then 004065BA. EDX is free: the
// CALL 0059CCBC that follows overwrites it.
inline constexpr uint8_t kInputDeviceCountFix[] = {
    0x0F, 0xBE, 0x8C, 0x18, 0xF0, 0x00, 0x00, 0x00,  // 590 movsx ecx, byte [eax+ebx+F0]
    0x31, 0xD2,                                      // 598 xor edx, edx
    0x49, 0x74, 0x0A,                                // 59A dec ecx; je 5A7
    0x49, 0x74, 0x0B,                                // 59D dec ecx; je 5AB
    0x49, 0x75, 0x0C,                                // 5A0 dec ecx; jne 5AF
    0xB2, 0x20, 0xEB, 0x08,                          // 5A3 mov dl, 20h; jmp 5AF
    0xB6, 0x01, 0xEB, 0x04,                          // 5A7 mov dh, 1 (100h); jmp 5AF
    0xB2, 0x04, 0xEB, 0x00,                          // 5AB mov dl, 4; jmp 5AF
    0x89, 0x14, 0x07,                                // 5AF mov [edi+eax], edx
    0xEB, 0x06,                                      // 5B2 jmp 5BA
    0x90, 0x90, 0x90, 0x90, 0x90, 0x90};             // 5B4
static_assert(sizeof(kInputDeviceCountFix) == sizeof(kInputDeviceCountBytes), "same length");

// FUN_0043A8A0: enemy logic update of one S.
// Ghidra 2026-10-15: ECX = S, no arguments, all 35 RETs are C3; returns in EAX the request passed to
// 00436B40 or -1. Verified CALL 0043C86F from the normal actor loop 0043C850. The resume/suspend wrappers
// 0043D2F0/0043D340 also call UpdateEnemy, but they are not gameplay ticks.
constexpr uintptr_t kWorldEnemyUpdateCall = 0x0043C86F;
constexpr uintptr_t kWorldEnemyUpdateReturn = 0x0043C874;
inline constexpr uint8_t kWorldEnemyUpdateCallBytes[] = {0xE8, 0x2C, 0xE0, 0xFF, 0xFF};
constexpr uintptr_t kUpdateEnemy = 0x0043A8A0;
inline constexpr uint8_t kUpdateEnemyPrologue[] = {0x83, 0xEC, 0x44, 0x53, 0x55, 0x56, 0x8B, 0xF1};

// FUN_004609D0: model/physics phase of a collection C, every frame in every room.
// __thiscall(C), no arguments, plain RET at 00460FE9.
constexpr uintptr_t kPhysics = 0x004609D0;
inline constexpr uint8_t kPhysicsPrologue[] = {0x81, 0xEC, 0x00, 0x01, 0x00, 0x00, 0x53, 0x55, 0x8B, 0xE9};

// Model drawing with the bones (2026-10-22): __thiscall(M, a, b), RET 8 (00412A60); reads M+78/M+7C;
// called for the enemies by 0043CF05 and 0043D109.
constexpr uintptr_t kDrawModel = 0x00412030;
inline constexpr uint8_t kDrawModelBytes[] = {0x81, 0xEC, 0x48, 0x01, 0x00, 0x00, 0x53, 0x55, 0x56, 0x8B, 0xF1, 0x57};
// Frame-based effects (2026-10-22): 0040EFD0 creates, __thiscall(manager, 15 arguments), RET 0x3C, returns
// the index in [manager+8] (at most 10); 0040E830 updates and draws, __thiscall(effect), 0 at the end of the
// sequence, RET 8 (two pointers: the axes of the square). Fields: +DC/+E0 sides, +F0 sum of +E8 at every
// draw, cell F0 / +EC - 1. The blood on a hit is call 0043AC86 in 0043A8A0 (blood1/blood2.BMP at point O+90).
// Killed enemies (2026-10-22): 00464FA0 __thiscall([5FD0DC]), RET, increments +3B8/+3BC (or the counters of
// area +8C); the PC death calls it at the end of the clip with S+4C, then S+5C = 1 (0043A9BE).
constexpr uintptr_t kCountKill = 0x00464FA0;
inline constexpr uint8_t kCountKillBytes[] = {0x8B, 0x81, 0x8C, 0x00, 0x00, 0x00, 0x83, 0xF8, 0xFF};
constexpr uintptr_t kCountKillCall = 0x0043A9BE;
inline constexpr uint8_t kCountKillCallBytes[] = {0xE8, 0xDD, 0xA5, 0x02, 0x00};
constexpr uintptr_t kGameStats = 0x005FD0DC;
constexpr uintptr_t kEffectCreate = 0x0040EFD0;
inline constexpr uint8_t kEffectCreateBytes[] = {0x53, 0x56, 0x8B, 0xF1, 0x57, 0x33, 0xDB, 0x8B, 0x7E, 0x0C};
constexpr uintptr_t kEffectDraw = 0x0040E830;
inline constexpr uint8_t kEffectDrawBytes[] = {0x83, 0xEC, 0x44, 0x55, 0x56, 0x8B, 0xF1, 0x57};
constexpr uintptr_t kBloodCall = 0x0043AC86;
inline constexpr uint8_t kBloodCallBytes[] = {0xE8, 0x45, 0x43, 0xFD, 0xFF};
constexpr uintptr_t kBloodCallReturn = 0x0043AC8B;
constexpr size_t kEffectList = 0x08;
constexpr size_t kEffectSideA = 0xDC;
constexpr size_t kEffectSideB = 0xE0;
constexpr size_t kEffectFrame = 0xF0;
constexpr size_t kEffectFrameDiv = 0xEC;  // cell = F0 / +EC - 1 (0040E9D9)

// Frames, states and doors for the PS1 screen effects (screen_fx.cpp; Ghidra and capstone 2026-10-23).
// 004694B0, main loop: [5FD0CC]+351C 1 CAPCOM30.AVI, 2 title, 3 new game, 4 TEXT.AVI, 5 OPENING.AVI,
// 6 rooms (0046AFE0), 7 YOU DIED (00469E50 -> 00591900), 8 Continue (00592520); +351D first frame of the state.
// 0046A050 leaves 7 for 8 when the button word of 00466E10 has 0x800.
constexpr size_t kMainState = 0x351C;  // byte in [kGameState]
constexpr uint32_t kPadConfirm = 0x800;
// 00466CD0: __thiscall(game, int), RET 4: EndScene (00466D90) and Present (0040E4F0); ends every frame
// (rooms, doors, menus).
constexpr uintptr_t kEndFrame = 0x00466CD0;
inline constexpr uint8_t kEndFrameBytes[] = {0x56, 0x8B, 0xF1, 0x80, 0xBE, 0x1C, 0x35, 0x00};
// 00466E10: __thiscall(game, int), RET 4: the button word of the frame (00406BB0 on [5FD0BC]).
constexpr uintptr_t kReadPad = 0x00466E10;
inline constexpr uint8_t kReadPadBytes[] = {0x51, 0x8B, 0x0D, 0xBC, 0xD0, 0x5F, 0x00, 0xC7};
// 00407810: __thiscall([5FD0C0], 6 int), RET 0x18: mouse click in the rectangle; 0046A775 uses it to shoot.
constexpr uintptr_t kMouseClick = 0x00407810;
inline constexpr uint8_t kMouseClickBytes[] = {0x8A, 0x44, 0x24, 0x18, 0x8B, 0x54, 0x24, 0x14};
// 0046A520: __thiscall(game, 7 arguments), RET 0x1C: the player's commands of one room frame, after the
// 34 ms wait. With [5FD0DC]+8 (health) at 0 it writes 0 into the sixth argument and returns 0: the room
// (e.g. 00479600) leaves with scene 0 and sets +351C = 7.
// 00461000/0045F090 call 0045FAE0 on the collided mesh, face + three 40-byte records.
// Assembly verified: ECX=mesh; RET 0x10. Observe its existing outputs only.
constexpr uintptr_t kReadCollisionTriangle = 0x0045FAE0;
inline constexpr uint8_t kReadCollisionTriangleBytes[] = {0x81,0xEC,0xD8,0,0,0,0x53,0x55,0x56,0x8B,0xF1};
constexpr uintptr_t kRoomTick = 0x0046A520;
inline constexpr uint8_t kRoomTickBytes[] = {0xA1, 0x60, 0xE0, 0x5B, 0x00, 0x83, 0xEC, 0x40, 0x53};
constexpr size_t kGameStatsHp = 0x08;  // int in [5FD0DC]
// Gameplay room dispatcher. It invokes one room function and returns after that
// room has performed its native cleanup, making its next entry a safe reload boundary.
constexpr uintptr_t kRoomDispatcher = 0x0046AFE0;
inline constexpr uint8_t kRoomDispatcherBytes[] = {0x53, 0x56, 0x8B, 0xF1, 0x33, 0xDB};
constexpr size_t kLiveSaveRecord = 0x3C;
constexpr size_t kLiveSaveRecordSize = 0x4D;
// 0042C3D0: __thiscall(hud), RET: draws the room HUD (crosshair, CONDITION 0042C160, weapons).
constexpr uintptr_t kDrawHud = 0x0042C3D0;
inline constexpr uint8_t kDrawHudBytes[] = {0x56, 0x57, 0x8B, 0xF9, 0x8A, 0x47, 0x28, 0x84};
// 0058D560: __thiscall([5FD0D0], DOORNEW path, kind), RET 8: the door animation, after the room has freed its
// objects (00479B91..00479C6A) and before the next room.
// 00591900: __thiscall([5FD0D0], int), RET 4: YOU DIED (DIED00.BMP), called by 00469F89 in state 7.
// 00592520: __thiscall(object, int, char continues left), RET 8: Continue (END_T00/T01.BMP), state 8.
// Common fields: [0] timeGetTime of the last step, [1] phase, [2] counter, [5] picture loaded.
constexpr uintptr_t kYouDiedScreen = 0x00591900;
inline constexpr uint8_t kYouDiedScreenBytes[] = {0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x6A, 0xFF};
constexpr uintptr_t kContinueScreen = 0x00592520;
inline constexpr uint8_t kContinueScreenBytes[] = {0x83, 0xEC, 0x38, 0x53, 0x55, 0x56, 0x8B, 0xF1};
constexpr uintptr_t kDoorAnim = 0x0058D560;
inline constexpr uint8_t kDoorAnimBytes[] = {0x6A, 0xFF, 0x68, 0x19, 0xBB, 0x5A, 0x00, 0x64};

// FUN_00461000: collision query, __thiscall(C, index, displacement*, response*, direction*), ret 0x10;
// only caller 00460CA2 in the physics.
constexpr uintptr_t kCollisionQuery = 0x00461000;
inline constexpr uint8_t kCollisionQueryPrologue[] = {0x81, 0xEC, 0x04, 0x01, 0x00, 0x00, 0x53, 0x55};
// FUN_004603F0: transformation of an object of the collection, __thiscall(C, index, tx, ty, tz, rx, ry, rz),
// RET 0x1C. The player's command handler calls it to turn with (0, 0, 0, 0, +/-2 or +/-4 degrees when
// running, 0): return to 0046AE8E (bit 0x8000, +) and 0046AECF (bit 0x2000, -) (checked in game on
// 2026-10-16: the player's rotations come from 0046AE8E).
constexpr uintptr_t kTransform = 0x004603F0;
inline constexpr uint8_t kTransformPrologue[] = {0x56, 0x57, 0x8B, 0x7C, 0x24, 0x0C, 0x8B, 0xF1};
constexpr uintptr_t kTurnLeftReturn = 0x0046AE8E;
constexpr uintptr_t kTurnRightReturn = 0x0046AECF;
// FUN_0045E110: __thiscall(O, tx, ty, tz, rx, ry, rz in degrees), RET 0x18 (Ghidra 2026-10-17): on the O+3C
// matrix it left-multiplies T(t), T(p), Rx, Ry, Rz, T(-p) (p = pivot O+14, D3DX 005960E3), i.e. it rotates
// in the local axes; then O+8 = p * M and O+B8 = (O+AC + p) * M (00595C44). It is called by 004603F0,
// 00460450, the physics and the scene code. The opening wake-up (00444560) calls it on the player from
// seven points, once per frame for 120 frames: rx +0.25/-0.25/+0.10/-0.10 and ry up to +-1.
constexpr uintptr_t kRotateObject = 0x0045E110;
inline constexpr uint8_t kRotateObjectPrologue[] = {0x83, 0xEC, 0x4C, 0x8B, 0x44, 0x24, 0x58, 0x8B, 0x54, 0x24, 0x50};
inline constexpr uintptr_t kWakeRotateReturns[] = {0x00456D17, 0x00456D3F, 0x00456D69, 0x00456D95,
                                                   0x00456DC5, 0x00456DF5, 0x00456E25};
// FUN_00467440: camera view, __thiscall(cam, eye*, look-at*, up*); the player's view calls it from
// 00460450 (return 004609B2). The bob in 00460450: PS1 tables on the stack, value x [5AD620] (0.1; running
// [5AD624]) added to the local eye, at 00460715 (running), 00460805 (backwards), 004608FD (forward):
// fmul dword ptr [address].
constexpr uintptr_t kSetView = 0x00467440;
constexpr uintptr_t kPlayerViewReturn = 0x004609B2;
inline constexpr uintptr_t kPcHeadBobScales[] = {0x00460715, 0x00460805, 0x004608FD};
// [005FD0C8]: graphics object (new 0x2A520 in 005932F0), cleared only by 00593050 at 0059308E.
constexpr uintptr_t kGraphicsObject = 0x005FD0C8;
// FUN_0046FEC0: void, no arguments, RET: PeekMessageA(PM_REMOVE) + TranslateMessage + DispatchMessageA in the
// middle of a frame (about 140 room loops, 004694B0, 00444560).
constexpr uintptr_t kMessagePump = 0x0046FEC0;
inline constexpr uint8_t kMessagePumpBytes[] = {0x83, 0xEC, 0x1C, 0x8D, 0x44, 0x24, 0x00, 0x6A, 0x01};
// CONDITION in the HUD: the selected FLC is in EAX and the HUD in ESI at this common lookup.
constexpr uintptr_t kDrawCondition = 0x0042C160;
inline constexpr uint8_t kDrawConditionBytes[] = {0x56, 0x57, 0x8B, 0xF1, 0xFF, 0x15, 0x5C, 0xD2, 0x5A, 0x00};
constexpr uintptr_t kConditionTextureLookup = 0x0042C25E;
inline constexpr uint8_t kConditionTextureBytes[] = {0x8B, 0x0E, 0x8B, 0x50, 0x64, 0x8B, 0x3C, 0x8A, 0x85, 0xFF};
constexpr uintptr_t kConditionAfterTexture = 0x0042C266;  // TEST EDI,EDI; null skips the draw
constexpr uintptr_t kFlcCountGetter = 0x00406100;
inline constexpr uint8_t kFlcCountGetterBytes[] = {0x66, 0x8B, 0x41, 0x2C, 0xC3};
constexpr size_t kFlcFrameCount = 0x2C;  // signed 16, native getter
constexpr size_t kFlcTextures = 0x64;    // texture pointer array
// 00466F40: projection, push of the vertical field of view (3F32B8C3 = 0.698 rad) before
// D3DXMatrixPerspectiveFovLH (00595A15).
constexpr uintptr_t kProjectionFovPush = 0x00466F9A;
// Same function: fild [5FD020] and fidiv [5FD024], the projection's aspect ratio.
constexpr uintptr_t kProjectionAspectWidth = 0x00466F82;
constexpr uintptr_t kProjectionAspectHeight = 0x00466F91;

// ---------------------------------------------------------------------------
// Block S (the enemy logic state of an enemy).
namespace S {
// 0043C600 stores EDI (enemy logic-manager slot) at S+04, before 361B0; the native loop 43C850 scans that manager
// array in ascending slots. Distinct from the render object index S+10.
constexpr size_t kAiSlot = 0x04;
constexpr size_t kCollection = 0x0C;  // C*
constexpr size_t kIndex = 0x10;       // int, index of O in C+0C
constexpr size_t kType = 0x14;        // int, "type" that drives the conversion
constexpr size_t kRequest = 0x1C;     // int, most recent request
constexpr size_t kSelector = 0x28;    // int, current selector (-1 = restart)
// byte, completions needed before 00436B40 accepts another selector.
constexpr size_t kThreshold = 0x2C;
// Proposed by the mapper and copied by 00436B40 when applied: +3C -> +2C (byte, completions before it can
// change), +40 -> +30 (float, speed).
constexpr size_t kNextThreshold = 0x3C;
constexpr size_t kNextSpeed = 0x40;
// int, cleared on application, +1 at every completion of track 0 (initial loop of 0043A8A0).
constexpr size_t kLoops = 0x34;
// byte, 1 when the clip has done the required completions (0043A8A0: S+2C == 0 and S+34 > 0, or
// S+34 >= S+2C). The mapper (00436EFE) holds the attack clip while it is 0.
constexpr size_t kDone = 0x2D;
// int, hit armed by the mapper when choosing an attack clip (00436F09); 0043A8A0 consumes it (S+50 = 0)
// when S+34 != 0 and, if distance, side and wait allow it, calls 0043BC70.
constexpr size_t kHitArmed = 0x50;
}  // namespace S

// Collection C (0043A8A0: O = [[C+0C] + 4*S+10], T = [[C+0C] + 4*C+14]).
namespace C {
constexpr size_t kObjects = 0x0C;  // O**
constexpr size_t kCount = 0x10;    // int, number of objects in C+0C (00461000)
constexpr size_t kTarget = 0x14;   // int, index of the designated T
}  // namespace C

// Object O (same layout for T).
namespace O {
constexpr size_t kPos = 0x08;  // float x, y, z
// Pivot of the rotations of 0045E110 (float x, y, z).
constexpr size_t kPivot = 0x14;
// float: scale of the attack ranges. In 0043A8A0 it is read only for requests 8 (d <= O+38 * 4.5) and 9
// (d <= O+38 * 7); the collision (00461000) uses it as a moving radius, but outside the enemy logic update.
constexpr size_t kAttackScale = 0x38;
// 4x4 matrix at +3C, rows of 16 bytes (3 floats + 1), translation at +6C (= O.pos):
// row 0 = (cos, 0, -sin), row 1 = (0, 1, 0), row 2 = forward (read in game).
constexpr size_t kMatrix = 0x3C;
constexpr size_t kMatrixRowFloats = 4;
constexpr size_t kForward = 0x5C;  // float x, y, z
constexpr size_t kModel = 0x7C;    // M*
// Hit (0043A8A0, decompiled): O+88 byte 1 = just hit (sound and blood, then 2), O+9C float damage of the
// hit, O+11C float health (<= 0: request 0x0F/0x10).
constexpr size_t kHitFlag = 0x88;
constexpr size_t kHitPos = 0x90;  // float x, y, z of the hit (0045FD20)
constexpr size_t kHitDamage = 0x9C;
// View rotation towards the attacker (0045FE30 / 0046051A), only for the player.
constexpr size_t kTurnActive = 0xA1;  // byte
constexpr size_t kTurnYaw = 0xA4;     // float, degrees still to turn
constexpr size_t kTurnPitch = 0xA8;   // float, degrees
constexpr size_t kAimOffsetLocal = 0xAC;  // float x, y, z: O+B8 = (O+AC + O+14) * matrix (0045E110)
// float x, y, z: point aimed at by whoever chases O (0043A8A0 brings it into the zombie's local space with
// 0045E9D0 and decides rotation/advance).
constexpr size_t kAimPoint = 0xB8;
// Player movement (0042C701..0042C77D): 0045FD70 with z = -256/+256 (walking) or -512/+512 (running,
// [5FD0DC]+4 = 1), divided by O+120; then 0045E730: O+C8 = O+C8 * 0.8 + O+D4.
constexpr size_t kVelocity = 0xC8;  // float x, y, z local (damped x0.8 every frame)
constexpr size_t kAccum = 0xD4;     // float x, y, z: movement accumulated by 0045FD70
// Result of the query: O+FC index of the collider hit, O+EC/F0/F4 normal of the mesh face (written only if
// dot(normal, normalize(O+E0)) > -0.5, i.e. not floor).
constexpr size_t kContactNormal = 0xEC;
// Collision (00461000): byte mode +F8 (1 sphere, 2 box, 3 mesh); +108 bit 2 = obstacle excluded from the
// query (0046114D).
constexpr size_t kCollisionMode = 0xF8;
constexpr size_t kContactIndex = 0xFC;
constexpr size_t kCollisionFlags = 0x108;
constexpr size_t kHealth = 0x11C;
constexpr size_t kDivisor = 0x120;  // float, divisor of the accumulation (10)
}  // namespace O

// Model M and tracks Q (records of 0x60 bytes).
namespace M {
constexpr size_t kTracks = 0x2C;      // Q*
constexpr size_t kTrackCount = 0x30;  // unsigned
constexpr size_t kTrackSize = 0x60;
// Bones (00412030): number and array of 0x104-byte records; bone i is at [M+7C] + i*0x104.
// Matrix in the model = (B.40 * B.C0) * (P.40 * P.C0) * ... along the parents (+100), with
// D3DXMatrixMultiply (00595459), row vectors.
constexpr size_t kBoneCount = 0x78;  // unsigned
constexpr size_t kBones = 0x7C;      // record*
constexpr size_t kBoneSize = 0x104;
constexpr size_t kBoneAnim = 0x40;     // 16 floats
constexpr size_t kBoneOffset = 0xC0;   // 16 floats
constexpr size_t kBoneParent = 0x100;  // record*
}  // namespace M
namespace Q {
constexpr size_t kCount = 0x0C;   // int, number of sequences (the selector is taken modulo)
constexpr size_t kIndex = 0x10;   // int, actual index of the sequence
constexpr size_t kCursor = 0x44;  // int, cursor in the command stream
constexpr size_t kPairs = 0x5C;   // pairs (control u32, initial cursor u32)
}  // namespace Q

template <typename T>
inline T Read(const void* base, size_t offset) {
  return *reinterpret_cast<const T*>(static_cast<const uint8_t*>(base) + offset);
}

template <typename T>
inline T* Field(void* base, size_t offset) {
  return reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset);
}

}  // namespace game
