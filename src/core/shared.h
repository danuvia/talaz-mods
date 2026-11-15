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
extern float player_hunger, player_saturation, player_exhaustion;  // -1 when the game's version hides them
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
float camerasway_roll();   // degrees
float camerasway_pitch();  // degrees, up is positive
void motionblur_tick(float dt);
bool motionblur_active();
float motionblur_keep();
bool motionblur_step(int width, int height, float& dx, float& dy);
void world_new_frame();

// huds.cpp: the counters and the drawing of every HUD
void huds_tick(float dt);
void huds_click(int index);
bool huds_visible();
void huds_draw(ImDrawList* dl, float u, ImFont* font);

// mousetweaks.cpp, and what each platform gives it: whether the mouse is over a slot of
// the player's or a container's items, whether it came onto one since last asked, a left
// click sent to the game (with Shift held for it if asked), the held left button or the
// held drop key let go and pressed again, and whether one of those is being sent right now
void mousetweaks_key(int key, bool down);
void mousetweaks_button(int index, bool down);
bool mousetweaks_scroll(bool playing);
void mousetweaks_cursor(float x, float y);
void mousetweaks_unhover();
bool slot_hovered();
bool entered_slot();
void drop_again();
void click_once(bool hold_shift);
void click_again();
bool clicking();

// game.cpp: what the core fixes in the game, and the paper doll setting
bool paper_doll();


}  // namespace mods
