// What the game's input and frames go through: the keys and buttons the mods watch,
// the mouse motion that turns the camera, and the frame tick that runs every mod.

#include <algorithm>

#include "config.h"
#include "mods.h"
#include "shared.h"

namespace mods {

std::atomic<bool> keys[5];
std::atomic<bool> buttons[2];
float turned_px = 0, look_vx = 0, look_vy = 0;

namespace {

constexpr int KEY_W = 'W', KEY_A = 'A', KEY_S = 'S', KEY_D = 'D', KEY_SPACE = ' ';
std::atomic<float> look_x{0}, look_y{0};
std::atomic<bool> jump_pressed{false};

int key_slot(int key) {
    switch (key) {
    case KEY_W: return 0;
    case KEY_A: return 1;
    case KEY_S: return 2;
    case KEY_D: return 3;
    case KEY_SPACE: return 4;
    default: return -1;
    }
}

}  // namespace

bool on_key(int key, int action, bool playing) {
    if (action != KEY_PRESS && action != KEY_RELEASE)
        return false;
    bool down = action == KEY_PRESS;
    if (togglesprint_key(key, down, playing))
        return true;
    if (int slot = key_slot(key); slot >= 0) {
        if (slot == 4 && down && !keys[4] && playing)
            jump_pressed = true;
        keys[slot] = down;
    }
    zoom_key(key, down, playing);
    return false;
}

void on_look(double dx, double dy) {
    look_x = look_x + float(dx);
    look_y = look_y + float(dy);
}

void on_mouse_button(int button, int action) {
    int index = button - 1;
    if (index < 0 || index > 1)
        return;
    bool down = action == BUTTON_PRESS;
    buttons[index] = down;
    if (down)
        huds_click(index);
}

bool on_scroll(double dy, bool playing) {
    return zoom_scroll(dy, playing);
}

void tick(float dt) {
    // the turn since the last frame, scaled to a 60th of a second so it reads the
    // same at any frame rate
    turned_px = look_x.exchange(0);
    look_vx = turned_px / std::max(dt * 60, 0.1f);
    look_vy = look_y.exchange(0) / std::max(dt * 60, 0.1f);
    world_new_frame();
    huds_tick(dt);
    nightvision_tick();
    camerasway_tick(dt, jump_pressed.exchange(false));
    motionblur_tick(dt);
    zoom_tick(dt);
}

void limit_fps() { fpslimit_wait(); }
void riptide(void* window) { riptide_apply(window); }
bool hud_visible() { return huds_visible(); }
void draw_hud(ImDrawList* dl, float u, ImFont* font) { huds_draw(dl, u, font); }

// The world pass draws nothing over the picture, but it needs the menu's GL functions loaded
bool effects_visible() {
    return enabled("motionblur") || enabled("camerasway");
}

}  // namespace mods
