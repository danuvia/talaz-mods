// Motion Blur, in the world pass. Following the camera (PolyBlur's Unity mode) the
// world is smeared along the way the camera turns and is sharp when it holds still;
// otherwise each frame keeps some of the last one, a trail (PolyBlur's Phosphor).

#include <algorithm>
#include <cmath>

#include "config.h"
#include "shared.h"

namespace mods {
namespace {

float blur_keep = 0;  // how much of the last frame stays, set each frame from the amount and frame time

}  // namespace

void motionblur_tick(float dt) {
    // the last frame keeps amount of itself per 60th of a second, like the Java mods,
    // so the trail is as long at 200 fps as at 60
    blur_keep = enabled("motionblur") ? std::pow(std::clamp(config::get("motionblur.amount") / 100, 0.f, 0.95f), dt * 60) : 0;
}

bool motionblur_active() {
    return blur_keep > 0;
}

float motionblur_keep() { return blur_keep; }

// How far the camera blur smears this frame, as a part of the picture's width and height.
// False when the camera barely moved and nothing needs smearing
// ponytail: the turn comes from mouse motion, so turning with a controller isn't blurred, and mouse counts to screen pixels is a guess (twice)
bool motionblur_step(int width, int height, float& dx, float& dy) {
    float amount = 2 * std::clamp(config::get("motionblur.amount") / 100, 0.f, 1.f);
    dx = look_vx * amount / width;
    dy = -look_vy * amount / height;
    return std::abs(dx * width) + std::abs(dy * height) >= 0.5f;
}

}  // namespace mods
