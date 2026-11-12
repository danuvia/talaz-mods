// Camera Sway, after Mirsario's Camera Overhaul (GPL-3.0), which OneClient ships:
// turning the camera rolls it a little, the roll building up while it turns and
// fading by itself; starting to strafe gives it a short nudge; a jump tilts it
// with the player's vertical speed. The finished world is turned and shifted,
// which at these angles looks like turning the camera. The hand and HUD stay put

#include <algorithm>
#include <cmath>

#include "config.h"
#include "shared.h"

namespace mods {
namespace {

float sway_roll = 0, sway_pitch = 0;  // degrees, what is drawn
float roll_build = 0;                 // -1 to 1, Camera Overhaul's turning roll target
bool strafed = false;

// Camera Overhaul's damp: move toward the target by 1 - smoothing^(2 dt)
float damp(float from, float to, float smoothing, float dt) {
    return from + (to - from) * (1 - std::pow(smoothing * smoothing, dt));
}

}  // namespace

bool camerasway_active() {
    return enabled("camerasway") && (std::abs(sway_roll) > 0.01f || std::abs(sway_pitch) > 0.01f);
}

void camerasway_tick(float dt, bool) {
    bool on = enabled("camerasway");
    // turning roll: the turn adds up, then decays
    // ponytail: a pixel of mouse motion is taken as 0.15 degrees of turn
    roll_build = damp(roll_build, 0, 0.0825f, dt);
    roll_build = std::clamp(roll_build + (on ? -turned_px * 0.15f * 0.0048f : 0), -1.f, 1.f);
    // starting to strafe nudges it the same way
    bool strafing = keys[1] != keys[3];
    if (on && strafing && !strafed)
        roll_build = std::clamp(roll_build + (keys[3] ? -0.35f : 0.35f), -1.f, 1.f);
    strafed = strafing;
    float x = std::abs(roll_build), eased = x < 0.5f ? 4 * x * x * x : 1 - std::pow(-2 * x + 2, 3.f) / 2;
    // eased once more on the way to the screen, so the tilt glides rather than follows each mouse jolt
    sway_roll = damp(sway_roll, on ? eased * config::get("camerasway.tilt") * (roll_build < 0 ? -1.f : 1.f) : 0, 0.1f, dt);

    // the jump: the player's real rise each tick, so falling, water and ladders count as they move
    float target = on ? std::clamp(player_rise, -0.42f, 0.42f) * config::get("camerasway.jump") : 0;
    if (std::abs(target) < 0.4f)
        target = 0;
    // followed over about a fifth of a second, so a jump started on the way down
    // swings from looking down to looking up instead of snapping
    // and settled faster on landing, when the target drops back to level
    sway_pitch = damp(sway_pitch, target, std::abs(target) < std::abs(sway_pitch) ? 0.008f : 0.04f, dt);
}

float camerasway_roll() { return sway_roll; }
float camerasway_pitch() { return sway_pitch; }

}  // namespace mods
