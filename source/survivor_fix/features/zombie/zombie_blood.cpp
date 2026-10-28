// Ps1ZombieBlood: the PC blood on a hit (0043A8A0 -> 0040EFD0 from 0043AC86, BMP blood1/blood2) with the
// timing, growth and point of the PS1 (ps1/blood_logic.h). The PC pictures and blending stay.
//   - Duration: blood1 (cells 40) like sequence A of 80098608 (29 frames), blood2 (cells 32) like B of
//     800987F8 (17 frames). The PC shows cell F0 / +EC - 1 (0040E9D9, F0 += +E8 at every draw) and closes at
//     6: with 0.1 / 0.1 cell 0 stays 2 draws and the others 1, 7 draws in all (0.23 s). Here +E8 = 0 and F0
//     is written at every draw: dense cell while the PS1 side grows, then the sparse ones over the
//     full-size frames (BloodPcCell); the PS1 shows the cloud growing full and breaking up at the end.
//   - Side: grows like the PS1 cells (8 -> 40 texels, x 0xAA / 256, 800936E0) instead of the fixed 600 of
//     the PC, blended from one frame to the next.
//   - Point: 80070628 puts it on the crosshair's ray, in front of the body (5/6 of the zombie's distance).
//     The PC puts it on the hit point O+90 (0045FD20, from the ray of 0042CACE) plus 300 down (005AD464):
//     half of the square ended up inside the zombie. Here it stays on the ray, 300 towards the eye: 5/6 of
//     the distance from the zombie's centre took the blood far from the hit when the ray goes down (chest)
//     or the zombie is on the ground.
// ASSUMPTION: side in the world = scaled side x 20, the same scale as the cell movements in 800936E0
// ((side * 1000 / 50) >> 2 on x / 4); with 26 it is 520, about the zombie's torso as on the PS1.
// ASSUMPTION: the side of the PC square is +DC (0040E830 uses +DC * 0.5 per half) and +E0 the other.
#include <intrin.h>

#include "zombie_shared.h"

#include "../../ps1/blood_logic.h"

