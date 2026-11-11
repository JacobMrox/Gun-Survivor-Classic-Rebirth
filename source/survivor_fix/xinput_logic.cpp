#include "xinput_logic.h"

#include <cmath>
#include <cstring>

namespace sf::pad {
namespace {

struct NamedCode {
  const char* name;
  uint8_t code;
};

// Names accepted by [Gamepad] entries. Several spellings share a scancode where players
// expect them to (Enter/Return, Shift/LeftShift, PageUp/Prior).
constexpr NamedCode kKeyNames[] = {
    {"Escape", dik::Escape},
    {"Tab", dik::Tab},
    {"Backspace", 0x0E},
    {"Return", dik::Return},
    {"Enter", dik::Return},
    {"LeftControl", dik::LeftControl},
    {"Ctrl", dik::LeftControl},
    {"LeftShift", dik::LeftShift},
    {"Shift", dik::LeftShift},
    {"LeftAlt", dik::LeftAlt},
    {"Alt", dik::LeftAlt},
    {"Space", dik::Space},
    {"RightShift", 0x36},
    {"RightControl", dik::RightControl},
    {"RightAlt", 0xB8},
    {"CapsLock", 0x3A},
    {"Up", dik::Up},
    {"Down", dik::Down},
    {"Left", dik::Left},
    {"Right", dik::Right},
    {"Insert", 0xD2},
    {"Delete", 0xD3},
    {"Home", 0xC7},
    {"End", 0xCF},
    {"PageUp", 0xC9},
    {"PageDown", 0xD1},
    {"Minus", 0x0C},
    {"Equals", 0x0D},
    {"LeftBracket", 0x1A},
    {"RightBracket", 0x1B},
    {"Semicolon", 0x27},
    {"Apostrophe", 0x28},
    {"Grave", 0x29},
    {"Backslash", 0x2B},
    {"Comma", 0x33},
    {"Period", 0x34},
    {"Slash", 0x35},
    {"NumpadMultiply", 0x37},
    {"NumpadSubtract", 0x4A},
    {"NumpadAdd", 0x4E},
    {"NumpadPeriod", 0x53},
    {"NumpadEnter", 0x9C},
    {"NumpadDivide", 0xB5},
    {"Numpad0", 0x52},
    {"Numpad1", 0x4F},
    {"Numpad2", 0x50},
    {"Numpad3", 0x51},
    {"Numpad4", 0x4B},
    {"Numpad5", 0x4C},
    {"Numpad6", 0x4D},
    {"Numpad7", 0x47},
    {"Numpad8", 0x48},
    {"Numpad9", 0x49},
    {"1", 0x02},
    {"2", 0x03},
    {"3", 0x04},
    {"4", 0x05},
    {"5", 0x06},
    {"6", 0x07},
    {"7", 0x08},
    {"8", 0x09},
    {"9", 0x0A},
    {"0", 0x0B},
    {"Q", 0x10},
    {"W", 0x11},
    {"E", 0x12},
    {"R", 0x13},
    {"T", 0x14},
    {"Y", 0x15},
    {"U", 0x16},
    {"I", 0x17},
    {"O", 0x18},
    {"P", 0x19},
    {"A", 0x1E},
    {"S", 0x1F},
    {"D", 0x20},
    {"F", 0x21},
    {"G", 0x22},
    {"H", 0x23},
    {"J", 0x24},
    {"K", 0x25},
    {"L", 0x26},
    {"Z", 0x2C},
    {"X", 0x2D},
    {"C", 0x2E},
    {"V", 0x2F},
    {"B", 0x30},
    {"N", 0x31},
    {"M", 0x32},
    {"F1", 0x3B},
    {"F2", 0x3C},
    {"F3", 0x3D},
    {"F4", 0x3E},
    {"F5", 0x3F},
    {"F6", 0x40},
    {"F7", 0x41},
    {"F8", 0x42},
    {"F9", 0x43},
    {"F10", 0x44},
    {"F11", 0x57},
    {"F12", 0x58},
};

bool EqualsIgnoreCase(const char* a, const char* b) {
  if (!a || !b) return false;
  while (*a && *b) {
    char ca = *a++;
    char cb = *b++;
    if (ca >= 'a' && ca <= 'z') ca = static_cast<char>(ca - 'a' + 'A');
    if (cb >= 'a' && cb <= 'z') cb = static_cast<char>(cb - 'a' + 'A');
    if (ca != cb) return false;
  }
  return *a == '\0' && *b == '\0';
}

// Bare digits are decimal scancodes, "0x" prefixed are hex, so a key the table does not name
// can still be bound.
bool ParseCode(const char* text, uint8_t* out) {
  if (!text || !*text) return false;
  int base = 10;
  if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
    base = 16;
    text += 2;
    if (!*text) return false;
  }
  unsigned value = 0;
  for (const char* p = text; *p; ++p) {
    unsigned digit;
    if (*p >= '0' && *p <= '9') digit = static_cast<unsigned>(*p - '0');
    else if (base == 16 && *p >= 'a' && *p <= 'f') digit = static_cast<unsigned>(*p - 'a' + 10);
    else if (base == 16 && *p >= 'A' && *p <= 'F') digit = static_cast<unsigned>(*p - 'A' + 10);
    else return false;
    value = value * static_cast<unsigned>(base) + digit;
    if (value > 255) return false;
  }
  *out = static_cast<uint8_t>(value);
  return true;
}

void ClearBinding(Binding* out) { *out = Binding{}; }

void SetKey(Binding* out, uint8_t key) { out->key = key; out->action = 0; }

void SetAction(Binding* out, Action action) { out->key = 0; out->action = static_cast<uint8_t>(action); }

}  // namespace

