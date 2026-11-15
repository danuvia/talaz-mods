// The mods that reach into the game itself, which are not on Windows yet. They do
// nothing here until each gets its Windows hooks; the menu, HUDs, Toggle Sprint
// and FPS Limit work as they are.

#include "shared.h"

namespace mods {

float last_fov = 70, player_rise = 0;
float player_hunger = -1, player_saturation = -1, player_exhaustion = -1;
// ponytail: no Mouse Tweaks on Windows yet; send the click with SendInput when the Windows mods are being tested
void click_again() {}
void click_once(bool) {}
bool slot_hovered() { return false; }
bool entered_slot() { return false; }
void drop_again() {}
void mousetweaks_unhover() {}
bool clicking() { return false; }

void install() {}
bool paper_doll() { return true; }
void world_new_frame() {}
void zoom_tick(float) {}
bool zoom_scroll(double, bool) { return false; }
void zoom_key(int, bool, bool) {}
void nightvision_tick() {}
void riptide_apply(void*) {}

}  // namespace mods
