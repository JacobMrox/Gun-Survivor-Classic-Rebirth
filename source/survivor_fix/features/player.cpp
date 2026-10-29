// Player: PS1 movement, walls, view (bob, bite jolt, field of view, wake-up) and the death view.
#include <windows.h>

#include <intrin.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "../config.h"
#include "../game.h"
#include "../hooks.h"
#include "../log.h"
#include "../ps1/damage_view_logic.h"
#include "../ps1/headbob_logic.h"
#include "../ps1/player_logic.h"
#include "../ps1/wake_view_logic.h"
#include "../ps1/wall_logic.h"
#include "features.h"

#define SF_RETURN_ADDRESS() reinterpret_cast<uintptr_t>(_ReturnAddress())

namespace sf::features {

namespace {

using PhysicsFn = void(__fastcall*)(void* c, void* edx);
using TransformFn = int(__fastcall*)(void* c, void* edx, int index, float tx, float ty, float tz,
                                     float rx, float ry, float rz);
PhysicsFn g_physics = nullptr;
TransformFn g_transform = nullptr;

int g_turnRequest = 0;  // -1/+1: direction asked by the command handler in this frame
ps1::PlayerTurn g_turn;
bool g_itemApproachPrompt = false;

uint8_t* Player(void* c, int* index) {
  auto** objects = game::Read<uint8_t**>(c, game::C::kObjects);
  const int target = game::Read<int>(c, game::C::kTarget);
  if (!objects || target < 0) return nullptr;
  if (index) *index = target;
  return objects[target];
}

void UpdateItemApproachPrompt(const uint8_t* player) {
  g_itemApproachPrompt = false;
  if (!g_config.itemApproachPrompt || !player) return;
  const float* position = game::Field<float>(const_cast<uint8_t*>(player), game::O::kPos);
  if (!std::isfinite(position[0]) || !std::isfinite(position[2])) return;
  const double px = position[0];
  const double pz = -position[2];  // ROOMENV uses the opposite Z axis.
  for (const auto& event : CurrentRoomDoors()) {
    if (!event.scripted || event.radius <= 0) continue;
    const double dx = px - event.x;
    const double dz = pz - event.z;
    const double distance = std::sqrt(dx * dx + dz * dz);
    // 80079A28 has an outer 1000-unit square guard.  The extra 600-unit
    // approach band is deliberately outside the real activation circle.
    const double outer = (std::min)(1000.0, static_cast<double>(event.radius) + 600.0);
    if (distance >= event.radius && distance < outer && std::fabs(dx) <= 1000.0 &&
        std::fabs(dz) <= 1000.0) {
      g_itemApproachPrompt = true;
      return;
    }
  }
}

bool Near(float a, float b) { return std::fabs(a - b) < 0.01f; }

float Dot(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

// 800771AC / 800785C0: the PS1 moves the player only horizontally, along their direction
// (8006F7A8), by 80 / 150 / 60 per frame from the first frame. The PC instead: the command handler
// (0046AE1C/0046AE36) adds to O+D4 a local step z = -/+256 or -/+512 divided by O+120, and 0045E730
// does O+C8 = O+C8 * 0.8 + O+D4 + gravity (O+E0 = (0, 10, 0) in the world, brought to local space by
// 0045E8F0, if O+10D): a ramp up to ~128-140 and, while walking, the local y velocity grows up to
// 50 and leaks horizontally because the local axes are not the world's (+18 on the 60 of the
// backward step). Here the end-of-frame velocity is chosen in the world: PS1 horizontal, the vertical
// one the PC would have (0.8 * vy + gravity), brought to local space with the matrix rows
// (world = lx * R0 + ly * R1 + lz * R2) and stored in O+D4 minus the gravity that 0045E730 will
// add, with O+C8 = 0.
// Gait from the step the command handler put in O+D4 (z = -/+256 or -/+512 / O+120).
ps1::Gait GaitOf(const uint8_t* t) {
  const float* accum = reinterpret_cast<const float*>(t + game::O::kAccum);
  if (!Near(accum[0], 0.0f) || !Near(accum[1], 0.0f) || Near(accum[2], 0.0f)) return ps1::Gait::None;
  const float divisor = game::Read<float>(t, game::O::kDivisor);
  if (!(divisor > 0.0f)) return ps1::Gait::None;
  const float walk = 256.0f / divisor, run = 512.0f / divisor;
  if (Near(accum[2], -walk)) return ps1::Gait::Walk;
  if (Near(accum[2], -run)) return ps1::Gait::Run;
  if (Near(accum[2], walk) || Near(accum[2], run)) return ps1::Gait::Back;  // no running backwards
  return ps1::Gait::None;  // not the command step (pushes, recoils)
}

float g_moveStep = 0.0f;   // PS1 length of this frame's step (0 = still)
float g_moveWant[3] = {};  // horizontal PS1 step of the frame in the world (ApplyMove)

void ApplyMoveSpeed(uint8_t* t, float speed);

void ApplyMove(uint8_t* t, ps1::Gait gait) {
  g_moveStep = 0.0f;
  float speed;  // positive = forward
  switch (gait) {
    case ps1::Gait::Walk:
      speed = static_cast<float>(ps1::kWalkForward);
      break;
    case ps1::Gait::Run:
      speed = static_cast<float>(ps1::kRunForward);
      break;
    case ps1::Gait::Back:
      speed = -static_cast<float>(ps1::kWalkBack);
      break;
    default:
      return;
  }
  ApplyMoveSpeed(t, speed);
}

// Horizontal step `speed` (positive = forward) along the player's direction, instead of the PC's.
void ApplyMoveSpeed(uint8_t* t, float speed) {
  g_moveStep = 0.0f;
  float* accum = game::Field<float>(t, game::O::kAccum);
  const float* m = reinterpret_cast<const float*>(t + game::O::kMatrix);
  const float* r[3] = {m, m + game::O::kMatrixRowFloats, m + 2 * game::O::kMatrixRowFloats};
  float len[3];
  for (int i = 0; i < 3; ++i) {
    len[i] = Dot(r[i], r[i]);
    if (!(len[i] > 1e-6f)) return;
  }
  // Horizontal forward: -row 2 (PlayerForward) without the vertical component.
  float fx = -r[2][0], fz = -r[2][2];
  const float fl = std::sqrt(fx * fx + fz * fz);
  if (!(fl > 1e-6f)) return;
  fx /= fl;
  fz /= fl;
  float* velocity = game::Field<float>(t, game::O::kVelocity);
  // Horizontal: the PS1 step from the first frame, without the PC acceleration and tail. Vertical: the
  // native gravity stays (O+C8 = O+C8 * 0.8 + O+D4 + O+E0 in local space, up to 50), which keeps the player
  // on the floor like the PS1, also going down. Removing it completely made the collision deflect it
  // horizontally against the floor, lift the player in steps going up and leave them floating going down
  // (08/02 ramp: up to 250 above the floor). The horizontal deviation of the collision is removed by
  // ClampMoveStep after the physics.
  float world[3] = {0.0f, 0.0f, 0.0f};
  for (int i = 0; i < 3; ++i) {
    for (int k = 0; k < 3; ++k) world[k] += velocity[i] * r[i][k];
  }
  const float keep[3] = {0.0f, world[1], 0.0f};  // only the vertical velocity of gravity
  const float want[3] = {fx * speed, 0.0f, fz * speed};
  for (int i = 0; i < 3; ++i) {
    accum[i] = Dot(want, r[i]) / len[i];
    velocity[i] = Dot(keep, r[i]) / len[i];
  }
  g_moveStep = std::fabs(speed);
  std::memcpy(g_moveWant, want, sizeof g_moveWant);
}

// Puts a new horizontal world step in O+D4, with the same conversion as ApplyMove.
void SetMoveWant(uint8_t* t, const float* want) {
  float* accum = game::Field<float>(t, game::O::kAccum);
  const float* m = reinterpret_cast<const float*>(t + game::O::kMatrix);
  for (int i = 0; i < 3; ++i) {
    const float* r = m + i * game::O::kMatrixRowFloats;
    const float len = Dot(r, r);
    if (!(len > 1e-6f)) return;
    accum[i] = Dot(want, r) / len;
  }
}

// After the physics: the horizontal movement of the frame does not exceed the PS1 step (gravity against the
// floor or the ramp does not lengthen it); the direction stays the one given by the collision. The whole
// movement is shortened, height included: the point stays on the surface the physics slid it along.
// Shortening only x and z, going down the 08/02 ramp every frame dropped more than the surface and at the
// end of the ramp the player was below the floor, stuck.
void ClampMoveStep(uint8_t* t, const float* before) {
  if (!(g_moveStep > 0.0f)) return;
  float* p = game::Field<float>(t, game::O::kPos);
  const float dx = p[0] - before[0], dz = p[2] - before[2];
  const float d = std::sqrt(dx * dx + dz * dz);
  if (!(d > g_moveStep)) return;
  const float k = g_moveStep / d;
  p[0] = before[0] + dx * k;
  p[1] = before[1] + (p[1] - before[1]) * k;
  p[2] = before[2] + dz * k;
  float* m = game::Field<float>(t, game::O::kMatrix);  // translation at +6C = O.pos
  m[3 * game::O::kMatrixRowFloats + 0] = p[0];
  m[3 * game::O::kMatrixRowFloats + 1] = p[1];
  m[3 * game::O::kMatrixRowFloats + 2] = p[2];
}

void ApplyTurn(void* c, int index) {
  const int rate = ps1::StepTurn(g_turn, g_turnRequest);
  g_turnRequest = 0;
  if (rate == 0) return;
  g_transform(c, nullptr, index, 0.0f, 0.0f, 0.0f, 0.0f, rate * 360.0f / 4096.0f, 0.0f);
}

using SetViewFn = int(__fastcall*)(void* cam, void* edx, const float* a, const float* b, const float* c);
SetViewFn g_setView = nullptr;

ps1::HeadBob g_bob;
int g_bobOffset = 0;
float g_zero = 0.0f;

// Player view. SetView (00467440): LookAt(eye = b, look-at = a, up = c) (00595CF9), then
// T(0, 0, -|a - b|) * [cam+0x3420] * T(0, 0, +|a - b|) * scale 0.01: the camera is at b. 00460450,
// in the player's frame: b = (x, y - 1, z), a = (x, y + 20, z - 200) ([5AD310], [5AD450]), so the
// view looks down by atan(21/200) = 6.0 degrees. On the PS1 the eye is on the player and looks at
// (0, 500, -3000) (8006DD5C, 80033FD8): 9.46 degrees down.
// Jolt (800830F8 -> 8009B71C): direction turned by 800A865E and raised by the difference between the
// angle of (800B3EF8, 3000) and the one at rest (500, 3000). y pointing down like on the PS1.
// Direction: a positive 800A865E turns like the right button (on the PS1 the left one lowers 800A8606;
// DuckStation frames: the zombie moves to the left). On the PC the left one increases atan2(x, z) of the
// direction, and (x c - z s, x s + z c) with a positive angle lowers it.
int g_joltYaw = 0;
int g_joltLookY = ps1::kViewLookY;

bool JoltActive() { return g_joltYaw != 0 || g_joltLookY != ps1::kViewLookY; }

// Ps1PlayerDeath (screen_fx.cpp): the view falls with 8008369C step 1 and twists in step 4.
struct DeathView {
  bool active = false;
  int eye = 0;    // eyes this much lower (PS1 units, y down): 800A8634 + 0x640
  int pitch = 0;  // steps of 800A865C (4096 per turn, negative = upwards)
  int twist = 0;  // 800B3F00, GsRVIEW2.rz (4096 = 1 degree)
};
DeathView g_death;
int g_deathSlide = 0;  // (0, 0, slide) along their PS1 axes at the next physics frame (positive = forward)

// Ps1View: the PS1 resting pitch instead of the PC's.
const float kPs1ViewPitch = std::atan2(static_cast<float>(ps1::kViewLookY), static_cast<float>(-ps1::kViewLookZ));
const float kPcViewPitch = std::atan2(21.0f, 200.0f);

// Turns the eye -> look-at direction keeping the eye still.
void AdjustPlayerView(const float* eye, float* at) {
  const bool jolt = g_config.ps1DamageView && JoltActive();
  if (!jolt && !g_config.ps1View && !g_death.active) return;
  const float r[3] = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
  const float h = std::sqrt(r[0] * r[0] + r[2] * r[2]);
  if (!(h > 0.0f)) return;
  const float len = std::sqrt(h * h + r[1] * r[1]);
  float pitch = std::atan2(r[1], h);
  if (g_config.ps1View) pitch += kPs1ViewPitch - kPcViewPitch;
  // 8008369C: 800A865C - 0x20 per frame; in the PS1 matrix (8009B71C) a negative rx raises the view, here
  // the pitch is positive downwards. ASSUMPTION: same direction.
  if (g_death.active) pitch += g_death.pitch * 6.28318531f / 4096.0f;
  float yaw = 0.0f;
  if (jolt) {
    const float depth = static_cast<float>(-ps1::kViewLookZ);
    pitch += std::atan2(static_cast<float>(g_joltLookY), depth) - kPs1ViewPitch;
    yaw = g_joltYaw * 6.28318531f / 4096.0f;
  }
  const float cy = std::cos(yaw), sy = std::sin(yaw);
  const float hx = r[0] / h, hz = r[2] / h;
  const float nh = len * std::cos(pitch);
  at[0] = eye[0] + (hx * cy - hz * sy) * nh;
  at[1] = eye[1] + len * std::sin(pitch);
  at[2] = eye[2] + (hx * sy + hz * cy) * nh;
}

// The PC rebuilds the player view (00460450 -> SetView) only on the frames that need it (no call
// during the 12 frames of the bite, with the player standing still). The player's last arguments are
// kept and, while a jolt is on or has just ended, the physics calls SetView again.
// Checked on 2026-10-17 by reading [5FD0CC]+0x33E0 during the bite: direction 0 -> -26.4 -> 0 degrees,
// pitch 5.7 -> -22.2 degrees, 440 ms, like 800830F8.
struct LastView {
  void* cam = nullptr;
  float a[3], b[3], c[3];
  bool hasUp = false;
  bool valid = false;
};
LastView g_lastView;
bool g_viewThisFrame = false;
bool g_joltShown = false;  // the last view set contained the jolt

int SetPlayerView(void* cam, void* edx, const float* a, const float* b, const float* c) {
  float at[3] = {a[0], a[1], a[2]};
  float eye[3] = {b[0], b[1], b[2]};
  if (g_config.ps1HeadBob) {
    eye[1] += g_bobOffset;
    at[1] += g_bobOffset;
  }
  if (g_death.active) {
    eye[1] += static_cast<float>(g_death.eye);
    at[1] += static_cast<float>(g_death.eye);
  }
  AdjustPlayerView(eye, at);
  g_joltShown = JoltActive() || g_death.active;
  float up[3];
  if (g_death.active && g_death.twist != 0 && c) {
    // View twist around the view direction (Rodrigues).
    float k[3] = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
    const float kl = std::sqrt(Dot(k, k));
    if (kl > 1e-6f) {
      for (float& v : k) v /= kl;
      const float angle = g_death.twist / 4096.0f * 0.0174532925f;
      const float co = std::cos(angle), si = std::sin(angle), kv = Dot(k, c);
      const float cross[3] = {k[1] * c[2] - k[2] * c[1], k[2] * c[0] - k[0] * c[2], k[0] * c[1] - k[1] * c[0]};
      for (int i = 0; i < 3; ++i) up[i] = c[i] * co + cross[i] * si + k[i] * kv * (1.0f - co);
      c = up;
    }
  }
  return g_setView(cam, edx, at, eye, c);
}

int __fastcall SetViewDetour(void* cam, void* edx, const float* a, const float* b, const float* c) {
  const bool fromPlayer = SF_RETURN_ADDRESS() == game::kPlayerViewReturn;
  // Bob: same offset for a and b, pitch unchanged.
  if ((g_config.ps1HeadBob || g_config.ps1DamageView || g_config.ps1View || g_config.ps1PlayerDeath) && fromPlayer &&
      a && b) {
    g_lastView.cam = cam;
    std::memcpy(g_lastView.a, a, sizeof g_lastView.a);
    std::memcpy(g_lastView.b, b, sizeof g_lastView.b);
    g_lastView.hasUp = c != nullptr;
    if (c) std::memcpy(g_lastView.c, c, sizeof g_lastView.c);
    g_lastView.valid = true;
    g_viewThisFrame = true;
    return SetPlayerView(cam, edx, a, b, c);
  }
  return g_setView(cam, edx, a, b, c);
}

// After the physics: if the game did not rebuild the view and the jolt is on (or the view still
// shows it), it is rebuilt with the player's last arguments.
void RefreshJoltView() {
  if ((!g_config.ps1DamageView && !g_death.active && !g_joltShown) || g_viewThisFrame || !g_lastView.valid) return;
  if (!JoltActive() && !g_death.active && !g_joltShown) return;
  SetPlayerView(g_lastView.cam, nullptr, g_lastView.a, g_lastView.b, g_lastView.hasUp ? g_lastView.c : nullptr);
}

// Ps1PlayerWalls -----------------------------------------------------------------
// PS1 (800785C0 -> 8007CB9C, ps1/wall_logic.h): the player is a point that stops on the wall
// segments of the room ([800B60CC], the same as the PC ROOMENV), already widened around the
// objects. The PC (00461000) treats them as a sphere of radius 650 (O+C4 = O+38) against the
// object boxes (mode 2, the real edges) and the mesh (3): the box at the 07/04 start stops at 650
// from the edge, the PS1 at 60-184. In the player's query the boxes are excluded with bit 2 of
// B+108 (0046114D) and then restored; the mesh stays (floor). After the physics the player's step
// goes through the PS1 walls. Wall coordinates: same x, z opposite to the PC's.
using QueryFn = int(__fastcall*)(void* c, void* edx, int index, float* disp, float* resp, float* dir);
QueryFn g_query = nullptr;
// 0045FAE0: __thiscall(mesh, face, recordA, recordB, recordC), RET 0x10.
// Records are 40 bytes; position is the first three floats (00461850).
// Observe triangles already read by 00461000; do not lock the vertex buffer again.
using ReadTriangleFn = void(__fastcall*)(void*, void*, int, float*, float*, float*);
ReadTriangleFn g_readTriangle = nullptr;
struct NativeTriangle {
  void* mesh = nullptr;
  int face = -1;
  float point[3] = {}, normal[3] = {}, rawNormal[3] = {};
  bool valid = false;
};
thread_local bool g_collectTriangle = false;
thread_local NativeTriangle g_nativeTriangle;

// 0045F090: model+14 has 0x18-byte parts, face start/count at +8/+C,
// bone at +10. Apply B+40 then B+C0 along B+100 parents, row vectors.
// This keeps translated parts in world coordinates; reject unterminated chains.
bool TriangleToWorld(void* mesh, int face, float (&points)[3][3]) {
  const auto* model = game::Read<const uint8_t*>(mesh, 0x7C);
  if (!model) return false;
  const auto* parts = game::Read<const uint8_t*>(model, 0x14);
  const unsigned count = game::Read<unsigned>(model, 0x18);
  if (!parts || !count || count > 1024) return false;
  const uint8_t* bone = nullptr;
  bool found = false;
  for (unsigned i = 0; i < count; ++i) {
    const auto* part = parts + i*0x18;
    const unsigned first = game::Read<unsigned>(part, 8), faces = game::Read<unsigned>(part, 12);
    if (face >= 0 && unsigned(face) >= first && unsigned(face)-first < faces) {
      bone = game::Read<const uint8_t*>(part, 16);found = true;break;
    }
  }
  if (!found) return false;
  for (int depth = 0; bone && depth < 64; ++depth) {
    for (size_t offset : {size_t(0x40), size_t(0xC0)}) {
      const auto* m = reinterpret_cast<const float*>(bone+offset);
      for (int k = 0; k < 16; ++k) if (!std::isfinite(m[k])) return false;
      for (auto& point : points) {
        const float w = point[0]*m[3]+point[1]*m[7]+point[2]*m[11]+m[15];
        if (w == 0.0f || !std::isfinite(w)) return false;
        float next[3];
        for (int k = 0; k < 3; ++k) {
          next[k] = (point[0]*m[k]+point[1]*m[4+k]+point[2]*m[8+k]+m[12+k])/w;
          if (!std::isfinite(next[k])) return false;
        }
        std::memcpy(point,next,sizeof next);
      }
    }
    bone = game::Read<const uint8_t*>(bone, 0x100);
  }
  return bone == nullptr;
}

void __fastcall ReadTriangleDetour(void* mesh, void* edx, int face, float* a, float* b, float* c) {
  g_readTriangle(mesh, edx, face, a, b, c);
  if (!g_collectTriangle) return;
  g_nativeTriangle = {};
  if (!a || !b || !c || face < 0) return;
  for (int i = 0; i < 3; ++i) {
    if (!std::isfinite(a[i]) || !std::isfinite(b[i]) || !std::isfinite(c[i])) return;
  }
  const float ab[3] = {b[0]-a[0], b[1]-a[1], b[2]-a[2]};
  const float ac[3] = {c[0]-a[0], c[1]-a[1], c[2]-a[2]};
  const float n[3] = {ab[1]*ac[2]-ab[2]*ac[1], ab[2]*ac[0]-ab[0]*ac[2], ab[0]*ac[1]-ab[1]*ac[0]};
  const float length = std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
  if (!(length > 0.0f) || !std::isfinite(length)) return;
  float points[3][3];
  std::memcpy(points[0],a,sizeof points[0]);
  std::memcpy(points[1],b,sizeof points[1]);
  std::memcpy(points[2],c,sizeof points[2]);
  if (!TriangleToWorld(mesh,face,points)) return;
  const float u[3] = {points[1][0]-points[0][0],points[1][1]-points[0][1],points[1][2]-points[0][2]};
  const float v[3] = {points[2][0]-points[0][0],points[2][1]-points[0][1],points[2][2]-points[0][2]};
  const float world[3] = {u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]};
  const float worldLength = std::sqrt(world[0]*world[0]+world[1]*world[1]+world[2]*world[2]);
  if (!(worldLength > 0.0f) || !std::isfinite(worldLength)) return;
  g_nativeTriangle.mesh = mesh;
  g_nativeTriangle.face = face;
  std::memcpy(g_nativeTriangle.point, points[0], sizeof g_nativeTriangle.point);
  for (int i = 0; i < 3; ++i) {
    g_nativeTriangle.rawNormal[i] = n[i]/length;
    g_nativeTriangle.normal[i] = world[i]/worldLength;
  }
  g_nativeTriangle.valid = true;
}

bool g_nativeDoorAccessReady = false;
bool g_native0801DoorAccessReady = false;

// PS1 800785C0 -> 8007CB9C/8007C60C then 80079A28 checks the original event ranges.
// PC 0047B020 exit 0C instead needs x in [7298, 8302], z <= -5838. Its step begins
// beyond the imported wall (ROOMENV 07/00: -5056), so clamping XZ first prevents
// both the native elevation and the door test. Native 3D physics is used only in that
// actual exit approach. The entrance is derived from ROOMENV, not a guessed margin.
// Locks, input/facing checks and the transition remain in the native stage handler.
bool NativeDoorPassage(const float* p, const float* before = nullptr) {
  if (!p) return false;
  if (g_nativeDoorAccessReady && IsCurrentRoom(7, 0)) {
    const float minX = game::Read<float>(reinterpret_cast<void*>(game::kNative0700DoorMinX), 0);
    const float maxX = game::Read<float>(reinterpret_cast<void*>(game::kNative0700DoorMaxX), 0);
    const float exitZ = game::Read<float>(reinterpret_cast<void*>(game::kNative0700DoorZ), 0);
    const bool outside = before && (!std::isfinite(before[0]) || before[0] < minX || before[0] > maxX);
    if (!outside && ps1::InNegativeZDoorPassage(CurrentRoomWalls(), minX, maxX, exitZ, p[0], p[2])) {
      return true;
    }
  }
  // PS1 800785C0 checks events 80079A28 after walls 8007CB9C/8007C60C.
  // PC 004941F0 Storage 0D instead needs X in [-3352.1, -3338] behind wall X = -2656.
  // Native 3D physics is reused in the proven exit approach, keeping the native event,
  // input/facing/locks intact. The separate Managers door 0F remains outside it.
  if (g_native0801DoorAccessReady && IsCurrentRoom(8, 1)) {
    const float minX = game::Read<float>(reinterpret_cast<void*>(game::kNative0801DoorMinX), 0);
    const float maxX = game::Read<float>(reinterpret_cast<void*>(game::kNative0801DoorMaxX), 0);
    const float minZ = game::Read<float>(reinterpret_cast<void*>(game::kNative0801StorageMinZ), 0);
    const float maxZ = game::Read<float>(reinterpret_cast<void*>(game::kNative0801StorageMaxZ), 0);
    const bool valid = std::isfinite(minX) && minX < maxX;
    const bool outside = before && (!std::isfinite(before[2]) || before[2] < minZ || before[2] > maxZ);
    if (valid && !outside && ps1::InNegativeXDoorPassage(CurrentRoomWalls(), minZ, maxZ, maxX, p[0], p[2])) {
      return true;
    }
  }
  // All the other doors: inside the PS1 door circle (80079A28, type 4 events of the room) the PS1
  // already opens against the wall, while the PC exit area can lie beyond the imported wall (08/02:
  // doors 496 from walls x -3232 and 3520, the door did not open). There the step stays with the
  // native physics, which reaches the PC exit.
  // The loader can also retain original type-3 scripted interaction ranges
  // (80079A28 -> 8008DA00). These use the same approach policy in every room;
  // native stage handlers still decide interaction, facing and progression.
  return ps1::InDoorEvent(CurrentRoomDoors(), p[0], p[2]);
}

constexpr float kNoNormal = 99.0f;  // placeholder: normal not written by the query
// The mesh face hit is the same wall as the PS1 wall if the wall passes within this distance of the face's
// point: at the bottom of the 08/02 ramp the floor's edge was 300 from the PS1 wall z -680, parallel, and
// replacing it let the player into the floor.
constexpr float kMeshWallPs1Match = 200.0f;
constexpr size_t kPlayerRadius = 0xC4;  // O+C4, radius of the player's sphere (650)
constexpr size_t kBoxResponseMode = 0x111;  // B+111 -> 00461000 -> 0040CA40
constexpr uint8_t kNativeStepResponse = 4;

// 00461000 uses O+B8 as the sphere centre and transforms the two box corners
// through 0045E980. A box below that centre can provide native vertical support;
// filtering it as a horizontal obstacle removes that support before mesh queries.
// Keep the original native response instead of synthesising an upward movement.
// 03/07 is a verified example: centre Y=-360, floor box top Y=-200, response 2.
bool NativeBoxCanSupport(const uint8_t* self, const uint8_t* box) {
  if (!g_config.ps1PlayerHeightSupport || !self || !box) return false;
  const float centerY = game::Read<float>(self, 0xBC);  // O+B8.y (00461000)
  const auto* a = reinterpret_cast<const float*>(box + 0x20);
  const auto* b = reinterpret_cast<const float*>(box + 0x2C);
  const auto* m = reinterpret_cast<const float*>(box + game::O::kMatrix);
  if (!std::isfinite(centerY)) return false;
  for (int i = 0; i < 3; ++i) {
    if (!std::isfinite(a[i]) || !std::isfinite(b[i]) || !std::isfinite(m[i * 4 + 1])) return false;
  }
  if (!std::isfinite(m[13]) || !(m[1] * m[1] + m[5] * m[5] + m[9] * m[9] > 0.0f)) return false;
  const float ay = a[0] * m[1] + a[1] * m[5] + a[2] * m[9] + m[13];
  const float by = b[0] * m[1] + b[1] * m[5] + b[2] * m[9] + m[13];
  return std::isfinite(ay) && std::isfinite(by) && (std::min)(ay, by) >= centerY;
}

// 00461000 shortens disp in place before visiting the next collider. Coincident
// mesh risers can consume the whole step before later mode-4 boxes see it. Visit
// native height responders first for this one query, preserving the designated
// player slot. Restore the collection and map the returned contact index before
// the caller sees it; actor indices and the original object array never change.
int QueryWithNativeSupport(void* c, void* edx, int index, float* disp, float* resp, float* dir) {
  if (!g_config.ps1PlayerWalls || !g_config.ps1PlayerHeightSupport || !c ||
      index != game::Read<int>(c, game::C::kTarget)) return g_query(c, edx, index, disp, resp, dir);
  auto** original = game::Read<uint8_t**>(c, game::C::kObjects);
  const int count = game::Read<int>(c, game::C::kCount);
  constexpr int capacity = 128;
  if (!original || index < 0 || index >= count || count > capacity || !original[index])
    return g_query(c, edx, index, disp, resp, dir);
  uint8_t* ordered[capacity] = {};
  int source[capacity] = {};
  bool support[capacity] = {};
  for (int i = 0; i < count; ++i) {
    auto* box = original[i];
    support[i] = i != index && box && game::Read<int8_t>(box, game::O::kCollisionMode) == 2 &&
                 (game::Read<uint32_t>(box, game::O::kCollisionFlags) & 2u) == 0 &&
                 (game::Read<uint8_t>(box, kBoxResponseMode) == kNativeStepResponse ||
                  NativeBoxCanSupport(original[index], box));
  }
  ordered[index] = original[index];
  source[index] = index;
  int next = 0;
  for (int pass = 0; pass < 2; ++pass) {
    for (int i = 0; i < count; ++i) {
      if (i == index || support[i] != (pass == 0)) continue;
      if (next == index) ++next;
      ordered[next] = original[i];
      source[next++] = i;
    }
  }
  bool changed = false;
  for (int i = 0; i < count; ++i) changed = changed || source[i] != i;
  if (!changed) return g_query(c, edx, index, disp, resp, dir);
  auto* contact = game::Field<int>(original[index], game::O::kContactIndex);
  const int savedContact = *contact;
  *contact = -1;
  *game::Field<uint8_t**>(c, game::C::kObjects) = ordered;
  const int result = g_query(c, edx, index, disp, resp, dir);
  *game::Field<uint8_t**>(c, game::C::kObjects) = original;
  *contact = *contact >= 0 && *contact < count ? source[*contact] : savedContact;
  return result;
}

int __fastcall QueryDetour(void* c, void* edx, int index, float* disp, float* resp, float* dir) {
  zombie::WorldBeforeQuery(c, index);
  if (!g_config.ps1PlayerWalls || !c || index != game::Read<int>(c, game::C::kTarget)) {
    return QueryWithNativeSupport(c, edx, index, disp, resp, dir);
  }
  auto** objects = game::Read<uint8_t**>(c, game::C::kObjects);
  const int count = game::Read<int>(c, game::C::kCount);
  uint8_t* self = objects && index >= 0 && index < count ? objects[index] : nullptr;
  const bool nativePassage = self && NativeDoorPassage(game::Field<float>(self, game::O::kPos));
  // Door approaches keep all native boxes and vertical responses. A return step
  // must still be able to leave a matched mesh wall toward the PS1 room interior.
  uint8_t* marked[128];
  int n = 0;
  for (int i = 0; !nativePassage && objects && i < count && n < 128; ++i) {
    uint8_t* b = objects[i];
    if (!b || i == index || game::Read<int8_t>(b, game::O::kCollisionMode) != 2) continue;
    // 0040CA40 mode 4 redirects a box contact upwards; 004A4870 uses it for
    // the six stair boxes in 07/03. Keep these native height contacts in every room.
    if (game::Read<uint8_t>(b, kBoxResponseMode) == kNativeStepResponse || NativeBoxCanSupport(self, b)) continue;
    uint32_t* flags = game::Field<uint32_t>(b, game::O::kCollisionFlags);
    if (*flags & 2) continue;
    *flags |= 2;
    marked[n++] = b;
  }
  // The mesh walls (mode 3) are decided by the PS1 segments too: with the 650 sphere the mesh pushed
  // the player beyond the PS1 line, which brought them back the next frame (07/04, wall z = 1824:
  // 50-250 oscillation and a step of 1 on the corner).
  // 00461000 writes the normal of the face hit in O+EC only if it is not floor
  // (dot(normal, O+E0) > -0.5) and the collider index in O+FC. If the hit is with a mesh wall
  // (|ny| < 0.5) the step stays whole and without sliding (00460CA2: rest = length - |disp|,
  // new candidate = resp * rest); floor, slopes and enemies stay with the PC.
  float wanted[3] = {};
  float savedNormal[3] = {};
  int savedHit = -1;
  if (self && disp) {
    std::memcpy(wanted, disp, sizeof wanted);
    std::memcpy(savedNormal, self + game::O::kContactNormal, sizeof savedNormal);
    savedHit = game::Read<int>(self, game::O::kContactIndex);
    float* normal = game::Field<float>(self, game::O::kContactNormal);
    normal[0] = 0.0f;
    normal[1] = kNoNormal;
    normal[2] = 0.0f;
    *game::Field<int>(self, game::O::kContactIndex) = -1;
  }
  const bool savedCollect = g_collectTriangle;
  const NativeTriangle savedTriangle = g_nativeTriangle;
  g_collectTriangle = g_readTriangle != nullptr;
  g_nativeTriangle = {};
  const int result = QueryWithNativeSupport(c, edx, index, disp, resp, dir);
  const NativeTriangle triangle = g_nativeTriangle;
  g_collectTriangle = savedCollect;
  g_nativeTriangle = savedTriangle;
  for (int k = 0; k < n; ++k) *game::Field<uint32_t>(marked[k], game::O::kCollisionFlags) &= ~2u;
  if (self && disp) {
    float* normal = game::Field<float>(self, game::O::kContactNormal);
    const bool written = normal[1] != kNoNormal;
    const int hit = game::Read<int>(self, game::O::kContactIndex);
    const bool meshWall = written && std::fabs(normal[1]) < 0.5f && hit >= 0 && hit < count &&
                          objects[hit] && game::Read<int8_t>(objects[hit], game::O::kCollisionMode) == 3;
    if (!written) std::memcpy(normal, savedNormal, sizeof savedNormal);
    if (hit == -1) *game::Field<int>(self, game::O::kContactIndex) = savedHit;
    // Only where a PS1 wall decides the same wall (near and parallel to the face): elsewhere (bottom and side
    // of the 08/02 ramp) the mesh stays solid, otherwise the player entered the ramp's volume, gravity took
    // them below the floor and the PS1 walls did not let them out again.
    // The face's point: from the position, against the normal, at the sphere's radius (O+C4 = 650).
    const float* pos = game::Field<float>(self, game::O::kPos);
    const float nl = std::hypot(normal[0], normal[2]);
    const float radius = game::Read<float>(self, kPlayerRadius);
    const float fx = nl > 1e-6f ? pos[0] - normal[0] / nl * radius : pos[0];
    const float fz = nl > 1e-6f ? pos[2] - normal[2] / nl * radius : pos[2];
    // PS1 800785C0 -> 8007CB9C/8007C60C: return movement uses the room walls.
    // The native sphere can already overlap a mesh at room entry. Its contact
    // plane cannot then be inferred from position minus radius on every step.
    // Use the exact face that the native query just read, with the same normal.
    bool entersRoom = false;
    if (meshWall && triangle.valid && triangle.mesh == objects[hit] &&
        triangle.face == game::Read<int>(self, 0x100) &&
        std::fabs(triangle.rawNormal[0]*normal[0] + triangle.rawNormal[1]*normal[1] +
                  triangle.rawNormal[2]*normal[2]) > 0.99f && std::fabs(triangle.normal[1]) < 0.5f) {
      const auto* centre = game::Field<float>(self,0xB8);
      const auto* wn = triangle.normal;
      const float plane = (centre[0]-triangle.point[0])*wn[0] +
                          (centre[1]-triangle.point[1])*wn[1] + (centre[2]-triangle.point[2])*wn[2];
      const float faceX = centre[0]-wn[0]*plane, faceZ = centre[2]-wn[2]*plane;
      entersRoom = ps1::MeshWallEntersRoom(CurrentRoomWalls(), faceX, faceZ, wn[0], wn[2],
                                         wanted[0], wanted[2], kMeshWallPs1Match);
    }
    const bool restoreMeshStep = entersRoom || (meshWall && !nativePassage &&
        ps1::MeshWallMatchesPs1(CurrentRoomWalls(), fx, fz, normal[0], normal[2], kMeshWallPs1Match));
    if (restoreMeshStep) {
      // Only the horizontal part: the vertical one stays the query's (floor and ramp). Restoring the wanted
      // y too (with gravity) let the player through the floor at the bottom of the 08/02 ramp, walking 150
      // lower (ground floor at -1438 instead of -1588).
      disp[0] = wanted[0];
      disp[2] = wanted[2];
      if (resp) resp[0] = resp[2] = 0.0f;
    }
  }
  return result;
}

void ApplyPs1Walls(uint8_t* t, const float* before) {
  const auto& walls = CurrentRoomWalls();
  float* p = game::Field<float>(t, game::O::kPos);
  if (walls.empty() || (p[0] == before[0] && p[2] == before[2])) return;
  if (NativeDoorPassage(p, before) || NativeDoorPassage(before, p)) return;
  const ps1::WallPoint p0{static_cast<int32_t>(std::lround(before[0])),
                          static_cast<int32_t>(std::lround(-before[2]))};
  ps1::WallPoint p1{static_cast<int32_t>(std::lround(p[0])), static_cast<int32_t>(std::lround(-p[2]))};
  const int hits = ps1::StepAgainstWalls(walls, p0, p1);
  if (hits == 0) return;
  // The physics computed the height at the point the wall does not let the player reach: the part of the
  // accepted step is kept. Restoring only x and z, at the bottom of the 08/02 ramp the player went back over
  // ground higher than their height, the sphere stayed inside the geometry and dropped below the floor.
  const float tried = std::hypot(p[0] - before[0], p[2] - before[2]);
  const float kept = std::hypot(static_cast<float>(p1.x) - before[0], static_cast<float>(-p1.z) - before[2]);
  if (tried > 1e-3f) p[1] = before[1] + (p[1] - before[1]) * (std::min)(1.0f, kept / tried);
  p[0] = static_cast<float>(p1.x);
  p[2] = static_cast<float>(-p1.z);
  float* m = game::Field<float>(t, game::O::kMatrix);  // translation at +6C = O.pos
  m[3 * game::O::kMatrixRowFloats + 0] = p[0];
  m[3 * game::O::kMatrixRowFloats + 1] = p[1];
  m[3 * game::O::kMatrixRowFloats + 2] = p[2];
}

// Ps1WakeView -------------------------------------------------------------------
// Opening wake-up (ps1/wake_view_logic.h). The PS1 keeps the absolute angles of the event camera
// and rebuilds the view from them (8008CA90 -> 8009B71C): the curve ends at rx = 0, level.
// The PC rotates the player's matrix in relative steps with 0045E110 from seven points of 00444560
// (game.h, kWakeRotateReturns): the pitch adds up to +2.5 degrees and rotating around the already
// tilted axes adds roll, 2.62 degrees in all until the room changes. Here those calls no longer
// rotate: from the first one, on every physics frame the matrix is the level base (direction at the
// start) turned by the PS1 yaw and pitch of that frame (Rx * Ry * base, rows like D3DX), up to the
// last frame (rx = 0). If the player turns meanwhile, the curve stops and the matrix stays level.
using RotateFn = int(__fastcall*)(void* o, void* edx, float tx, float ty, float tz, float rx, float ry, float rz);
RotateFn g_rotate = nullptr;

struct WakeView {
  uint8_t* o = nullptr;
  int frame = -1;        // -1: curve not active
  int lastCall = -1000;  // physics frame of the last PC wake-up call
  double seconds = 0.0;  // frame time since the start of the curve
  LARGE_INTEGER last = {};
  float base[3][3] = {};
};
int g_physicsFrames = 0;  // player physics frames
WakeView g_wake;

constexpr float kDegToRad = 3.14159265f / 180.0f;

bool IsWakeRotate(uintptr_t from) {
  for (uintptr_t r : game::kWakeRotateReturns) {
    if (r == from) return true;
  }
  return false;
}

float* MatrixRow(uint8_t* o, int i) {
  return reinterpret_cast<float*>(o + game::O::kMatrix) + i * game::O::kMatrixRowFloats;
}

// Level rows 0-2 with the horizontal direction of row 2 and the lengths of the current ones.
void LevelRows(uint8_t* o, float out[3][3]) {
  float len[3];
  for (int i = 0; i < 3; ++i) len[i] = std::sqrt(Dot(MatrixRow(o, i), MatrixRow(o, i)));
  const float* r2 = MatrixRow(o, 2);
  float fx = r2[0], fz = r2[2];
  const float h = std::sqrt(fx * fx + fz * fz);
  if (h > 0.0f) {
    fx /= h;
    fz /= h;
  } else {
    fx = 0.0f;
    fz = 1.0f;
  }
  const float up = MatrixRow(o, 1)[1] >= 0.0f ? 1.0f : -1.0f;
  float r0[3] = {up * fz, 0.0f, -up * fx};  // row 1 x row 2
  if (Dot(r0, MatrixRow(o, 0)) < 0.0f) {
    for (float& v : r0) v = -v;
  }
  for (int k = 0; k < 3; ++k) out[0][k] = r0[k] * len[0];
  out[1][0] = 0.0f;
  out[1][1] = up * len[1];
  out[1][2] = 0.0f;
  out[2][0] = fx * len[2];
  out[2][1] = 0.0f;
  out[2][2] = fz * len[2];
}

// Writes rows 0-2 keeping O+8 still (pivot O+14 in the world) and rebuilds O+B8 like 0045E110.
void WriteRotation(uint8_t* o, const float rows[3][3]) {
  const float* pivot = reinterpret_cast<const float*>(o + game::O::kPivot);
  const float* pos = reinterpret_cast<const float*>(o + game::O::kPos);
  for (int i = 0; i < 3; ++i) std::memcpy(MatrixRow(o, i), rows[i], 3 * sizeof(float));
  float* tr = MatrixRow(o, 3);
  for (int k = 0; k < 3; ++k) {
    tr[k] = pos[k] - (pivot[0] * rows[0][k] + pivot[1] * rows[1][k] + pivot[2] * rows[2][k]);
  }
  const float* local = reinterpret_cast<const float*>(o + game::O::kAimOffsetLocal);
  float* aim = reinterpret_cast<float*>(o + game::O::kAimPoint);
  const float q[3] = {local[0] + pivot[0], local[1] + pivot[1], local[2] + pivot[2]};
  for (int k = 0; k < 3; ++k) aim[k] = q[0] * rows[0][k] + q[1] * rows[1][k] + q[2] * rows[2][k] + tr[k];
}

// The PS1 runs at 30 frames per second, the PC physics at 22-25 during the wake-up: the curve's frame
// comes from the time, counted only while frames arrive (at most 0.1 s per frame, so a pause of the
// game does not make it jump).
void ApplyWakeFrame() {
  LARGE_INTEGER now, freq;
  QueryPerformanceCounter(&now);
  QueryPerformanceFrequency(&freq);
  if (g_wake.last.QuadPart) {
    const double dt = static_cast<double>(now.QuadPart - g_wake.last.QuadPart) / freq.QuadPart;
    g_wake.seconds += dt < 0.1 ? dt : 0.1;
  }
  g_wake.last = now;
  g_wake.frame = static_cast<int>(g_wake.seconds * 30.0);
  if (g_wake.frame >= ps1::kWakeViewFrames) g_wake.frame = ps1::kWakeViewFrames - 1;
  const ps1::WakeAngles a = ps1::WakeView(g_wake.frame);
  const float cp = std::cos(a.pitchDeg * kDegToRad), sp = std::sin(a.pitchDeg * kDegToRad);
  const float cy = std::cos(a.yawDeg * kDegToRad), sy = std::sin(a.yawDeg * kDegToRad);
  // D3DXMatrixRotationX(p) * D3DXMatrixRotationY(y), rows.
  const float p[3][3] = {{cy, 0.0f, -sy}, {sp * sy, cp, sp * cy}, {cp * sy, -sp, cp * cy}};
  float rows[3][3];
  for (int i = 0; i < 3; ++i) {
    for (int k = 0; k < 3; ++k) {
      rows[i][k] = p[i][0] * g_wake.base[0][k] + p[i][1] * g_wake.base[1][k] + p[i][2] * g_wake.base[2][k];
    }
  }
  WriteRotation(g_wake.o, rows);
  if (g_wake.frame >= ps1::kWakeViewFrames - 1) {
    g_wake.frame = -1;
    Log("Ps1WakeView: end of the PS1 curve, view level");
  }
}

int __fastcall RotateDetour(void* o, void* edx, float tx, float ty, float tz, float rx, float ry, float rz) {
  auto* t = static_cast<uint8_t*>(o);
  if (g_config.ps1WakeView && t && IsWakeRotate(SF_RETURN_ADDRESS())) {
    // A new sequence if the PC calls had stopped more than a second (30 frames) ago.
    if (g_wake.frame < 0 && g_physicsFrames - g_wake.lastCall > 30) {
      g_wake.o = t;
      g_wake.frame = 0;
      g_wake.seconds = 0.0;
      g_wake.last.QuadPart = 0;
      LevelRows(t, g_wake.base);
      Log("Ps1WakeView: wake-up, the view follows the PS1 curve (%d frames)", ps1::kWakeViewFrames);
    }
    g_wake.lastCall = g_physicsFrames;
    return 1;  // the PS1 curve replaces the PC step
  }
  const int result = g_rotate(o, edx, tx, ty, tz, rx, ry, rz);
  if (g_wake.frame >= 0 && t == g_wake.o && ry != 0.0f) {  // the player turns: curve over, level
    g_wake.frame = -1;
    float rows[3][3];
    LevelRows(t, rows);
    WriteRotation(t, rows);
    Log("Ps1WakeView: the player turned before the end of the curve, view level");
  }
  return result;
}

// 800785C0 resolves the walls on the step (8007CB9C) before moving the player. Here the PS1 step is cut on
// the PS1 walls before the PC physics: otherwise the physics took it beyond the wall, computed the height
// there (past the edge of the 08/02 ramp, in the void: lower) and ApplyPs1Walls restored only x and z;
// against the wall along the ramp, frame after frame, the player ended up inside the ramp and below the floor.
void ClampMoveToWalls(uint8_t* t) {
  if (!(g_moveStep > 0.0f)) return;
  const auto& walls = CurrentRoomWalls();
  const float* p = game::Field<float>(t, game::O::kPos);
  const float to[3] = {p[0] + g_moveWant[0], p[1], p[2] + g_moveWant[2]};
  if (walls.empty() || NativeDoorPassage(p) || NativeDoorPassage(to, p)) return;
  const ps1::WallPoint p0{static_cast<int32_t>(std::lround(p[0])), static_cast<int32_t>(std::lround(-p[2]))};
  ps1::WallPoint p1{static_cast<int32_t>(std::lround(to[0])), static_cast<int32_t>(std::lround(-to[2]))};
  if (ps1::StepAgainstWalls(walls, p0, p1) == 0) return;
  const float want[3] = {static_cast<float>(p1.x) - p[0], 0.0f, static_cast<float>(-p1.z) - p[2]};
  SetMoveWant(t, want);
  g_moveStep = std::sqrt(want[0] * want[0] + want[2] * want[2]);
}

// 8008369C step 1: 8008286C rotates (0, 0, 800BB07C) by the player's angles and the result is added to
// 800A85D8/800A85E0, or, if 8007CB9C finds a wall, the position becomes the one stopped by the wall. A positive
// value moves forward: DuckStation 2026-10-23, deadly bite in 07/00, z from -7291 to -6808 towards the zombie,
// which stays about 590 away. During the death the PC physics does not move the player (O+D4 and O+C8 are
// ignored), so the position is written here like on the PS1, after the physics.
void DeathSlideAfterPhysics(uint8_t* t) {
  const float slide = static_cast<float>(g_deathSlide);
  g_deathSlide = 0;
  const float* m = reinterpret_cast<const float*>(t + game::O::kMatrix);
  float fx = -m[2 * game::O::kMatrixRowFloats + 0], fz = -m[2 * game::O::kMatrixRowFloats + 2];
  const float fl = std::sqrt(fx * fx + fz * fz);
  if (!(fl > 1e-6f)) return;
  fx /= fl;
  fz /= fl;
  float* p = game::Field<float>(t, game::O::kPos);
  float to[3] = {p[0] + fx * slide, p[1], p[2] + fz * slide};
  const auto& walls = CurrentRoomWalls();
  if (g_config.ps1PlayerWalls && !walls.empty()) {
    const ps1::WallPoint p0{static_cast<int32_t>(std::lround(p[0])), static_cast<int32_t>(std::lround(-p[2]))};
    ps1::WallPoint p1{static_cast<int32_t>(std::lround(to[0])), static_cast<int32_t>(std::lround(-to[2]))};
    if (ps1::StepAgainstWalls(walls, p0, p1) != 0) {
      to[0] = static_cast<float>(p1.x);
      to[2] = static_cast<float>(-p1.z);
    }
  }
  p[0] = to[0];
  p[2] = to[2];
  float* mt = game::Field<float>(t, game::O::kMatrix);  // translation at +6C = O.pos
  mt[3 * game::O::kMatrixRowFloats + 0] = p[0];
  mt[3 * game::O::kMatrixRowFloats + 2] = p[2];
}

void __fastcall PhysicsDetour(void* c, void* edx) {
  int index = -1;
  uint8_t* t = c ? Player(c, &index) : nullptr;
  if (t) {
    const ps1::Gait gait = GaitOf(t);
    g_moveStep = 0.0f;  // set again by ApplyMove or by the death slide
    if (g_deathSlide != 0) {
      // The death slide is applied by DeathSlideAfterPhysics: no command step here.
    } else if (g_config.ps1PlayerMove) {
      ApplyTurn(c, index);
      ApplyMove(t, gait);
      if (g_config.ps1PlayerWalls) ClampMoveToWalls(t);
    }
    if (g_config.ps1HeadBob) g_bobOffset = ps1::StepHeadBob(g_bob, gait);
  }
  g_viewThisFrame = false;
  float before[3] = {};
  if (t) std::memcpy(before, t + game::O::kPos, sizeof before);
  if (!t) g_moveStep = 0.0f;
  zombie::WorldPhysicsBegin(c);
  g_physics(c, edx);
  zombie::WorldPhysicsEnd(c);
  if (!t) {
    g_itemApproachPrompt = false;
    return;
  }
  if (g_moveStep > 0.0f) ClampMoveStep(t, before);
  if (g_deathSlide != 0) DeathSlideAfterPhysics(t);
  if (g_config.ps1PlayerWalls) ApplyPs1Walls(t, before);
  ++g_physicsFrames;
  if (g_wake.frame >= 0 && t == g_wake.o) ApplyWakeFrame();
  RefreshJoltView();
  UpdateItemApproachPrompt(t);
}

// The rotation asked by the commands is not applied here: only its direction is taken, and
// ApplyTurn turns by the PS1 step at the physics frame.
int __fastcall TransformDetour(void* c, void* edx, int index, float tx, float ty, float tz, float rx,
                               float ry, float rz) {
  const uintptr_t from = SF_RETURN_ADDRESS();
  if (g_config.ps1PlayerMove && (from == game::kTurnLeftReturn || from == game::kTurnRightReturn) &&
      tx == 0.0f && ty == 0.0f && tz == 0.0f && rx == 0.0f && rz == 0.0f && ry != 0.0f) {
    g_turnRequest = ry > 0.0f ? 1 : -1;
    return 1;
  }
  return g_transform(c, edx, index, tx, ty, tz, rx, ry, rz);
}

// Ps1View: the PS1 vertical field of view. GsSetProjection(H = 270) (8006DD5C -> 8002D818) on 240 lines:
// 2 atan(120 / 270) = 47.9 degrees. The PC (00466F40) gives D3DXMatrixPerspectiveFovLH a fixed 40 degrees
// (push 3F32B8C3 at 00466F9A) and the aspect ratio width / height ([5FD020] / [5FD024]): with the PS1
// vertical field 4:3 is identical to the PS1 and wider screens see more at the sides (Hor+).
bool SetPs1Fov() {
  uint8_t* code = reinterpret_cast<uint8_t*>(game::kProjectionFovPush);
  const uint8_t expected[] = {0x68, 0xC3, 0xB8, 0x32, 0x3F};
  if (std::memcmp(code, expected, sizeof expected) != 0) {
    Log("Ps1View: unexpected bytes at %08X, field of view unchanged", static_cast<unsigned>(game::kProjectionFovPush));
    return false;
  }
  const float fov = 2.0f * std::atan(120.0f / 270.0f);
  DWORD old = 0;
  if (!VirtualProtect(code + 1, 4, PAGE_EXECUTE_READWRITE, &old)) return false;
  std::memcpy(code + 1, &fov, 4);
  VirtualProtect(code + 1, 4, old, &old);
  FlushInstructionCache(GetCurrentProcess(), code + 1, 4);
  Log("Ps1View: vertical field of view %.2f degrees (PC 40), pitch %.2f degrees (PC %.2f)",
      fov * 57.2957795f, kPs1ViewPitch * 57.2957795f, kPcViewPitch * 57.2957795f);
  // Projection aspect ratio: the one of the final picture (ForceWidth x ForceHeight), not of the game's
  // video mode ([5FD020] / [5FD024], 1024x768 even at 2560x1440): fild / fidiv at 00466F82 / 00466F91 read
  // from here. With the vertical field fixed, wide screens see more.
  static int width = 0, height = 0;
  uint8_t* fild = reinterpret_cast<uint8_t*>(game::kProjectionAspectWidth);
  uint8_t* fidiv = reinterpret_cast<uint8_t*>(game::kProjectionAspectHeight);
  const uint8_t fildBytes[] = {0xDB, 0x05, 0x20, 0xD0, 0x5F, 0x00};
  const uint8_t fidivBytes[] = {0xDA, 0x35, 0x24, 0xD0, 0x5F, 0x00};
  if (DisplaySize(&width, &height) && std::memcmp(fild, fildBytes, 6) == 0 &&
      std::memcmp(fidiv, fidivBytes, 6) == 0) {
    const int* pw = &width;
    const int* ph = &height;
    if (VirtualProtect(fild, 32, PAGE_EXECUTE_READWRITE, &old)) {
      std::memcpy(fild + 2, &pw, 4);
      std::memcpy(fidiv + 2, &ph, 4);
      VirtualProtect(fild, 32, old, &old);
      FlushInstructionCache(GetCurrentProcess(), fild, 32);
      Log("Ps1View: projection aspect ratio %dx%d (config.ini)", width, height);
    }
  }
  return true;
}

// 00460450: the PC bob (the same tables as the PS1, x 0.1 in local space and only on the eye) is
// removed by pointing the three multiplications at a zero in this DLL.
bool RemovePcHeadBob() {
  for (uintptr_t at : game::kPcHeadBobScales) {
    const uint8_t* code = reinterpret_cast<const uint8_t*>(at);
    if (code[0] != 0xD8 || code[1] != 0x0D) {
      Log("Ps1HeadBob: unexpected bytes at %08X, feature off", static_cast<unsigned>(at));
      return false;
    }
  }
  for (uintptr_t at : game::kPcHeadBobScales) {
    void* operand = reinterpret_cast<void*>(at + 2);
    DWORD old = 0;
    if (!VirtualProtect(operand, 4, PAGE_EXECUTE_READWRITE, &old)) return false;
    const float* zero = &g_zero;
    std::memcpy(operand, &zero, 4);
    VirtualProtect(operand, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), operand, 4);
  }
  Log("Ps1HeadBob: PC bob removed (00460715, 00460805, 004608FD), the PS1 one is used");
  return true;
}

}  // namespace

