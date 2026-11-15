// Mouse Tweaks: moving items fast in inventories. Scroll over a slot to send its stack to
// the other side, and hold Shift with the left button and sweep over items to send each
// one you pass. Both are the game's own Shift click, sent for you. Holding Q and sweeping
// drops each item you pass, the game's own drop key pressed again on every new slot.
// ponytail: Q is taken to be the drop key. Read the game's key binding if players rebind it
// ponytail: the wheel moves a whole stack, not one item a notch like the Java mod. The game has an "auto place one" of its own for that; call it on the game's thread once that's worth the work

#include <cmath>

#include "shared.h"

namespace mods {
namespace {

bool shift = false, left = false, drop = false;
float last_x = 0, last_y = 0;
// how far the cursor moves between clicks while sweeping, in window points: well under a slot
constexpr float SWEEP = 6;

}  // namespace

void mousetweaks_key(int key, bool down) {
    // a key Mouse Tweaks presses itself isn't the player's
    if (clicking())
        return;
    // 16 is Shift, and the right one comes with 256 added
    if ((key & 255) == 16)
        shift = down;
    if (key == 'Q')
        drop = down;
}

void mousetweaks_button(int index, bool down) {
    if (index == 0 && !clicking())
        left = down;
}

// The wheel over an inventory slot, while a screen has the mouse. Recipes and the creative
// list scroll as they always did: only slots that hold your items count
bool mousetweaks_scroll(bool playing) {
    if (playing || !enabled("mousetweaks") || !slot_hovered())
        return false;
    click_once(!shift);
    return true;
}

void mousetweaks_cursor(float x, float y) {
    // asked on every move, so an old arrival never waits for a later Q
    bool arrived = entered_slot();
    if (arrived && drop && enabled("mousetweaks"))
        drop_again();
    bool sweeping = left && shift && enabled("mousetweaks");
    if (sweeping && std::hypot(x - last_x, y - last_y) < SWEEP)
        return;
    last_x = x, last_y = y;
    // the button is already down from the first click, so it's let go and pressed again
    if (sweeping)
        click_again();
}

}  // namespace mods
