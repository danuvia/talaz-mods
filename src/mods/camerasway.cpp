// Camera Sway, after Mirsario's Camera Overhaul (GPL-3.0), which OneClient ships:
// turning the camera rolls it a little, the roll building up while it turns and
// fading by itself; starting to strafe gives it a short nudge; a jump tilts it
// with the player's vertical speed. The finished world is turned and shifted,
// which at these angles looks like turning the camera. The hand and HUD stay put
// ponytail: the jump is simulated from the key with Minecraft's jump speed and gravity, so a jump in water or in the air tilts too. Read the player's velocity once the mod finds it

#include <algorithm>
#include <cmath>

#include "config.h"
#include "shared.h"

namespace mods {
namespace {

float sway_roll = 0, sway_pitch = 0;  // degrees, what is drawn
float roll_build = 0;                 // -1 to 1, Camera Overhaul's turning roll target
float jump_speed = 0, jump_height = 0, jump_tick = 0;  // blocks per tick, blocks, seconds
bool strafed = false, jump_queued = false;

// Camera Overhaul's damp: move toward the target by 1 - smoothing^(2 dt)
float damp(float from, float to, float smoothing, float dt) {
    return from + (to - from) * (1 - std::pow(smoothing * smoothing, dt));
}

}  // namespace

bool camerasway_active() {
    return enabled("camerasway") && (std::abs(sway_roll) > 0.01f || std::abs(sway_pitch) > 0.01f);
}

void camerasway_tick(float dt, bool jumped) {
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

    // the jump: Minecraft's 0.42 blocks a tick up, gravity 0.08 a tick and drag 0.98
    // a press counts on the ground or on the way down, the game taking it as soon as
    // the player lands; held, the game jumps again on every landing, and so does this
    if (jumped && on && (jump_height <= 0 || jump_speed < 0))
        jump_queued = true;
    if (jump_height <= 0 && on && (jump_queued || keys[4]))
        jump_speed = 0.42f, jump_height = 0.0001f, jump_queued = false, jump_tick = 0;
    for (jump_tick += dt; jump_height > 0 && jump_tick >= 0.05f; jump_tick -= 0.05f) {
        jump_height += jump_speed;
        jump_speed = (jump_speed - 0.08f) * 0.98f;
        // landing with the key held or a press waiting goes straight into the next jump
        if (jump_height <= 0)
            jump_height = 0, jump_speed = 0;
        if (jump_height <= 0 && on && (jump_queued || keys[4]))
            jump_speed = 0.42f, jump_height = 0.0001f, jump_queued = false;
    }
    if (jump_height <= 0)
        jump_tick = 0;
    float target = on ? jump_speed * config::get("camerasway.jump") : 0;
    if (std::abs(target) < 0.4f)
        target = 0;
    // followed over about a fifth of a second, so a jump started on the way down
    // swings from looking down to looking up instead of snapping
    sway_pitch = damp(sway_pitch, target, 0.04f, dt);
}

void camerasway_world(GLint vp[4]) {
    static GLuint program = 0, vao = 0, frame = 0;
    if (!program) {
        program = fullscreen_program(
                "#version 300 es\nprecision highp float; uniform sampler2D frame; uniform vec2 turn; uniform vec2 shift;"
                " uniform float aspect; uniform float grow; in vec2 uv; out vec4 color;\n"
                "void main() { vec2 p = (uv - 0.5) * vec2(aspect, 1.0) / grow;"
                " p = vec2(p.x * turn.x - p.y * turn.y, p.x * turn.y + p.y * turn.x);"
                " color = vec4(texture(frame, p / vec2(aspect, 1.0) + 0.5 + shift).rgb, 1); }");
        glGenVertexArrays(1, &vao);
        frame = frame_texture(GL_LINEAR);
    }
    float aspect = float(vp[2]) / float(vp[3]), roll = sway_roll * 3.14159265f / 180;
    // a pitch of a degrees moves the picture by tan(a) over tan(fov / 2) of half its height
    float fov = last_fov > 30 && last_fov < 150 ? last_fov : 70;
    // a positive pitch looks up, so the picture slides down
    float shift = std::clamp(0.5f * std::tan(sway_pitch * 3.14159265f / 180) / std::tan(fov * 3.14159265f / 360), -0.03f, 0.03f);
    // not grown: at these angles the strip left at the edges is a few pixels, filled
    // from the edge itself, and growing the picture showed as a zoom
    float grow = 1;
    glBindTexture(GL_TEXTURE_2D, frame);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, vp[0], vp[1], vp[2], vp[3], 0);
    glDisable(GL_BLEND);
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "frame"), 0);
    glUniform2f(glGetUniformLocation(program, "turn"), std::cos(roll), std::sin(roll));
    glUniform2f(glGetUniformLocation(program, "shift"), 0, shift);
    glUniform1f(glGetUniformLocation(program, "aspect"), aspect);
    glUniform1f(glGetUniformLocation(program, "grow"), grow);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

}  // namespace mods
