// Ps1ZombiePose: the pose the PS1 adds to the zombie over the animation (ps1/pose_logic.h).
//   - Head towards the player (E+364, 80101130 / 801010EC), on bone 3.
//   - Torso bend on a hit without reaction (E+368, 80101474 row 4), on bone 2 and reversed on bone 6.
// On the PC the bones are composed when the model is drawn by 00412030 (__thiscall(M, a, b), RET 8, reads
// the bones from M+78/M+7C, called by 0043CF05 and 0043D109 for the enemies): for the duration of the draw
// the animation matrix (+40) of those bones is multiplied by the rotation in their frame and then
// restored. The game's animation state does not change.
// TODO: 801046F4 node 5 also adds the torso's y rotation (E+36C) while the bend is active.
// ASSUMPTION: the local axes of the PC bones are those of the PS1 nodes (same sequences).
#include "zombie_shared.h"

#include "../../ps1/pose_logic.h"

namespace sf::features::zombie {

namespace {

struct Pose {
  int head = 0;   // E+364
  int twist = 0;  // E+368
  DWORD seen = 0;
};
std::unordered_map<void*, Pose> g_poseBySubject;          // per S
std::unordered_map<const void*, void*> g_subjectByModel;  // M -> S
std::unordered_map<const void*, void*> g_subjectByDrawn;  // M and its tracks -> S (DeathAfterDraw)

constexpr unsigned kTorsoBone = 2;   // PS1 node 1
constexpr unsigned kHeadBone = 3;    // PS1 node 2
constexpr unsigned kPelvisBone = 6;  // PS1 node 5

using DrawModelFn = int(__fastcall*)(void* m, void* edx, int a, int b);
DrawModelFn g_drawModel = nullptr;

void PreRotate(float* anim, char axis, float radians) {
  const float c = std::cos(radians), s = std::sin(radians);
  float r[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  if (axis == 'y') {
    r[0] = c; r[2] = -s; r[8] = s; r[10] = c;  // D3DXMatrixRotationY
  } else {
    r[0] = c; r[1] = s; r[4] = -s; r[5] = c;   // D3DXMatrixRotationZ
  }
  float out[16];
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      float v = 0.0f;
      for (int k = 0; k < 4; ++k) v += r[i * 4 + k] * anim[k * 4 + j];
      out[i * 4 + j] = v;
    }
  }
  std::memcpy(anim, out, sizeof out);
}

float Radians(int ps1Units) { return static_cast<float>(ps1Units) * 6.28318531f / 4096.0f; }

int DrawPosed(void* m, void* edx, int a, int b);

// Drawing also advances the animation (00412469: inc [M+44]): the PS1 death held on its last pose
// restores the cursors after every draw (DeathAfterDraw).
// Does the drawn M still belong to S? Protected reads: at the doors the game frees the room's objects and
// reuses the addresses for the door's models, while the maps above still hold the previous zombie.
// Without this check DeathAfterDraw read an S already freed (crash in GetObjects, S+0C = 93, zombie killed
// in 07/00 and the door to 08/01).
bool StillOwns(void* s, const void* m) {
  auto read = [](const void* from, void* to, size_t size) {
    SIZE_T got = 0;
    return from && ReadProcessMemory(GetCurrentProcess(), from, to, size, &got) && got == size;
  };
  uint8_t* c = nullptr;
  uint8_t** objects = nullptr;
  int index = -1;
  uint8_t* o = nullptr;
  uint8_t* model = nullptr;
  if (!read(static_cast<uint8_t*>(s) + game::S::kCollection, &c, sizeof c) ||
      !read(c + game::C::kObjects, &objects, sizeof objects) ||
      !read(static_cast<uint8_t*>(s) + game::S::kIndex, &index, sizeof index) || index < 0 || index > 4096 ||
      !read(objects + index, &o, sizeof o) || !read(o + game::O::kModel, &model, sizeof model) || !model) {
    return false;
  }
  if (model == m) return true;
  uint8_t* tracks = nullptr;
  unsigned n = 0;
  if (!read(model + game::M::kTracks, &tracks, sizeof tracks) || !read(model + game::M::kTrackCount, &n, sizeof n))
    return false;
  for (unsigned k = 0; tracks && k < n && k < 8; ++k)
    if (tracks + k * game::M::kTrackSize == m) return true;
  return false;
}

int __fastcall DrawModelDetour(void* m, void* edx, int a, int b) {
  auto posed = g_subjectByModel.find(m);
  if (posed != g_subjectByModel.end() && !StillOwns(posed->second, m)) g_subjectByModel.erase(posed);
  const int result = DrawPosed(m, edx, a, b);
  auto owner = g_subjectByDrawn.find(m);
  if (owner != g_subjectByDrawn.end()) {
    if (StillOwns(owner->second, m)) {
      DeathAfterDraw(owner->second);
    } else {
      g_subjectByDrawn.erase(owner);
    }
  }
  return result;
}

int DrawPosed(void* m, void* edx, int a, int b) {
  auto owner = g_subjectByModel.find(m);
  if (owner == g_subjectByModel.end() || !g_config.ps1ZombiePose) return g_drawModel(m, edx, a, b);
  auto pose = g_poseBySubject.find(owner->second);
  if (pose == g_poseBySubject.end() || GetTickCount() - pose->second.seen > kAliveMs ||
      (!pose->second.head && !pose->second.twist)) {
    return g_drawModel(m, edx, a, b);
  }
  auto* bones = game::Read<uint8_t*>(m, game::M::kBones);
  const unsigned count = game::Read<unsigned>(m, game::M::kBoneCount);
  if (!bones || count <= kPelvisBone) return g_drawModel(m, edx, a, b);
  const unsigned touched[3] = {kTorsoBone, kHeadBone, kPelvisBone};
  float saved[3][16];
  for (int i = 0; i < 3; ++i) {
    std::memcpy(saved[i], bones + touched[i] * game::M::kBoneSize + game::M::kBoneAnim, sizeof saved[i]);
  }
  auto anim = [&](unsigned bone) {
    return reinterpret_cast<float*>(bones + bone * game::M::kBoneSize + game::M::kBoneAnim);
  };
  const Pose& p = pose->second;
  if (p.head) PreRotate(anim(kHeadBone), 'y', Radians(p.head));
  if (p.twist) {
    PreRotate(anim(kTorsoBone), 'z', Radians(p.twist));
    PreRotate(anim(kPelvisBone), 'z', -Radians(p.twist));
  }
  const int result = g_drawModel(m, edx, a, b);
  for (int i = 0; i < 3; ++i) {
    std::memcpy(bones + touched[i] * game::M::kBoneSize + game::M::kBoneAnim, saved[i], sizeof saved[i]);
  }
  return result;
}

// PS1 state equivalent for the head: 1 = follows the player (states 4 and 6), -1 = straightens it (1-3, 7),
// 0 = holds it.
int HeadMode(void* s, const Entry& e) {
  const int request = game::Read<int>(s, game::S::kRequest);
  if (IsDeath(request)) return -1;  // state 7
  switch (e.phase) {
    case Phase::Knockback:
    case Phase::BackFall:  // state 6
      return 1;
    case Phase::Supine:
    case Phase::SitUp:
    case Phase::GetUp1:
    case Phase::GetUp2:
      // State 4, but with clips 0E/0F/0x10/0x11 80104108 does not call the state function: E+364 stays
      // (DuckStation 2026-10-22: 60 fixed for the whole of 0E and 0F).
      return 0;
    case Phase::Idle:
      if (IsBiting(s)) return 0;              // state 5
      return IsLocomotion(request) ? 1 : 0;   // chase; 01 (sleeping, state 0) holds it
    default:
      return 0;  // stumble (0xC), lunge (0xB), grab (0xE)
  }
}

}  // namespace

