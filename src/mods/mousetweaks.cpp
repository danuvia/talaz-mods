// Mouse Tweaks: moving items fast in inventories. Hold Shift with the left button and
// sweep over items to send each one you pass to the other side. It is the game's own
// Shift click, sent again for you as the mouse moves.
// ponytail: no wheel yet. Scrolling over an item should move it too, but a tooltip alone doesn't say the mouse is over an inventory slot (recipes and the creative list have them), and a Shift click there crafts or grabs. Read the hovered slot's collection from the inventory screen first

#include <cmath>

#include "shared.h"

namespace mods {
namespace {

bool shift = false, left = false;
float last_x = 0, last_y = 0;
// how far the cursor moves between clicks while sweeping, in window points: well under a slot
constexpr float SWEEP = 6;

}  // namespace

void mousetweaks_key(int key, bool down) {
    // 16 is Shift, and the right one comes with 256 added
    if ((key & 255) == 16)
        shift = down;
}

void mousetweaks_button(int index, bool down) {
    if (index == 0 && !clicking())
        left = down;
}

void mousetweaks_cursor(float x, float y) {
    bool sweeping = left && shift && enabled("mousetweaks");
    if (sweeping && std::hypot(x - last_x, y - last_y) < SWEEP)
        return;
    last_x = x, last_y = y;
    // the button is already down from the first click, so it's let go and pressed again
    if (sweeping)
        click_again();
}

}  // namespace mods
