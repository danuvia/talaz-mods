#pragma once

#include <cstdint>

struct ImDrawData;

// Every input handler returns true when the menu swallowed the event, so the
// game never sees clicks and keys meant for the menu.
namespace menu {

// What the menu needs from the launcher: whether the game holds the mouse,
// which is when a world is being played with no screen open, and the height of
// the launcher's own menubar, which its mouse coordinates still include
// (mcpelauncher only takes it off for the game). Either may be null.
void connect(bool (*mouse_locked)(), const int* menubar_height);

bool key(int key, int action);
bool mouse_button(double x, double y, int button, int action);
bool mouse_move(double x, double y, bool relative);
bool scroll(double dx, double dy);
// What the menu needs from the platform's graphics: ImGui's backend for it, and a
// pixel of the frame the game just drew, read for the title screen's mark (x and y
// from 0 to 1, from the top left)
struct Renderer {
    bool (*init)();
    void (*new_frame)();
    void (*render)(ImDrawData* data);
    void (*rebuild_fonts)();
    void (*read_pixel)(float x, float y, uint8_t rgba[4]);
};
void set_renderer(const Renderer& renderer);
const Renderer& renderer();

// Called once per frame after the game drew it, with the frame's size in pixels
void frame(int width, int height);

}  // namespace menu
