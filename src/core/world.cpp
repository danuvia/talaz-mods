// The world pass. The game draws the world straight to the screen, then clears only
// the depth before the hand and the interface. At that clear the mods that change
// the world (Camera Sway, then Motion Blur) work on the finished picture, so the
// hand and the HUDs stay sharp. The game's GL state is put back afterwards.

#include <android/log.h>

#include <atomic>
#include <tuple>
#include <utility>
#include <vector>

#include "mods.h"
#include "shared.h"

namespace mods {

GLuint fullscreen_program(const char* fragment) {
    // one triangle over the whole screen, made from the vertex number alone
    const char* vs = "#version 300 es\nout vec2 uv;\n"
                     "void main() { vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);"
                     " uv = p * 0.5 + 0.5; gl_Position = vec4(p, 0, 1); }";
    GLuint program = glCreateProgram();
    for (auto [type, src] : {std::pair{GL_VERTEX_SHADER, vs}, std::pair{GL_FRAGMENT_SHADER, fragment}}) {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);
        glAttachShader(program, shader);
        glDeleteShader(shader);
    }
    glLinkProgram(program);
    return program;
}

GLuint frame_texture(GLint filter) {
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return texture;
}

// copies the screen into a texture made by frame_texture, allocating it only when the
// size changes: glCopyTexImage2D every frame made the driver allocate a new screen sized
// texture each time
void copy_frame(GLuint texture, GLint vp[4]) {
    static std::vector<std::tuple<GLuint, GLint, GLint>> sizes;
    glBindTexture(GL_TEXTURE_2D, texture);
    for (auto& [t, w, h] : sizes)
        if (t == texture) {
            if (w == vp[2] && h == vp[3]) {
                glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vp[0], vp[1], vp[2], vp[3]);
                return;
            }
            w = vp[2], h = vp[3];
            glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, vp[0], vp[1], vp[2], vp[3], 0);
            return;
        }
    sizes.emplace_back(texture, vp[2], vp[3]);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, vp[0], vp[1], vp[2], vp[3], 0);
}

namespace {

void (*real_clear)(GLbitfield) = nullptr;
std::atomic<bool> passed_this_frame{false};

void world_pass() {
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    if (vp[2] <= 0 || vp[3] <= 0)
        return;
    // the game's state, put back afterwards
    GLint prog, va, tex, active, src_rgb, dst_rgb, src_a, dst_a, eq_rgb, eq_a;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &va);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
    glGetIntegerv(GL_BLEND_SRC_RGB, &src_rgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &dst_rgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &src_a);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &dst_a);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &eq_rgb);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &eq_a);
    GLboolean mask[4];
    glGetBooleanv(GL_COLOR_WRITEMASK, mask);
    bool blend = glIsEnabled(GL_BLEND), depth = glIsEnabled(GL_DEPTH_TEST), cull = glIsEnabled(GL_CULL_FACE),
         scissor = glIsEnabled(GL_SCISSOR_TEST), stencil = glIsEnabled(GL_STENCIL_TEST);

    // what every pass draws over: the whole picture, colour only
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    if (camerasway_active())
        camerasway_world(vp);
    if (motionblur_active())
        motionblur_world(vp);

    glUseProgram(prog);
    glBindVertexArray(va);
    glBindTexture(GL_TEXTURE_2D, tex);
    glActiveTexture(active);
    glBlendFuncSeparate(src_rgb, dst_rgb, src_a, dst_a);
    glBlendEquationSeparate(eq_rgb, eq_a);
    glColorMask(mask[0], mask[1], mask[2], mask[3]);
    (blend ? glEnable : glDisable)(GL_BLEND);
    (depth ? glEnable : glDisable)(GL_DEPTH_TEST);
    (cull ? glEnable : glDisable)(GL_CULL_FACE);
    (scissor ? glEnable : glDisable)(GL_SCISSOR_TEST);
    (stencil ? glEnable : glDisable)(GL_STENCIL_TEST);
}

void clear_hook(GLbitfield bits) {
    // only the screen's own depth clear counts: the paper doll and other pictures
    // are drawn in framebuffers of their own and clear there first
    if (bits == GL_DEPTH_BUFFER_BIT && (motionblur_active() || camerasway_active()) && tc_glDrawArrays && !passed_this_frame) {
        GLint fb = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
        if (fb == 0) {
            passed_this_frame = true;
            world_pass();
        }
    }
    real_clear(bits);
}

}  // namespace

void world_install(Patch patch) {
    real_clear = reinterpret_cast<decltype(real_clear)>(hook_import("glClear", (void*)clear_hook, patch));
    if (!real_clear)
        __android_log_print(ANDROID_LOG_ERROR, CLIENT_NAME, "The world pass couldn't find the game's glClear");
}

// a new frame may pass again
void world_new_frame() {
    passed_this_frame = false;
}

}  // namespace mods
