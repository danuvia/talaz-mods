// Toggle Sprint: press Ctrl once to keep sprinting. The game keeps seeing Ctrl held,
// because its release is kept back until the next press.
// The game forgets every held key when a screen opens over the world or the window loses
// focus, which used to end the sprint while the mod still counted it as on. So coming
// back to the world, the key is let go and pressed again for the game.

#include "config.h"
#include "shared.h"

namespace mods {

namespace {
bool sprinting = false;
}

bool togglesprint_key(int key, bool down, bool playing) {
    if (key != int(config::get("key.sprint")) || !(sprinting || (playing && enabled("togglesprint"))))
        return false;
    if (down)
        sprinting = !sprinting;
    return down != sprinting;
}

void togglesprint_frame(bool playing) {
    static bool was_playing = false;
    static int press_in = -1;
    // turned off in the menu while sprinting: let the key go
    if (sprinting && !enabled("togglesprint")) {
        sprinting = false;
        send_sprint(false);
    }
    // a few frames after the world is back, once the game listens to keys again
    if (sprinting && playing && !was_playing)
        press_in = 3;
    was_playing = playing;
    if (!sprinting || !playing) {
        press_in = -1;
        return;
    }
    if (press_in >= 0 && press_in-- == 0) {
        send_sprint(false);
        send_sprint(true);
    }
}

}  // namespace mods
