// Current room and its PS1 walls and doors, read from the PC's Resource\ROOMENV.DAT (the PC game
// does not use that file). The walls serve the PS1 player walls and the zombie chase.
//
// 00475530 builds "PSX_DATA\stage\STAGEss\rrrr\rrrr_A/B.HMD" (Ghidra 2026-10-15: __cdecl, four
// arguments, plain RET; used by 004740C0 and 18 other call sites of the room loader).
#include <windows.h>
#include <d3d8.h>

#include <algorithm>
#include <cstdio>
#include <vector>

#include "../config.h"
#include "../survivor_pc.h"
#include "../game.h"
#include "../hooks.h"
#include "../log.h"
#include "../ps1/nav_logic.h"
#include "../roomenv_index.h"
#include "features.h"

namespace sf::features {

namespace {

using RoomPathFn = void(__cdecl*)(char* out, int stage, int room, int variant);
RoomPathFn g_roomPath = nullptr;

int g_stage = -1;
int g_room = -1;
std::vector<ps1::Wall> g_enemyWalls;
bool g_geometryReady = false;
std::vector<ps1::Wall> g_walls;       // movement walls of the current room
std::vector<ps1::VoiceProfile> g_voiceProfiles;
std::vector<ps1::Placement> g_placements;  // PS1-original spawns (ROOMENV.DAT)
std::vector<ps1::DoorEvent> g_doors;  // direct and optional scripted approach ranges (section 4)

// 0045E030 draws one room object. Bit 2 of its +108 flags makes it return without calling the
// model draw (00412030), and room 03/0D sets that bit on its four walls while the light switch
// byte 005F44BC is zero. The unlit room therefore draws no walls at all: the frame is only the
// black clear, the HUD and the switch glow. Drawing those walls with the room lights off and a
// dim ambient gives the PS1's dark-but-visible room instead.
constexpr uintptr_t kRoomObjectDraw = 0x0045E030;
inline constexpr uint8_t kRoomObjectDrawBytes[] = {0x83, 0xEC, 0x40, 0x56, 0x8B, 0xF1,
                                                   0xF6, 0x86, 0x08, 0x01, 0x00, 0x00, 0x04};
constexpr uintptr_t kRoomLightFlag = 0x005F44BC;   // 0 = lights off, 2 = the switch turned them on
constexpr uintptr_t kGraphicsObject = 0x005FD0C8;  // +8 is the game's IDirect3DDevice8
constexpr int kUnlitRoomScene = 0x2D;
constexpr DWORD kUnlitRoomAmbient = 0xFF323232;
constexpr size_t kObjectFlags = 0x108;

using RoomObjectDrawFn = int(__thiscall*)(void* self);
RoomObjectDrawFn g_roomObjectDraw = nullptr;

IDirect3DDevice8* RoomDevice() {
  auto* graphics = *reinterpret_cast<char**>(kGraphicsObject);
  return graphics ? *reinterpret_cast<IDirect3DDevice8**>(graphics + 8) : nullptr;
}

int __fastcall RoomObjectDrawDetour(void* self, void* /*edx*/) {
  auto* flags = reinterpret_cast<uint32_t*>(static_cast<char*>(self) + kObjectFlags);
  const bool unlitRoom = SurvivorPc_IsSurvivorExe() &&
                         *reinterpret_cast<int*>(game::kSceneId) == kUnlitRoomScene &&
                         *reinterpret_cast<uint8_t*>(kRoomLightFlag) == 0 && (*flags & 4u) != 0;
  IDirect3DDevice8* device = unlitRoom ? RoomDevice() : nullptr;
  if (!device) return g_roomObjectDraw(self);

  DWORD oldAmbient = 0, oldLighting = 0;
  BOOL lightWas[8] = {};
  device->GetRenderState(D3DRS_AMBIENT, &oldAmbient);
  device->GetRenderState(D3DRS_LIGHTING, &oldLighting);
  for (DWORD i = 0; i < 8; ++i) {
    lightWas[i] = FALSE;
    device->GetLightEnable(i, &lightWas[i]);
    device->LightEnable(i, FALSE);
  }
  device->SetRenderState(D3DRS_AMBIENT, kUnlitRoomAmbient);
  device->SetRenderState(D3DRS_LIGHTING, TRUE);
  // The native branch only draws when bit 2 is clear: clear it, draw, then restore it.
  *flags &= ~4u;
  const int result = g_roomObjectDraw(self);
  *flags |= 4u;
  for (DWORD i = 0; i < 8; ++i) device->LightEnable(i, lightWas[i]);
  device->SetRenderState(D3DRS_AMBIENT, oldAmbient);
  device->SetRenderState(D3DRS_LIGHTING, oldLighting);
  return result;
}

// Movement walls (800B60CC on the PS1) from the room's record in the PC ROOMENV.DAT:
// section at offset +4 of the header: n1 (byte), then at +8 n1 walls of 16 bytes.
void LoadWalls(int stage, int room) {
  g_walls.clear();
  g_enemyWalls.clear();
  g_doors.clear();
  g_voiceProfiles.clear();
  g_placements.clear();
  g_geometryReady = false;
  uint32_t offset = 0;
  bool found = false;
  for (const auto& r : game::roomenv::kRooms) {
    if (r.stage == stage && r.room == room) {
      offset = r.offset;
      found = true;
      break;
    }
  }
  if (!found) {
    Log("walls: room %02X/%02X has no walls in ROOMENV", stage, room);
    return;
  }
  FILE* f = _wfopen((g_dir + L"Resource\\ROOMENV.DAT").c_str(), L"rb");
  if (!f) {
    Log("walls: Resource\\ROOMENV.DAT not found");
    return;
  }
  uint32_t header[7] = {};
  uint8_t counts[8] = {};
  bool ok = std::fseek(f, static_cast<long>(offset), SEEK_SET) == 0 &&
            std::fread(header, sizeof(header), 1, f) == 1 && header[0] == 6 &&
            header[1] == 0x1C &&
            std::fseek(f, static_cast<long>(offset + header[1]), SEEK_SET) == 0 &&
            std::fread(counts, sizeof(counts), 1, f) == 1;
  if (ok) {
    g_walls.resize(counts[0]);
    ok = g_walls.empty() ||
         std::fread(g_walls.data(), sizeof(ps1::Wall), g_walls.size(), f) == g_walls.size();
  }
  if (ok && header[3]>header[2] && header[3]-header[2]<=65536) {
    std::vector<uint8_t> encounters(header[3]-header[2]);
    if (std::fseek(f,static_cast<long>(offset+header[2]),SEEK_SET)==0 &&
        std::fread(encounters.data(),1,encounters.size(),f)==encounters.size()) {
      g_placements = ps1::ParsePlacements(encounters.data(), encounters.size());
      if (g_config.ps1ZombieVoiceBanks)
        g_voiceProfiles = ps1::ParseVoiceProfiles(encounters.data(),encounters.size());
    }
  }
  if (ok && header[5] > header[4] && header[5] - header[4] <= 0x1000) {
    // Section 4: original ranges of 80079A28; direct type 4 and scripted type 3.
    std::vector<uint8_t> events(header[5] - header[4]);
    if (std::fseek(f, static_cast<long>(offset + header[4]), SEEK_SET) == 0 &&
        std::fread(events.data(), 1, events.size(), f) == events.size()) {
      g_doors = ps1::ParseDoorEvents(events.data(), events.size());
      if (!g_config.ps1ScriptedAccess) {
        g_doors.erase(std::remove_if(g_doors.begin(), g_doors.end(),
                                    [](const ps1::DoorEvent& event) { return event.scripted; }),
                      g_doors.end());
      }
    }
  }
  if (ok) {
    // 8008DAD0: N4 follows N1*16 + (N2+N3)*20 and has 20-byte records.
    const long extra = static_cast<long>(offset + header[1] + 8 + counts[0] * 16 + (counts[2] + counts[4]) * 20);
    ok = std::fseek(f, extra, SEEK_SET) == 0;
    for (unsigned i = 0; ok && i < counts[6]; ++i) {
      int32_t record[5];
      ok = std::fread(record, sizeof record, 1, f) == 1;
      if (ok) g_enemyWalls.push_back({record[0], record[1], record[2], record[3]});
    }
  }
  std::fclose(f);
  if (!ok) {
    g_walls.clear();
    g_enemyWalls.clear();
    Log("walls: invalid record for room %02X/%02X at 0x%X", stage, room, offset);
    return;
  }
  g_geometryReady = true;
  const auto scripted = std::count_if(g_doors.begin(), g_doors.end(),
                                     [](const ps1::DoorEvent& event) { return event.scripted; });
  Log("walls: room %02X/%02X, %u walls, %u enemy-only walls, %u doors, %u placements",
      stage, room,
      static_cast<unsigned>(g_walls.size()), static_cast<unsigned>(g_enemyWalls.size()),
      static_cast<unsigned>(g_doors.size() - scripted),
      static_cast<unsigned>(g_placements.size()));
  if (g_config.ps1ScriptedAccess)
    Log("access: room %02X/%02X, %u scripted approach ranges; native interaction checks retained",
        stage, room, static_cast<unsigned>(scripted));
}

void __cdecl RoomPathDetour(char* out, int stage, int room, int variant) {
  g_roomPath(out, stage, room, variant);
  stage &= 0xFF;
  room &= 0xFF;
  // Scene list diagnostic: the HMD path identifies the physical room, so one
  // line per scene/room pair turns a pass through the F10 jump menu into the
  // exact scene -> STAGEss/rrrr table. The bytes at 0x5C12AC are the path
  // template the native function patches, so `out` is already the final path.
  static int lastScene = -1, lastStage = -1, lastRoom = -1;
  const int scene = *reinterpret_cast<int*>(game::kSceneId);
  if (scene != lastScene || stage != lastStage || room != lastRoom) {
    char path[64] = {};
    for (size_t i = 0; out && i + 1 < sizeof(path) && out[i]; ++i) path[i] = out[i];
    Log("room: scene %02X -> stage %02X room %02X variant %d, %s", scene, stage, room, variant, path);
    lastScene = scene;
    lastStage = stage;
    lastRoom = room;
  }
  if (stage == g_stage && room == g_room) return;
  g_stage = stage;
  g_room = room;
  zombie::ResetZombieRoom();
  LoadWalls(stage, room);
}

}  // namespace

const std::vector<ps1::Wall>& CurrentRoomWalls() { return g_walls; }
const std::vector<ps1::DoorEvent>& CurrentRoomDoors() { return g_doors; }
const std::vector<ps1::Wall>& CurrentEnemyOnlyWalls() { return g_enemyWalls; }
bool CurrentRoomGeometryReady() { return g_geometryReady; }
const std::vector<ps1::VoiceProfile>& CurrentRoomVoiceProfiles() { return g_voiceProfiles; }
const std::vector<ps1::Placement>& CurrentRoomPlacements() { return g_placements; }
bool IsCurrentRoom(int stage, int room) { return g_stage == stage && g_room == room; }
bool CurrentRoomId(int* stage, int* room) {
  if (g_stage < 0 || g_room < 0) return false;
  if (stage) *stage = g_stage;
  if (room) *room = g_room;
  return true;
}

bool InstallRoomWalls() {
  const game::ByteSignature check = {game::kRoomPath, game::kRoomPathPrologue, sizeof(game::kRoomPathPrologue)};
  if (!game::CheckBytes(check)) {
    Log("RoomWalls: unexpected bytes at %08X, different executable? Feature off.",
        static_cast<unsigned>(game::kRoomPath));
    return false;
  }
  const bool roomPath = Hook("RoomPath", game::kRoomPath, reinterpret_cast<void*>(&RoomPathDetour),
                             reinterpret_cast<void**>(&g_roomPath));
  if (!roomPath) return false;
  if (game::CheckBytes({kRoomObjectDraw, kRoomObjectDrawBytes, sizeof kRoomObjectDrawBytes})) {
    if (!Hook("RoomObjectDraw", kRoomObjectDraw, reinterpret_cast<void*>(&RoomObjectDrawDetour),
              reinterpret_cast<void**>(&g_roomObjectDraw)))
      Log("RoomWalls: room object draw hook failed; unlit room 2D stays black.");
  } else {
    Log("RoomWalls: unexpected bytes at %08X, unlit room 2D stays black.",
        static_cast<unsigned>(kRoomObjectDraw));
  }
  return roomPath;
}

}  // namespace sf::features
