#pragma once

// What the Android side shares: the tools for finding and hooking things in the
// game's library, and the GL pieces the world pass and its mods draw with.

#include <cstddef>
#include <vector>

#include "gl.h"
#include "shared.h"

namespace mods {

// mcpelauncher's way to write into the game's code and tables
using Patch = void* (*)(void*, void*, size_t);

// --- the game's library --------------------------------------------------------

struct Segment {
    std::byte* begin;
    size_t size;
    bool exec;
};
std::vector<Segment> segments_of(void* lib);
std::byte* find_string(const std::vector<Segment>& segs, const void* needle, size_t n);
std::byte* find_pointer(const std::vector<Segment>& segs, const void* value);
bool in_code(const std::vector<Segment>& segs, const void* p);
// Points one of the game's imported functions at a replacement. Returns what was there
void* hook_import(const char* name, void* replacement, Patch patch);

void zoom_install(const std::vector<Segment>& segs, Patch patch, int major, int minor, int patch_version);
void nightvision_install(const std::vector<Segment>& segs, Patch patch);
void riptide_install(Patch patch);
void world_install(Patch patch);
void camerasway_world(GLint vp[4]);
void motionblur_world(GLint vp[4]);

// One triangle over the whole screen, drawn with a program of the caller's fragment shader
GLuint fullscreen_program(const char* fragment);
void copy_frame(GLuint texture, GLint vp[4]);
GLuint frame_texture(GLint filter);

}  // namespace mods