namespace sf::features::zombie {

namespace {

constexpr float kSideScale = 20.0f;
constexpr float kPullToEye = 300.0f;  // enough to leave the body (zombie radius ~250)
constexpr int kBloodCellA = 0x28;     // blood1.BMP, cells 40 x 40
constexpr int kPcCells = 6;           // blood1 240 x 40 / 40, blood2 193 x 32 / 32

// __thiscall(manager, 15 arguments), RET 0x3C; returns the effect's index ([manager+8][i]).
using CreateEffectFn = int(__fastcall*)(void* m, void* edx, const char* name, float step, float stepB, float sideA,
                                        float sideB, float shrink, const char* bmp, int cellW, int cellH, int zero,
                                        float x, float y, float z, int blend, int once);
// __thiscall(effect, a, b), RET 8 (0040EF36; from 0040F285 with the two axes of the square): updates and
// draws, 0 when a play-once sequence ends.
using DrawEffectFn = int(__fastcall*)(void* e, void* edx, const float* a, const float* b);
CreateEffectFn g_createEffect = nullptr;
DrawEffectFn g_drawEffect = nullptr;

void* g_subject = nullptr;  // S being updated (0043A8A0), from the update detour

struct Blood {
  ps1::BloodSequence seq;
  int age;
};
std::unordered_map<void*, Blood> g_blood;

float Side(ps1::BloodSequence seq, int age) {
  return ps1::BloodScaledSide(ps1::BloodFrameAt(seq, age).cell) * kSideScale;
}

// Blended side: from the side of the current frame to that of the next over the course of the frame.
float SmoothSide(ps1::BloodSequence seq, int age) {
  const int frame = ps1::BloodFrameAt(seq, age).frame;
  if (frame < 0) return Side(seq, age);
  int start = age;
  while (start > 0 && ps1::BloodFrameAt(seq, start - 1).frame == frame) --start;
  int end = age;
  while (ps1::BloodFrameAt(seq, end + 1).frame == frame) ++end;
  const float now = Side(seq, age);
  const float next = ps1::BloodFrameAt(seq, end + 1).frame < 0 ? now : Side(seq, end + 1);
  return now + (next - now) * static_cast<float>(age - start + 1) / static_cast<float>(end - start + 2);
}

int __fastcall CreateEffectDetour(void* m, void* edx, const char* name, float step, float stepB, float sideA,
                                  float sideB, float shrink, const char* bmp, int cellW, int cellH, int zero,
                                  float x, float y, float z, int blend, int once) {
  uint8_t *o, *t;
  if (!g_config.ps1ZombieBlood || reinterpret_cast<uintptr_t>(_ReturnAddress()) != game::kBloodCallReturn ||
      !g_subject || !GetObjects(g_subject, o, t)) {
    return g_createEffect(m, edx, name, step, stepB, sideA, sideB, shrink, bmp, cellW, cellH, zero, x, y, z, blend,
                          once);
  }
  const ps1::BloodSequence seq = cellW == kBloodCellA ? ps1::BloodSequence::A : ps1::BloodSequence::B;
  // On the crosshair's ray (0042CB04: origin + direction x t = O+90), kPullToEye towards the eye (T+8, the
  // player's position, y = height of the view).
  const float* hit = reinterpret_cast<const float*>(o + game::O::kHitPos);
  const float* eye = reinterpret_cast<const float*>(t + game::O::kPos);
  const float dx = eye[0] - hit[0], dy = eye[1] - hit[1], dz = eye[2] - hit[2];
  const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
  const float k = len > kPullToEye * 2.0f ? kPullToEye / len : 0.0f;
  const float nx = hit[0] + dx * k;
  const float ny = hit[1] + dy * k;
  const float nz = hit[2] + dz * k;
  const float newStep = 0.0f;  // F0 is written by DrawEffectDetour
  const float side = Side(seq, 0);
  const int index =
      g_createEffect(m, edx, name, newStep, stepB, side, side, shrink, bmp, cellW, cellH, zero, nx, ny, nz, blend, once);
  if (index >= 0) {
    auto** effects = game::Read<void**>(m, game::kEffectList);
    if (effects && effects[index]) g_blood[effects[index]] = {seq, 0};
  }
  return index;
}

int __fastcall DrawEffectDetour(void* e, void* edx, const float* axisA, const float* axisB) {
  auto it = g_blood.find(e);
  if (it == g_blood.end()) return g_drawEffect(e, edx, axisA, axisB);
  Blood& b = it->second;
  const float side = SmoothSide(b.seq, b.age);
  *game::Field<float>(e, game::kEffectSideA) = side;
  *game::Field<float>(e, game::kEffectSideB) = side;
  // Cell = F0 / +EC - 1 truncated; -1 (PS1 end) -> 6.5, which 0040EA1F closes (play-once sequence).
  const int cell = ps1::BloodPcCell(b.seq, b.age, kPcCells);
  *game::Field<float>(e, game::kEffectFrame) =
      ((cell < 0 ? kPcCells : cell) + 1.5f) * game::Read<float>(e, game::kEffectFrameDiv);
  const int result = g_drawEffect(e, edx, axisA, axisB);
  ++b.age;
  if (!result || b.age > ps1::BloodDuration(b.seq) + 30) g_blood.erase(it);
  return result;
}

}  // namespace

void SetBloodSubject(void* s) { g_subject = s; }

void SpawnHeadBlood(void* s, const uint8_t* o) {
  // 0043A8A0 keeps the effect manager at S+08. Reuse its blood2 sprite with
  // the native parameters, positioned slightly below bone 3 at the neck. The
  // ordinary hit path subsequently creates the impact cloud at the head too.
  void* manager = s ? game::Read<void*>(s, 0x08) : nullptr;
  float head[3];
  if (!manager || !BoneWorld(o, 3, head)) return;
  head[1] += 120.0f;  // PC world Y increases downward: head joint -> neck
  auto create = reinterpret_cast<CreateEffectFn>(game::kEffectCreate);
  create(manager, nullptr, reinterpret_cast<const char*>(0x005BAFAC),
         0.1f, 0.1f, 600.0f, 600.0f, 0.8f,
         reinterpret_cast<const char*>(0x005BAFB4), 32, 32, 0,
         head[0], head[1], head[2], 1, 1);
}

void SpawnHeadBurst(void* s, const uint8_t* o) {
  // RE2's head loss combines its detached-head event with the regular hit
  // cloud. Survivor cannot safely draw a detached model part yet, so preserve
  // the real hidden head and add the two native blood sequences as a short,
  // denser decapitation burst rather than inventing a foreign particle format.
  void* manager = s ? game::Read<void*>(s, 0x08) : nullptr;
  float head[3];
  if (!manager || !BoneWorld(o, 3, head)) return;
  auto create = reinterpret_cast<CreateEffectFn>(game::kEffectCreate);
  const char* name = reinterpret_cast<const char*>(0x005BAFAC);
  const char* blood2 = reinterpret_cast<const char*>(0x005BAFB4);
  const char* blood1 = "RESOURCE\\blood1.BMP";

  // Broad impact cloud at the sever point, followed by the longer neck spray.
  create(manager, nullptr, name, 0.1f, 0.1f, 760.0f, 760.0f, 0.82f,
         blood1, 40, 40, 0, head[0], head[1] + 35.0f, head[2], 1, 1);
  create(manager, nullptr, name, 0.1f, 0.1f, 540.0f, 540.0f, 0.8f,
         blood2, 32, 32, 0, head[0] - 90.0f, head[1] + 120.0f, head[2] + 45.0f, 1, 1);
  create(manager, nullptr, name, 0.1f, 0.1f, 540.0f, 540.0f, 0.8f,
         blood2, 32, 32, 0, head[0] + 90.0f, head[1] + 120.0f, head[2] - 45.0f, 1, 1);
  // EF03_0 is Survivor's eight-cell flesh/debris burst. It is the native
  // analogue of RE2's extra decapitation debris and remains tied only to a
  // confirmed lethal head hit.
  create(manager, nullptr, "head_debris", 0.12f, 0.12f, 680.0f, 680.0f, 0.82f,
         "RESOURCE\\EF03_0.BMP", 32, 32, 0,
         head[0], head[1] + 45.0f, head[2], 1, 1);
}

bool InstallZombieBlood() {
  if (!g_config.ps1ZombieBlood) return false;
  const game::ByteSignature checks[] = {
      {game::kEffectCreate, game::kEffectCreateBytes, sizeof(game::kEffectCreateBytes)},
      {game::kEffectDraw, game::kEffectDrawBytes, sizeof(game::kEffectDrawBytes)},
      {game::kBloodCall, game::kBloodCallBytes, sizeof(game::kBloodCallBytes)},
  };
  for (const auto& check : checks) {
    if (!game::CheckBytes(check)) {
      Log("Ps1ZombieBlood: unexpected bytes at %08X, PC blood unchanged", static_cast<unsigned>(check.address));
      g_config.ps1ZombieBlood = false;
      return false;
    }
  }
  return Hook("CreateEffect", game::kEffectCreate, reinterpret_cast<void*>(&CreateEffectDetour),
              reinterpret_cast<void**>(&g_createEffect)) &&
         Hook("DrawEffect", game::kEffectDraw, reinterpret_cast<void*>(&DrawEffectDetour),
              reinterpret_cast<void**>(&g_drawEffect));
}

}  // namespace sf::features::zombie
