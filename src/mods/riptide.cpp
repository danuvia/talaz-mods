// Riptide. mcpelauncher decides on a Mac that vsync is broken and paces frames itself,
// sleeping after each one, which held the game near 190 fps; a swap interval of 0
// turns that off, and 1 puts it back. The game's own vsync setting never reaches the
// window, so the mod sets it on the window directly, through the launcher's own
// GLFWGameWindow::setSwapInterval.

#include <android/log.h>

#include "mods.h"
#include "shared.h"

namespace mods {
namespace {

using SetSwapInterval = void (*)(void*, int);
SetSwapInterval set_swap_interval = nullptr;

}  // namespace

void set_swap_interval_function(void* function) {
    set_swap_interval = reinterpret_cast<SetSwapInterval>(function);
}

void riptide_apply(void* window) {
    static int applied = -1;
    int want = enabled("riptide") ? 0 : 1;
    // only on the window class whose method this is: its vtable's swap interval slot must hold it
    if (!set_swap_interval || !window || want == applied || (*static_cast<void***>(window))[14] != reinterpret_cast<void*>(set_swap_interval))
        return;
    set_swap_interval(window, want);
    applied = want;
    __android_log_print(ANDROID_LOG_INFO, CLIENT_NAME, "Riptide %s", want ? "off" : "on");
}

}  // namespace mods
