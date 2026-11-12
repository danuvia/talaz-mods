#pragma once

// What the mods share with each other and with the core: the input they watch,
// the tools for finding and hooking things in the game's library, and the hooks
// each mod in src/mods/ gives the core.

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "imgui.h"

namespace mods {

using Clock = std::chrono::steady_clock;

// Key actions and codes as the launcher reports them, which follow Windows' virtual keys
constexpr int KEY_PRESS = 0, KEY_RELEASE = 2, BUTTON_PRESS = 0;

// --- input -------------------------------------------------------------------

extern std::atomic<bool> keys[5];     // W A S D Space
extern std::atomic<bool> buttons[2];  // left, right
// How far the camera turned in the last frame, in screen pixels, from the game's
// mouse motion while it holds the mouse: raw, and scaled to a 60th of a second
extern float turned_px, look_vx, look_vy;

bool enabled(const char* id);

// --- each mod's hooks, called by the core ------------------------------------

// zoom.cpp: the camera's field of view
void zoom_tick(float dt);
bool zoom_scroll(double dy, bool playing);
void zoom_key(int key, bool down, bool playing);
extern bool ab_test;  // flipped by TALAZ_AB for comparisons
extern float player_rise;  // blocks the player rose in the last game tick
extern float last_fov;  // the field of view the world was last drawn with

// nightvision.cpp
void nightvision_tick();

// togglesprint.cpp: true when the game should not see the key
bool togglesprint_key(int key, bool down, bool playing);

// fpslimit.cpp
void fpslimit_wait();

// riptide.cpp
void riptide_apply(void* window);

// The world pass (android/world.cpp) runs at the game's depth clear, between the world
// and the hand: each of these turns the finished world, in that order
void camerasway_tick(float dt, bool jumped);
bool camerasway_active();
void motionblur_tick(float dt);
bool motionblur_active();
void world_new_frame();

// huds.cpp: the counters and the drawing of every HUD
void huds_tick(float dt);
void huds_click(int index);
bool huds_visible();
void huds_draw(ImDrawList* dl, float u, ImFont* font);

// game.cpp: what the core fixes in the game, and the paper doll setting
bool paper_doll();


}  // namespace mods
