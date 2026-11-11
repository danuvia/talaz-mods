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

// ponytail: the turn comes from mouse motion, so turning with a controller isn't blurred. Read the camera's angles once the mod finds them
void camera_blur(GLint vp[4]) {
    static GLuint program = 0, vao = 0, frame = 0;
    // ponytail: mouse counts to screen pixels is a guess (twice), tune once the camera angles are read
    float amount = 2 * std::clamp(config::get("motionblur.amount") / 100, 0.f, 1.f);
    float dx = look_vx * amount / vp[2], dy = -look_vy * amount / vp[3];
    if (std::abs(dx * vp[2]) + std::abs(dy * vp[3]) < 0.5f)
        return;
    if (!program) {
        program = fullscreen_program(
                "#version 300 es\nprecision mediump float; uniform sampler2D frame; uniform vec2 step; uniform int samples; in vec2 uv; out vec4 color;\n"
                "void main() { vec3 sum = vec3(0); for (int i = 0; i < samples; i++) {"
                " float t = float(i) / float(samples - 1) - 0.5; sum += texture(frame, clamp(uv + step * t, 0.0, 1.0)).rgb; }"
                " color = vec4(sum / float(samples), 1); }");
        glGenVertexArrays(1, &vao);
        frame = frame_texture(GL_LINEAR);
    }
    glBindTexture(GL_TEXTURE_2D, frame);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, vp[0], vp[1], vp[2], vp[3], 0);
    glDisable(GL_BLEND);
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "frame"), 0);
    glUniform2f(glGetUniformLocation(program, "step"), dx, dy);
    glUniform1i(glGetUniformLocation(program, "samples"), std::clamp(int(config::get("motionblur.samples")), 2, 64));
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

// the last frame laid over this one, and the result kept for the next
void trail(GLint vp[4]) {
    static GLuint program = 0, vao = 0, last = 0;
    static GLint last_w = 0, last_h = 0;
    if (!program) {
        program = fullscreen_program(
                "#version 300 es\nprecision mediump float; uniform sampler2D last; uniform float keep; in vec2 uv; out vec4 color;\n"
                "void main() { color = vec4(texture(last, uv).rgb, keep); }");
        glGenVertexArrays(1, &vao);
        last = frame_texture(GL_NEAREST);
    }
    glBindTexture(GL_TEXTURE_2D, last);
    // a new size starts the trail over
    if (vp[2] == last_w && vp[3] == last_h) {
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
        glUseProgram(program);
        glUniform1i(glGetUniformLocation(program, "last"), 0);
        glUniform1f(glGetUniformLocation(program, "keep"), blur_keep);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, vp[0], vp[1], vp[2], vp[3], 0);
    last_w = vp[2];
    last_h = vp[3];
}

}  // namespace

void motionblur_tick(float dt) {
    // the last frame keeps amount of itself per 60th of a second, like the Java mods,
    // so the trail is as long at 200 fps as at 60
    blur_keep = enabled("motionblur") ? std::pow(std::clamp(config::get("motionblur.amount") / 100, 0.f, 0.95f), dt * 60) : 0;
}

bool motionblur_active() {
    return blur_keep > 0;
}

void motionblur_world(GLint vp[4]) {
    if (config::get("motionblur.camera") != 0)
        camera_blur(vp);
    else
        trail(vp);
}

}  // namespace mods