float StickAimDelta(float axis, float deadZone, float speed) {
  if (axis > 1.0f) axis = 1.0f;
  if (axis < -1.0f) axis = -1.0f;
  if (deadZone < 0.0f) deadZone = 0.0f;
  if (deadZone > 0.95f) deadZone = 0.95f;
  const float magnitude = std::fabs(axis);
  if (magnitude <= deadZone) return 0.0f;
  const float scaled = (magnitude - deadZone) / (1.0f - deadZone);
  return (axis < 0.0f ? -scaled : scaled) * speed;
}

uint32_t SourceMask(const PadState& pad, float deadZone) {
  const uint16_t b = pad.buttons;
  const bool horizontal = AxisBeyondDeadZone(pad.leftX, deadZone);
  const bool vertical = AxisBeyondDeadZone(pad.leftY, deadZone);
  uint32_t mask = 0;
  auto set = [&mask](Source source) { mask |= 1u << static_cast<unsigned>(source); };

  if (b & padbutton::DpadUp) set(Source::DpadUp);
  if (b & padbutton::DpadDown) set(Source::DpadDown);
  if (b & padbutton::DpadLeft) set(Source::DpadLeft);
  if (b & padbutton::DpadRight) set(Source::DpadRight);
  if (b & padbutton::A) set(Source::A);
  if (b & padbutton::B) set(Source::B);
  if (b & padbutton::X) set(Source::X);
  if (b & padbutton::Y) set(Source::Y);
  if (b & padbutton::LeftShoulder) set(Source::LeftShoulder);
  if (b & padbutton::RightShoulder) set(Source::RightShoulder);
  if (pad.leftTrigger >= 128) set(Source::LeftTrigger);
  if (pad.rightTrigger >= 128) set(Source::RightTrigger);
  if (b & padbutton::Start) set(Source::Start);
  if (b & padbutton::Back) set(Source::Back);
  if (b & padbutton::LeftThumb) set(Source::LeftThumb);
  if (b & padbutton::RightThumb) set(Source::RightThumb);
  // XInput reports up as positive; the stick drives the game's Up key when pushed up.
  if (vertical && pad.leftY > 0.0f) set(Source::StickUp);
  if (vertical && pad.leftY < 0.0f) set(Source::StickDown);
  if (horizontal && pad.leftX < 0.0f) set(Source::StickLeft);
  if (horizontal && pad.leftX > 0.0f) set(Source::StickRight);
  return mask;
}

const char* SourceName(Source source) {
  switch (source) {
    case Source::DpadUp: return "DpadUp";
    case Source::DpadDown: return "DpadDown";
    case Source::DpadLeft: return "DpadLeft";
    case Source::DpadRight: return "DpadRight";
    case Source::A: return "A";
    case Source::B: return "B";
    case Source::X: return "X";
    case Source::Y: return "Y";
    case Source::LeftShoulder: return "LB";
    case Source::RightShoulder: return "RB";
    case Source::LeftTrigger: return "LT";
    case Source::RightTrigger: return "RT";
    case Source::Start: return "Start";
    case Source::Back: return "Back";
    case Source::LeftThumb: return "L3";
    case Source::RightThumb: return "R3";
    case Source::StickUp: return "StickUp";
    case Source::StickDown: return "StickDown";
    case Source::StickLeft: return "StickLeft";
    case Source::StickRight: return "StickRight";
    case Source::Count: break;
  }
  return "";
}

bool SourceFromName(const char* name, Source* out) {
  if (!name || !out) return false;
  for (unsigned i = 0; i < kSourceCount; ++i) {
    const Source source = static_cast<Source>(i);
    if (EqualsIgnoreCase(name, SourceName(source))) {
      *out = source;
      return true;
    }
  }
  return false;
}

const char* ActionName(Action action) {
  switch (action) {
    case Action::DebugMenu: return "DebugMenu";
    case Action::QuickSave: return "QuickSave";
    case Action::QuickLoad: return "QuickLoad";
    case Action::Fullscreen: return "Fullscreen";
    case Action::None: return "None";
    case Action::Count: break;
  }
  return "None";
}