void SetDamageViewJolt(int yawUnits, int lookY) {
  g_joltYaw = yawUnits;
  g_joltLookY = lookY;
}

void SetDeathView(bool active, int eye, int pitch, int twist) {
  g_death.active = active;
  g_death.eye = eye;
  g_death.pitch = pitch;
  g_death.twist = twist;
}

void SetDeathSlide(int slide) { g_deathSlide = slide; }

int PlayerEyeBob() { return g_config.ps1HeadBob ? g_bobOffset : 0; }

bool ItemApproachPromptVisible() { return g_config.itemApproachPrompt && g_itemApproachPrompt; }

bool InstallPlayer() {
  const game::ByteSignature physics = {game::kPhysics, game::kPhysicsPrologue,
                                       sizeof(game::kPhysicsPrologue)};
  const game::ByteSignature transform = {game::kTransform, game::kTransformPrologue,
                                         sizeof(game::kTransformPrologue)};
  if (!game::CheckBytes(physics) || !game::CheckBytes(transform)) {
    Log("Player: unexpected bytes at %08X or %08X, different executable? Feature off.",
        static_cast<unsigned>(game::kPhysics), static_cast<unsigned>(game::kTransform));
    return false;
  }
  const game::ByteSignature query = {game::kCollisionQuery, game::kCollisionQueryPrologue,
                                     sizeof(game::kCollisionQueryPrologue)};
  if ((g_config.ps1PlayerWalls || g_config.ps1ZombieWorld) && !game::CheckBytes(query)) {
    Log("Ps1PlayerWalls: unexpected bytes at %08X, feature off", static_cast<unsigned>(game::kCollisionQuery));
    g_config.ps1PlayerWalls = false;
    g_config.ps1ZombieWorld = false;
  }
  if (g_config.ps1PlayerWalls) {
    const game::ByteSignature doorChecks[] = {
        {game::kNative0700DoorCheck, game::kNative0700DoorCheckBytes, sizeof(game::kNative0700DoorCheckBytes)},
        {game::kNative0700DoorMinX, game::kNative0700DoorMinXBytes, sizeof(game::kNative0700DoorMinXBytes)},
        {game::kNative0700DoorMaxX, game::kNative0700DoorMaxXBytes, sizeof(game::kNative0700DoorMaxXBytes)},
        {game::kNative0700DoorZ, game::kNative0700DoorZBytes, sizeof(game::kNative0700DoorZBytes)},
    };
    g_nativeDoorAccessReady = true;
    for (const auto& check : doorChecks) g_nativeDoorAccessReady = game::CheckBytes(check) && g_nativeDoorAccessReady;
    const game::ByteSignature storageChecks[] = {
        {game::kNative0801DoorCheck, game::kNative0801DoorCheckBytes, sizeof(game::kNative0801DoorCheckBytes)},
        {game::kNative0801DoorMinX, game::kNative0801DoorMinXBytes, sizeof(game::kNative0801DoorMinXBytes)},
        {game::kNative0801DoorMaxX, game::kNative0801DoorMaxXBytes, sizeof(game::kNative0801DoorMaxXBytes)},
        {game::kNative0801StorageMinZ, game::kNative0801StorageMinZBytes, sizeof(game::kNative0801StorageMinZBytes)},
        {game::kNative0801StorageMaxZ, game::kNative0801StorageMaxZBytes, sizeof(game::kNative0801StorageMaxZBytes)},
    };
    g_native0801DoorAccessReady = true;
    for (const auto& check : storageChecks) g_native0801DoorAccessReady = game::CheckBytes(check) && g_native0801DoorAccessReady;
    Log("Ps1PlayerWalls: PC door access 07/00 %s, 08/01 Storage %s", g_nativeDoorAccessReady ? "ready" : "off",
        g_native0801DoorAccessReady ? "ready" : "off");
  }
  if (g_config.ps1PlayerWalls) {
    const game::ByteSignature triangle = {game::kReadCollisionTriangle, game::kReadCollisionTriangleBytes,
                                          sizeof(game::kReadCollisionTriangleBytes)};
    if (!game::CheckBytes(triangle) ||
        !Hook("PlayerCollisionTriangle", game::kReadCollisionTriangle,
              reinterpret_cast<void*>(&ReadTriangleDetour), reinterpret_cast<void**>(&g_readTriangle))) {
      Log("Ps1PlayerWalls: exact triangle capture unavailable; native doorway response retained");
    }
  }
  if (g_config.ps1WakeView) {
    const game::ByteSignature rotate = {game::kRotateObject, game::kRotateObjectPrologue,
                                        sizeof(game::kRotateObjectPrologue)};
    if (!game::CheckBytes(rotate)) {
      Log("Ps1WakeView: unexpected bytes at %08X, feature off", static_cast<unsigned>(game::kRotateObject));
      g_config.ps1WakeView = false;
    } else {
      Hook("RotateObject", game::kRotateObject, reinterpret_cast<void*>(&RotateDetour),
           reinterpret_cast<void**>(&g_rotate));
    }
  }
  const bool ready =
      Hook("Physics", game::kPhysics, reinterpret_cast<void*>(&PhysicsDetour),
           reinterpret_cast<void**>(&g_physics)) &&
      ((!g_config.ps1PlayerWalls && !g_config.ps1ZombieWorld) ||
       Hook("CollisionQuery", game::kCollisionQuery, reinterpret_cast<void*>(&QueryDetour),
            reinterpret_cast<void**>(&g_query))) &&
      Hook("Transform", game::kTransform, reinterpret_cast<void*>(&TransformDetour),
           reinterpret_cast<void**>(&g_transform)) &&
      ((!g_config.ps1HeadBob && !g_config.ps1DamageView && !g_config.ps1View && !g_config.ps1PlayerDeath) ||
       Hook("SetView", game::kSetView, reinterpret_cast<void*>(&SetViewDetour),
            reinterpret_cast<void**>(&g_setView))) &&
      (!g_config.ps1HeadBob || RemovePcHeadBob()) && (!g_config.ps1View || SetPs1Fov());
  zombie::WorldBridgeReady(ready && g_query && g_config.ps1ZombieWorld);
  return ready;
}

}  // namespace sf::features
