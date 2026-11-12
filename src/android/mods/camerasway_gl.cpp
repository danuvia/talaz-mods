// Camera Sway's drawing on Android: the finished world turned and shifted with GL.
// The sway itself is worked out in mods/camerasway.cpp

#include <algorithm>
#include <cmath>

#include "hooks.h"

namespace mods {

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
    float aspect = float(vp[2]) / float(vp[3]), roll = camerasway_roll() * 3.14159265f / 180;
    // a pitch of a degrees moves the picture by tan(a) over tan(fov / 2) of half its height
    float fov = last_fov > 30 && last_fov < 150 ? last_fov : 70;
    // a positive pitch looks up, so the picture slides down
    float shift = std::clamp(0.5f * std::tan(camerasway_pitch() * 3.14159265f / 180) / std::tan(fov * 3.14159265f / 360), -0.03f, 0.03f);
    // not grown: at these angles the strip left at the edges is a few pixels, filled
    // from the edge itself, and growing the picture showed as a zoom
    float grow = 1;
    copy_frame(frame, vp);
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
