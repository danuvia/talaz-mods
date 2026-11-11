// Toggle Sprint: press Ctrl once to keep sprinting. The game keeps seeing Ctrl held,
// because its release is kept back until the next press.

#include "config.h"
#include "shared.h"

namespace mods {

// ponytail: Ctrl stays held for the game in chat and menus too. Let go there once the mod can send keys itself
bool togglesprint_key(int key, bool down, bool playing) {
    static bool sprinting = false;
    if (key != int(config::get("key.sprint")) || !(sprinting || (playing && enabled("togglesprint"))))
        return false;
    if (down)
        sprinting = !sprinting;
    return down != sprinting;
}

}  // namespace mods
