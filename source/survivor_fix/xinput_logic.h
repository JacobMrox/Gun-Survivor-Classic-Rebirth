// Maps an XInput pad onto the DirectInput keyboard the game already reads.
// Pure logic: no Windows or XInput headers, so the tests can exercise it directly.
//
// Every physical button and stick direction is a Source, and every Source carries its own
// Binding. A Binding is either a DirectInput scancode - a key the game already polls for its
// own actions - or one of the wrapper's actions (debug menu, quick save/load, window toggle),
// which the pad layer runs itself because they live outside the game's input code. That is
// how the spare pad buttons get functions the original configuration has no key for.
#pragma once

#include <cstdint>

namespace sf::pad {

// DirectInput keyboard scancodes (DIK_*) for the keys the pad can drive.
namespace dik {
constexpr uint8_t Escape = 0x01;
constexpr uint8_t Tab = 0x0F;
constexpr uint8_t Return = 0x1C;
constexpr uint8_t LeftControl = 0x1D;
constexpr uint8_t LeftShift = 0x2A;
constexpr uint8_t LeftAlt = 0x38;
constexpr uint8_t Space = 0x39;
constexpr uint8_t Up = 0xC8;
constexpr uint8_t Down = 0xD0;
constexpr uint8_t Left = 0xCB;
constexpr uint8_t Right = 0xCD;
constexpr uint8_t RightControl = 0x9D;
}  // namespace dik

// XInput gamepad button bits (kept local so the header needs no XInput headers).
namespace padbutton {
constexpr uint16_t DpadUp = 0x0001;
constexpr uint16_t DpadDown = 0x0002;
constexpr uint16_t DpadLeft = 0x0004;
constexpr uint16_t DpadRight = 0x0008;
constexpr uint16_t Start = 0x0010;
constexpr uint16_t Back = 0x0020;
constexpr uint16_t LeftThumb = 0x0040;
constexpr uint16_t RightThumb = 0x0080;
constexpr uint16_t LeftShoulder = 0x0100;
constexpr uint16_t RightShoulder = 0x0200;
constexpr uint16_t A = 0x1000;
constexpr uint16_t B = 0x2000;
constexpr uint16_t X = 0x4000;
constexpr uint16_t Y = 0x8000;
}  // namespace padbutton

// Everything that can carry a binding. The left stick is split into its four directions, so
// a direction can be bound independently of the d-pad.
enum class Source : unsigned {
  DpadUp,
  DpadDown,
  DpadLeft,
  DpadRight,
  A,
  B,
  X,
  Y,
  LeftShoulder,
  RightShoulder,
  LeftTrigger,
  RightTrigger,
  Start,
  Back,
  LeftThumb,
  RightThumb,
  StickUp,
  StickDown,
  StickLeft,
  StickRight,
  Count
};

inline constexpr unsigned kSourceCount = static_cast<unsigned>(Source::Count);

// Wrapper features a pad button can run directly, for things the game has no key for.
enum class Action : unsigned {
  None = 0,
  DebugMenu,
  QuickSave,
  QuickLoad,
  Fullscreen,
  Count
};

inline constexpr unsigned kActionCount = static_cast<unsigned>(Action::Count);

// A binding is a key, an action, or nothing. Both fields are 0 when the source is unbound.
struct Binding {
  uint8_t key = 0;     // DirectInput scancode; 0 = this source sends no key
  uint8_t action = 0;  // Action; 0 = this source runs no wrapper action
};

struct PadState {
  uint16_t buttons = 0;
  float leftX = 0.0f;        // -1 (left) .. 1 (right)
  float leftY = 0.0f;        // -1 (down) .. 1 (up), XInput sign convention
  float rightX = 0.0f;
  float rightY = 0.0f;
  uint8_t leftTrigger = 0;   // 0..255
  uint8_t rightTrigger = 0;
};

// A stick direction only registers once it clears the dead zone.
inline bool AxisBeyondDeadZone(float axis, float deadZone) {
  if (deadZone < 0.0f) deadZone = 0.0f;
  if (deadZone > 0.95f) deadZone = 0.95f;
  return axis <= -deadZone || axis >= deadZone;
}

// Pixels the right stick moves the crosshair in one poll: the travel left after the dead zone
// is rescaled to 0..1 so the stick still has full range. Signed; 0 inside the dead zone.
float StickAimDelta(float axis, float deadZone, float speed);

// Which sources are held. Bit i mirrors Source(i).
uint32_t SourceMask(const PadState& pad, float deadZone);

// Configuration names: the [Gamepad] entry a source is read from ("A", "LB", "StickRight"...).
const char* SourceName(Source source);
bool SourceFromName(const char* name, Source* out);
const char* ActionName(Action action);
bool ActionFromName(const char* name, Action* out);
// Key names accepted in the configuration ("Return", "LeftShift", "F5", "Up", "Numpad3"...).
bool DikFromName(const char* name, uint8_t* out);
const char* DikName(uint8_t code);  // nullptr for an unknown scancode

// Shipped bindings: A/B/X/Y and the stick drive the game's keys, the thumb clicks and the
// shoulders carry the wrapper's own additions so no two buttons do the same thing.
Binding DefaultBinding(Source source);
void DefaultBindings(Binding* out, unsigned count);

// "Return", "0x39", "57", "DebugMenu", "None"/"-"/empty (unbound). False for an unknown name.
bool ParseBinding(const char* text, Binding* out);

// Write the keys of the held sources into a DirectInput keyboard state buffer.
// Returns how many keys were set. Keys already held on the keyboard stay set and keys the
// pad is not sending are never cleared, so the keyboard keeps working while a pad is plugged in.
unsigned OverlayBoundKeys(const PadState& pad, const Binding* bindings, unsigned count, float deadZone,
                          uint8_t* keyboardState, unsigned stateSize);

// Bit i set when source i is held and bound to Action i (bit index = Action value).
uint32_t ActionMask(const PadState& pad, const Binding* bindings, unsigned count, float deadZone);

}  // namespace sf::pad
