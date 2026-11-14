// Mouse Tweaks on Android: the clicks it sends go in through the launcher's own GLFW
// mouse handler, the way real ones arrive.

#include <atomic>

#include "hooks.h"
#include "shared.h"

namespace mods {
namespace {

void* (*current_context)() = nullptr;
void (*mouse_button)(void* window, int button, int action, int mods) = nullptr;
std::atomic<void*> window{nullptr};
bool sending = false;

// GLFW's numbers
constexpr int LEFT = 0, PRESS = 1, RELEASE = 0, WITH_SHIFT = 1;

}  // namespace

void mousetweaks_connect(void* context, void* button) {
    current_context = reinterpret_cast<decltype(current_context)>(context);
    mouse_button = reinterpret_cast<decltype(mouse_button)>(button);
}

// Once a frame, on the thread that draws: the window is the one whose context is current
void mousetweaks_frame() {
    if (!window && current_context)
        window = current_context();
}

bool clicking() { return sending; }

void click_again() {
    void* w = window;
    if (!w || !mouse_button)
        return;
    sending = true;
    mouse_button(w, LEFT, RELEASE, WITH_SHIFT);
    mouse_button(w, LEFT, PRESS, WITH_SHIFT);
    sending = false;
}

}  // namespace mods