bool ActionFromName(const char* name, Action* out) {
  if (!name || !out) return false;
  for (unsigned i = 0; i < static_cast<unsigned>(Action::Count); ++i) {
    const Action action = static_cast<Action>(i);
    if (EqualsIgnoreCase(name, ActionName(action))) {
      *out = action;
      return true;
    }
  }
  return false;
}

bool DikFromName(const char* name, uint8_t* out) {
  if (!name || !out) return false;
  for (const NamedCode& entry : kKeyNames) {
    if (EqualsIgnoreCase(name, entry.name)) {
      *out = entry.code;
      return true;
    }
  }
  return ParseCode(name, out);
}

const char* DikName(uint8_t code) {
  if (code == 0) return "None";
  for (const NamedCode& entry : kKeyNames) {
    if (entry.code == code) return entry.name;
  }
  return nullptr;
}

Binding DefaultBinding(Source source) {
  Binding binding;
  switch (source) {
    // Movement: the game's arrow keys, from the d-pad or the left stick.
    case Source::DpadUp:
    case Source::StickUp: SetKey(&binding, dik::Up); break;
    case Source::DpadDown:
    case Source::StickDown: SetKey(&binding, dik::Down); break;
    case Source::DpadLeft:
    case Source::StickLeft: SetKey(&binding, dik::Left); break;
    case Source::DpadRight:
    case Source::StickRight: SetKey(&binding, dik::Right); break;
    // Face buttons: the game's own action, confirm, shoot and menu keys, one each.
    case Source::A: SetKey(&binding, dik::Return); break;
    case Source::B: SetKey(&binding, dik::Escape); break;
    case Source::X: SetKey(&binding, dik::Space); break;
    case Source::Y: SetKey(&binding, dik::Tab); break;
    case Source::LeftShoulder: SetKey(&binding, dik::LeftShift); break;
    case Source::RightShoulder: SetKey(&binding, dik::LeftControl); break;
    case Source::LeftTrigger: SetKey(&binding, dik::LeftAlt); break;
    case Source::RightTrigger: SetKey(&binding, dik::RightControl); break;
    // Spare buttons: the wrapper's own additions. Nothing here duplicates a game key.
    case Source::LeftThumb: SetAction(&binding, Action::DebugMenu); break;
    case Source::RightThumb: SetAction(&binding, Action::QuickSave); break;
    case Source::Start:
    case Source::Back:
    case Source::Count: ClearBinding(&binding); break;
  }
  return binding;
}

void DefaultBindings(Binding* out, unsigned count) {
  if (!out) return;
  for (unsigned i = 0; i < count && i < kSourceCount; ++i) {
    out[i] = DefaultBinding(static_cast<Source>(i));
  }
}

bool ParseBinding(const char* text, Binding* out) {
  if (!out) return false;
  ClearBinding(out);
  if (!text) return true;
  while (*text == ' ' || *text == '\t') ++text;
  size_t length = std::strlen(text);
  while (length && (text[length - 1] == ' ' || text[length - 1] == '\t')) --length;
  if (length == 0) return true;
  char name[64];
  if (length >= sizeof(name)) return false;
  std::memcpy(name, text, length);
  name[length] = '\0';
  if (std::strcmp(name, "-") == 0) return true;
  Action action;
  if (ActionFromName(name, &action)) {
    if (action != Action::None) SetAction(out, action);
    return true;
  }
  uint8_t key;
  if (DikFromName(name, &key) && key != 0) {
    SetKey(out, key);
    return true;
  }
  return false;
}

unsigned OverlayBoundKeys(const PadState& pad, const Binding* bindings, unsigned count, float deadZone,
                          uint8_t* keyboardState, unsigned stateSize) {
  if (!bindings || !keyboardState) return 0;
  const uint32_t mask = SourceMask(pad, deadZone);
  unsigned written = 0;
  for (unsigned i = 0; i < count && i < kSourceCount; ++i) {
    if ((mask & (1u << i)) == 0) continue;
    const uint8_t code = bindings[i].key;
    if (code == 0 || code >= stateSize) continue;
    keyboardState[code] = 0x80;
    ++written;
  }
  return written;
}

uint32_t ActionMask(const PadState& pad, const Binding* bindings, unsigned count, float deadZone) {
  if (!bindings) return 0;
  const uint32_t mask = SourceMask(pad, deadZone);
  uint32_t actions = 0;
  for (unsigned i = 0; i < count && i < kSourceCount; ++i) {
    if ((mask & (1u << i)) == 0) continue;
    const uint8_t action = bindings[i].action;
    if (action == 0 || action >= static_cast<uint8_t>(Action::Count)) continue;
    actions |= 1u << action;
  }
  return actions;
}

}  // namespace sf::pad
