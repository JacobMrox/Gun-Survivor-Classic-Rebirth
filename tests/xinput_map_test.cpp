#include <cstdio>
#include <cstring>

#include "../source/survivor_fix/xinput_logic.h"

namespace {

int fails = 0;

void Check(bool condition, int line, const char* text) {
  if (condition) return;
  std::printf("FAIL %d %s\n", line, text);
  ++fails;
}

#define CHECK(x) Check((x), __LINE__, #x)

unsigned Bit(sf::pad::Source source) { return 1u << static_cast<unsigned>(source); }
unsigned ActionBit(sf::pad::Action action) { return 1u << static_cast<unsigned>(action); }

}  // namespace

int main() {
  // ---- sources and dead zone ---------------------------------------------------------------
  const sf::pad::PadState idle;
  CHECK(sf::pad::SourceMask(idle, 0.5f) == 0);

  sf::pad::PadState stick;
  stick.leftX = 0.4f;
  CHECK(sf::pad::SourceMask(stick, 0.5f) == 0);
  stick.leftX = 0.5f;
  CHECK(sf::pad::SourceMask(stick, 0.5f) == Bit(sf::pad::Source::StickRight));
  stick.leftX = -0.9f;
  CHECK(sf::pad::SourceMask(stick, 0.5f) == Bit(sf::pad::Source::StickLeft));

  // XInput reports up as positive: pushing the stick forward must select the game's Up key,
  // never Down (the first build had this reversed).
  sf::pad::PadState forward;
  forward.leftY = 0.7f;
  CHECK(sf::pad::SourceMask(forward, 0.5f) == Bit(sf::pad::Source::StickUp));
  sf::pad::PadState back;
  back.leftY = -0.7f;
  CHECK(sf::pad::SourceMask(back, 0.5f) == Bit(sf::pad::Source::StickDown));

  sf::pad::PadState resting;
  resting.leftX = 0.0f;
  CHECK(sf::pad::SourceMask(resting, 0.0f) == 0);

  // D-pad edges and diagonals, independent of the stick.
  sf::pad::PadState dpad;
  dpad.buttons = sf::pad::padbutton::DpadLeft | sf::pad::padbutton::DpadDown;
  const uint32_t diagonal = sf::pad::SourceMask(dpad, 0.5f);
  CHECK((diagonal & Bit(sf::pad::Source::DpadLeft)) != 0);
  CHECK((diagonal & Bit(sf::pad::Source::DpadDown)) != 0);
  CHECK((diagonal & Bit(sf::pad::Source::DpadRight)) == 0);

  // Triggers need half travel.
  sf::pad::PadState trigger;
  trigger.rightTrigger = 127;
  CHECK(sf::pad::SourceMask(trigger, 0.5f) == 0);
  trigger.rightTrigger = 128;
  CHECK(sf::pad::SourceMask(trigger, 0.5f) == Bit(sf::pad::Source::RightTrigger));
  trigger.rightTrigger = 0;
  trigger.leftTrigger = 200;
  CHECK(sf::pad::SourceMask(trigger, 0.5f) == Bit(sf::pad::Source::LeftTrigger));

  // ---- default bindings --------------------------------------------------------------------
  sf::pad::Binding defaults[sf::pad::kSourceCount];
  sf::pad::DefaultBindings(defaults, sf::pad::kSourceCount);
  CHECK(defaults[static_cast<unsigned>(sf::pad::Source::A)].key == sf::pad::dik::Return);
  CHECK(defaults[static_cast<unsigned>(sf::pad::Source::B)].key == sf::pad::dik::Escape);
  CHECK(defaults[static_cast<unsigned>(sf::pad::Source::X)].key == sf::pad::dik::Space);
  CHECK(defaults[static_cast<unsigned>(sf::pad::Source::Y)].key == sf::pad::dik::Tab);
  CHECK(defaults[static_cast<unsigned>(sf::pad::Source::StickUp)].key == sf::pad::dik::Up);
  CHECK(defaults[static_cast<unsigned>(sf::pad::Source::StickDown)].key == sf::pad::dik::Down);
  CHECK(defaults[static_cast<unsigned>(sf::pad::Source::LeftThumb)].action ==
        static_cast<uint8_t>(sf::pad::Action::DebugMenu));
  CHECK(defaults[static_cast<unsigned>(sf::pad::Source::RightThumb)].action ==
        static_cast<uint8_t>(sf::pad::Action::QuickSave));
  // Start and Back ship unbound: no second exit button, no duplicate confirm.
  CHECK(defaults[static_cast<unsigned>(sf::pad::Source::Start)].key == 0);
  CHECK(defaults[static_cast<unsigned>(sf::pad::Source::Start)].action == 0);
  CHECK(defaults[static_cast<unsigned>(sf::pad::Source::Back)].key == 0);
  CHECK(defaults[static_cast<unsigned>(sf::pad::Source::Back)].action == 0);

  // No source carries both a key and an action, and no two face or shoulder buttons send the
  // same key. The d-pad and the left stick are the intended aliases of the movement keys.
  for (unsigned i = 0; i < sf::pad::kSourceCount; ++i) CHECK(!(defaults[i].key && defaults[i].action));
  const sf::pad::Source distinct[] = {sf::pad::Source::A,        sf::pad::Source::B,
                                      sf::pad::Source::X,        sf::pad::Source::Y,
                                      sf::pad::Source::LeftShoulder, sf::pad::Source::RightShoulder,
                                      sf::pad::Source::LeftTrigger, sf::pad::Source::RightTrigger};
  for (unsigned i = 0; i < sizeof(distinct) / sizeof(distinct[0]); ++i) {
    const sf::pad::Binding& binding = defaults[static_cast<unsigned>(distinct[i])];
    CHECK(binding.key != 0);
    for (unsigned j = 0; j < i; ++j) {
      CHECK(defaults[static_cast<unsigned>(distinct[j])].key != binding.key);
    }
  }
  CHECK(defaults[static_cast<unsigned>(sf::pad::Source::DpadUp)].key ==
        defaults[static_cast<unsigned>(sf::pad::Source::StickUp)].key);
  CHECK(defaults[static_cast<unsigned>(sf::pad::Source::DpadLeft)].key ==
        defaults[static_cast<unsigned>(sf::pad::Source::StickLeft)].key);
  CHECK(std::strcmp(sf::pad::SourceName(sf::pad::Source::LeftShoulder), "LB") == 0);

  // ---- names and parsing -------------------------------------------------------------------
  bool ok = false;
  sf::pad::Source source = sf::pad::Source::Count;
  ok = sf::pad::SourceFromName("stickright", &source);
  CHECK(ok && source == sf::pad::Source::StickRight);
  CHECK(!sf::pad::SourceFromName("Turbo", &source));

  sf::pad::Binding binding;
  CHECK(sf::pad::ParseBinding("Return", &binding) && binding.key == sf::pad::dik::Return && binding.action == 0);
  CHECK(sf::pad::ParseBinding(" leftshift ", &binding) && binding.key == sf::pad::dik::LeftShift);
  CHECK(sf::pad::ParseBinding("F5", &binding) && binding.key == 0x3F);
  CHECK(sf::pad::ParseBinding("0x39", &binding) && binding.key == sf::pad::dik::Space);
  CHECK(sf::pad::ParseBinding("203", &binding) && binding.key == sf::pad::dik::Left);
  CHECK(sf::pad::ParseBinding("DebugMenu", &binding) &&
        binding.action == static_cast<uint8_t>(sf::pad::Action::DebugMenu) && binding.key == 0);
  CHECK(sf::pad::ParseBinding("none", &binding) && binding.key == 0 && binding.action == 0);
  CHECK(sf::pad::ParseBinding("-", &binding) && binding.key == 0 && binding.action == 0);
  CHECK(sf::pad::ParseBinding("", &binding) && binding.key == 0 && binding.action == 0);
  CHECK(!sf::pad::ParseBinding("Teleport", &binding));
  CHECK(!sf::pad::ParseBinding("0x1FF", &binding));
  CHECK(std::strcmp(sf::pad::DikName(sf::pad::dik::Return), "Return") == 0);
  CHECK(sf::pad::DikName(0xFE) == nullptr);

  // ---- overlay ----------------------------------------------------------------------------
  uint8_t keys[256] = {};
  keys[sf::pad::dik::Up] = 0x80;  // keyboard already holds Up
  sf::pad::PadState press;
  press.buttons = sf::pad::padbutton::A | sf::pad::padbutton::DpadRight;
  const unsigned written = sf::pad::OverlayBoundKeys(press, defaults, sf::pad::kSourceCount, 0.5f, keys, sizeof(keys));
  CHECK(written == 2);
  CHECK(keys[sf::pad::dik::Return] == 0x80);
  CHECK(keys[sf::pad::dik::Right] == 0x80);
  CHECK(keys[sf::pad::dik::Up] == 0x80);
  CHECK(keys[sf::pad::dik::Down] == 0);

  // A released pad never clears keyboard state, and short or null buffers are safe.
  uint8_t held[256] = {};
  held[sf::pad::dik::Return] = 0x80;
  CHECK(sf::pad::OverlayBoundKeys(sf::pad::PadState{}, defaults, sf::pad::kSourceCount, 0.5f, held, sizeof(held)) == 0);
  CHECK(held[sf::pad::dik::Return] == 0x80);
  uint8_t small[8] = {};
  CHECK(sf::pad::OverlayBoundKeys(press, defaults, sf::pad::kSourceCount, 0.5f, small, sizeof(small)) == 0);
  CHECK(sf::pad::OverlayBoundKeys(press, defaults, sf::pad::kSourceCount, 0.5f, nullptr, 0) == 0);
  CHECK(sf::pad::OverlayBoundKeys(press, nullptr, 0, 0.5f, keys, sizeof(keys)) == 0);

  // The spare thumb buttons carry the wrapper's own additions instead of a key.
  sf::pad::PadState thumbs;
  thumbs.buttons = sf::pad::padbutton::LeftThumb | sf::pad::padbutton::RightThumb;
  uint8_t thumbKeys[256] = {};
  CHECK(sf::pad::OverlayBoundKeys(thumbs, defaults, sf::pad::kSourceCount, 0.5f, thumbKeys, sizeof(thumbKeys)) == 0);
  const uint32_t actions = sf::pad::ActionMask(thumbs, defaults, sf::pad::kSourceCount, 0.5f);
  CHECK((actions & ActionBit(sf::pad::Action::DebugMenu)) != 0);
  CHECK((actions & ActionBit(sf::pad::Action::QuickSave)) != 0);
  CHECK((actions & ActionBit(sf::pad::Action::QuickLoad)) == 0);

  // A rebound source follows the new binding, and an unbound source does nothing.
  sf::pad::Binding custom[sf::pad::kSourceCount];
  sf::pad::DefaultBindings(custom, sf::pad::kSourceCount);
  CHECK(sf::pad::ParseBinding("Back", &binding) == false);  // Back is a source, not a key
  CHECK(sf::pad::ParseBinding("QuickLoad", &binding));
  custom[static_cast<unsigned>(sf::pad::Source::Start)] = binding;
  custom[static_cast<unsigned>(sf::pad::Source::A)] = sf::pad::Binding{};
  sf::pad::PadState both;
  both.buttons = sf::pad::padbutton::Start | sf::pad::padbutton::A;
  uint8_t customKeys[256] = {};
  CHECK(sf::pad::OverlayBoundKeys(both, custom, sf::pad::kSourceCount, 0.5f, customKeys, sizeof(customKeys)) == 0);
  const uint32_t customActions = sf::pad::ActionMask(both, custom, sf::pad::kSourceCount, 0.5f);
  CHECK(customActions == ActionBit(sf::pad::Action::QuickLoad));

  // ---- crosshair aiming --------------------------------------------------------------------
  CHECK(sf::pad::StickAimDelta(0.0f, 0.25f, 20.0f) == 0.0f);
  CHECK(sf::pad::StickAimDelta(0.25f, 0.25f, 20.0f) == 0.0f);
  CHECK(sf::pad::StickAimDelta(1.0f, 0.25f, 20.0f) == 20.0f);
  CHECK(sf::pad::StickAimDelta(-1.0f, 0.25f, 20.0f) == -20.0f);
  const float half = sf::pad::StickAimDelta(0.625f, 0.25f, 20.0f);  // (0.625-0.25)/0.75 = 0.5
  CHECK(half > 9.9f && half < 10.1f);
  CHECK(sf::pad::StickAimDelta(0.5f, 0.25f, 20.0f) > sf::pad::StickAimDelta(0.3f, 0.25f, 20.0f));

  // ---- debug menu navigation ---------------------------------------------------------------
  CHECK(sf::pad::MenuNavMask(sf::pad::PadState{}, 0.5f) == 0);
  sf::pad::PadState navPad;
  navPad.buttons = sf::pad::padbutton::DpadUp | sf::pad::padbutton::DpadLeft;
  uint32_t nav = sf::pad::MenuNavMask(navPad, 0.5f);
  CHECK(nav == (sf::pad::menunav::Up | sf::pad::menunav::Left));
  navPad.buttons = sf::pad::padbutton::A;
  CHECK(sf::pad::MenuNavMask(navPad, 0.5f) == sf::pad::menunav::Confirm);
  navPad.buttons = sf::pad::padbutton::B;
  CHECK(sf::pad::MenuNavMask(navPad, 0.5f) == sf::pad::menunav::Back);
  // The left stick moves the menu too, and a resting stick never does, even with no dead zone.
  sf::pad::PadState navStick;
  navStick.leftY = 0.9f;
  CHECK(sf::pad::MenuNavMask(navStick, 0.0f) == sf::pad::menunav::Up);
  navStick.leftY = -0.9f;
  CHECK(sf::pad::MenuNavMask(navStick, 0.0f) == sf::pad::menunav::Down);
  navStick.leftY = 0.1f;
  CHECK(sf::pad::MenuNavMask(navStick, 0.0f) == 0);

  if (fails == 0) std::printf("xinput_map: ok\n");
  return fails ? 1 : 0;
}
