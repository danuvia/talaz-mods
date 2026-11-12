// Motion Blur's drawing on Android, in the world pass. How much to blur is worked out
// in mods/motionblur.cpp

#include <algorithm>
#include <cmath>

#include "config.h"
#include "hooks.h"

namespace mods {
namespace {

void camera_blur(GLint vp[4]) {
    static GLuint program = 0, vao = 0, frame = 0;
    float dx, dy;
    if (!motionblur_step(vp[2], vp[3], dx, dy))
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
    copy_frame(frame, vp);
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
        glUniform1f(glGetUniformLocation(program, "keep"), motionblur_keep());
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    copy_frame(last, vp);
    last_w = vp[2];
    last_h = vp[3];
}

}  // namespace

void motionblur_world(GLint vp[4]) {
    if (config::get("motionblur.camera") != 0)
        camera_blur(vp);
    else
        trail(vp);
}

}  // namespace mods
