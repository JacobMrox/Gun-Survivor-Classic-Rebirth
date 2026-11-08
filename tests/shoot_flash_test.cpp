#include "../source/survivor_fix/ps1/screen_fx_logic.h"
#include <cassert>

int main() {
  ps1::ShootFlash flash;
  assert(!flash.Visible());
  flash.Shot(false, 2);
  assert(!flash.Visible());
  flash.Shot(true, 1);
  assert(flash.Visible());
  flash.AfterPresent(1);
  assert(!flash.Visible());
  flash.Shot(true, 2);
  flash.AfterPresent(1);
  assert(flash.Visible());
  flash.Shot(true, 2);
  flash.AfterPresent(1);
  assert(flash.Visible());
  flash.AfterPresent(1);
  assert(!flash.Visible());
}
