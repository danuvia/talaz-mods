// The mods that reach into the game itself, which are not on Windows yet. They do
// nothing here until each gets its Windows hooks; the menu, HUDs, Toggle Sprint
// and FPS Limit work as they are.

#include "shared.h"

namespace mods {

float last_fov = 70, player_rise = 0;

void install() {}
bool paper_doll() { return true; }
void world_new_frame() {}
void zoom_tick(float) {}
bool zoom_scroll(double, bool) { return false; }
void zoom_key(int, bool, bool) {}
void nightvision_tick() {}
void riptide_apply(void*) {}

}  // namespace mods