void PoseOnHit(void* s, bool behind) {
  if (!g_config.ps1ZombiePose) return;
  g_poseBySubject[s].twist = ps1::TwistForHit(behind);
}

void UpdatePose(void* s, const Entry& e) {
  if (!g_config.ps1ZombiePose && !g_config.ps1ZombieDeath) return;
  uint8_t *o, *t;
  if (!GetObjects(s, o, t)) return;
  if (auto* m = game::Read<uint8_t*>(o, game::O::kModel)) {
    g_subjectByModel[m] = s;
    g_subjectByDrawn[m] = s;
    // The model's tracks may be drawn separately (0043CF05 and 0043D109): they lead to S too, so
    // DeathAfterDraw sees them all. Only for the death: the pose reads the bones and applies only to M.
    auto* tracks = game::Read<uint8_t*>(m, game::M::kTracks);
    const unsigned n = game::Read<unsigned>(m, game::M::kTrackCount);
    for (unsigned k = 0; tracks && k < n && k < 8; ++k) g_subjectByDrawn[tracks + k * game::M::kTrackSize] = s;
  }
  if (!g_config.ps1ZombiePose) return;
  Pose& p = g_poseBySubject[s];
  p.seen = GetTickCount();
  const int mode = HeadMode(s, e);
  if (mode > 0) {
    // 80101130: side of the player relative to the head's direction (heading + E+364).
    const float* fw = reinterpret_cast<const float*>(o + game::O::kForward);
    const Geometry g = Measure(o, t);
    const float a = std::atan2(fw[0], fw[2]) + Radians(p.head);
    const float hx = std::sin(a), hz = std::cos(a);
    const float cross = hx * g.toZ - hz * g.toX;  // > 0: player to the right of the head (PC, y down)
    p.head = ps1::TrackHead(p.head, cross > 0.0f);
  } else if (mode < 0) {
    p.head = ps1::RelaxHead(p.head);
  }
  p.twist = ps1::DecayTwist(p.twist);
}

bool InstallZombiePose() {
  if (!g_config.ps1ZombiePose && !g_config.ps1ZombieDeath) return false;
  if (!game::CheckBytes({game::kDrawModel, game::kDrawModelBytes, sizeof(game::kDrawModelBytes)})) {
    Log("Ps1ZombiePose: unexpected bytes at %08X, PS1 pose off", static_cast<unsigned>(game::kDrawModel));
    g_config.ps1ZombiePose = false;
    return false;
  }
  return Hook("DrawModel", game::kDrawModel, reinterpret_cast<void*>(&DrawModelDetour),
              reinterpret_cast<void**>(&g_drawModel));
}

}  // namespace sf::features::zombie
