#include "screen_fx_logic.h"

namespace ps1 {

void FadeOut(Fade& f, int speed) {
  f.state = (speed << 8 | 6) & 0xFFFF;
  f.level = 0;
}

void FadeIn(Fade& f, int speed, bool longHold) {
  f.state = (speed << 8 | 5) & 0xFFFF;
  f.level = longHold ? 0x4FF : 0xFF;
}

void StepFade(Fade& f) {
  const int speed = f.state >> 8;
  if (f.state & 1) {
    f.level -= speed;
    if (f.level <= 0) {
      f.level = 0;
      f.state = 0;
    }
  } else if (f.state & 2) {
    f.level += speed;
    if (f.level > 0xFE) {
      f.level = 0xFF;
      f.state = 4;
    }
  }
}

int FadeDrawLevel(const Fade& f) {
  if (!FadeDrawn(f)) return 0;
  return f.level < 0 ? 0 : (f.level > 0xFF ? 0xFF : f.level);
}

void StartPlayerDeath(PlayerDeath& d, int eye) {
  d = PlayerDeath{};
  d.eye = eye;
}

void RecoverPitch(int& pitch) {
  if (pitch > 0) {
    pitch -= 0x10;
    if (pitch < 0) pitch = 0;
  } else if (pitch < 0) {
    pitch += 0x10;
    if (pitch > 0) pitch = 0;
  }
}

DeathFrame StepPlayerDeath(PlayerDeath& d, Fade& f) {
  DeathFrame out;
  switch (d.step) {
    case 0:
      d.trail = true;
      d.trailColor = 0x28;
      d.subtract = 0;
      d.trailScale = 0x1140;
      d.twistSpeed = 0;
      d.counter = 0x18;
      d.fall = 0x50;
      d.slide = 0xB4;
      ++d.step;
      break;
    case 1:
      if (d.counter != 0) {
        --d.counter;
        d.pitch -= 0x20;
      }
      d.eye += d.fall;
      if (-200 < d.eye) ++d.step;
      out.slide = d.slide;
      d.slide = d.slide - 1 - d.counter;
      break;
    case 2:
      d.counter = 0x5A;
      ++d.step;
      break;
    case 3:
      if (d.counter == 0) {
        d.counter = 0x3C;
        ++d.step;
      } else {
        --d.counter;
      }
      break;
    case 4:
      d.trailColor += 4;
      d.trailScale += 0xE;
      if (0x60 < d.trailColor) {
        d.twist += d.twistSpeed;
        d.twistSpeed += 0x400;
        if (0x2D000 < d.twistSpeed) d.twistSpeed = 0x2D000;
      }
      if (0x7F < d.trailColor) d.trailColor = 0x7F;
      if (d.counter < 0x20) {
        d.subtract = (0x20 - d.counter) * 8;
        if (0xFF < d.subtract) d.subtract = 0xFF;
      }
      if (d.counter == 0) {
        ++d.step;
      } else {
        --d.counter;
      }
      break;
    case 5:
      FadeOut(f, kDeathFadeOutSpeed);
      ++d.step;
      [[fallthrough]];
    case 6:
      if (FadeBlack(f)) {
        d.trail = false;
        ++d.step;
      }
      break;
    default:
      break;
  }
  // 8008369C: if (800A28CC < 7) 800825E4(), after the step.
  out.redFlash = d.step < 7;
  return out;
}

}  // namespace ps1
