// Mouse Tweaks on Android: which slot the mouse is over, and the clicks sent for it.
// A screen with slots is a ContainerScreenController. When the mouse comes onto a slot
// the game calls slot 62 of its vtable with the slot's collection and number, and slot
// 63 when it leaves. Some screens bring their own slot 62, so each one found is wrapped.
// Clicks go in through the launcher's own GLFW handlers, the way real ones arrive.
// ponytail: the vtable slots are 1.21.114.1's, checked against the two functions' first instructions. Other versions get the Shift sweep and no wheel

#include "log.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

#include "hooks.h"
#include "shared.h"

namespace mods {
namespace {

void* (*current_context)() = nullptr;
void (*mouse_button)(void* window, int button, int action, int mods) = nullptr;
void (*key)(void* window, int key, int scancode, int action, int mods) = nullptr;
std::atomic<void*> window{nullptr};
std::atomic<bool> over_items{false}, entered{false};
// which slot the mouse is over, so leaving another one doesn't count as leaving this one:
// moving to the slot next door, the game reports the arrival before the leaving
std::atomic<uint64_t> slot{0};

uint64_t slot_id(const std::string& collection, int index) {
    return std::hash<std::string_view>{}(collection) * 31 + uint64_t(index) + 1;
}
std::atomic<bool> sending{false};
// the frame a Shift pressed for a click is let go on, or -1. The game looks at its keys
// once a frame, so a Shift pressed and let go around a click in one go is never seen held
std::atomic<int> frame{0}, release_shift_at{-1};

// GLFW's numbers
constexpr int LEFT = 0, PRESS = 1, RELEASE = 0, LEFT_SHIFT = 340, WITH_SHIFT = 1, KEY_Q = 81;

// the collections that hold items of the player's or of a chest's, where a Shift click
// moves a stack across and does nothing else
bool holds_items(std::string_view collection) {
    for (std::string_view name : {"hotbar_items", "inventory_items", "combined_hotbar_and_inventory_items", "container_items"})
        if (collection == name)
            return true;
    return false;
}

constexpr int WRAPS = 12;
uint64_t (*real_hover[WRAPS])(void*, const std::string&, int) = {};
template <int N>
uint64_t hover_hook(void* self, const std::string& collection, int index) {
    uint64_t result = real_hover[N](self, collection, index);
    slot = slot_id(collection, index);
    over_items = holds_items(collection);
    if (over_items)
        entered = true;
    return result;
}
template <int... N>
constexpr auto hover_hooks(std::integer_sequence<int, N...>) {
    return std::array<void*, sizeof...(N)>{reinterpret_cast<void*>(&hover_hook<N>)...};
}

uint64_t (*real_unhover)(void*, const std::string&, int) = nullptr;
uint64_t unhover_hook(void* self, const std::string& collection, int index) {
    if (slot == slot_id(collection, index))
        over_items = false;
    return real_unhover(self, collection, index);
}

}  // namespace

void mousetweaks_connect(void* context, void* button, void* key_function) {
    current_context = reinterpret_cast<decltype(current_context)>(context);
    mouse_button = reinterpret_cast<decltype(mouse_button)>(button);
    key = reinterpret_cast<decltype(key)>(key_function);
}

void mousetweaks_install(const std::vector<Segment>& segs, Patch patch) {
    void** base = vtable_named(segs, "25ContainerScreenController");
    // stp x29, x30, [sp, #-0x30]! opens the hover this was read from, sub sp, sp, #0x70 the unhover
    if (!base || !in_code(segs, base[62]) || !in_code(segs, base[63]) || *static_cast<uint32_t*>(base[62]) != 0xa9bd7bfd ||
        *static_cast<uint32_t*>(base[63]) != 0xd101c3ff) {
        log_error("Mouse Tweaks couldn't find the game's slots, so the wheel is off");
        return;
    }
    // every screen that keeps the base's unhover is one of these; its slot 62 is wrapped
    void* unhover = base[63];
    real_unhover = reinterpret_cast<decltype(real_unhover)>(unhover);
    static const auto hooks = hover_hooks(std::make_integer_sequence<int, WRAPS>{});
    void* unhover_replacement = reinterpret_cast<void*>(&unhover_hook);
    int wrapped = 0, screens = 0;
    for (const Segment& seg : segs) {
        if (seg.exec)
            continue;
        auto** words = reinterpret_cast<void**>(seg.begin);
        for (size_t i = 1; i < seg.size / sizeof(void*); i++) {
            if (words[i] != unhover)
                continue;
            void* hover = words[i - 1];
            if (!in_code(segs, hover))
                continue;
            int n = 0;
            while (n < wrapped && reinterpret_cast<void*>(real_hover[n]) != hover)
                n++;
            if (n == WRAPS)
                continue;
            if (n == wrapped)
                real_hover[wrapped++] = reinterpret_cast<uint64_t (*)(void*, const std::string&, int)>(hover);
            void* hover_replacement = hooks[n];
            patch(words + i - 1, &hover_replacement, sizeof(void*));
            patch(words + i, &unhover_replacement, sizeof(void*));
            screens++;
        }
    }
    log_info("Mouse Tweaks watches the slots of %d screens", screens);
}

// Once a frame, on the thread that draws: the window is the one whose context is current
void mousetweaks_frame() {
    if (!window && current_context)
        window = current_context();
    int now = ++frame;
    int due = release_shift_at;
    if (due >= 0 && now >= due && release_shift_at.compare_exchange_strong(due, -1)) {
        sending = true;
        key(window, LEFT_SHIFT, 0, RELEASE, 0);
        sending = false;
    }
}

bool slot_hovered() { return over_items; }
bool entered_slot() { return entered.exchange(false); }
void mousetweaks_unhover() { over_items = false; }
bool clicking() { return sending; }

void click_once(bool hold_shift) {
    void* w = window;
    if (!w || !mouse_button || !key)
        return;
    sending = true;
    if (hold_shift)
        key(w, LEFT_SHIFT, 0, PRESS, WITH_SHIFT);
    mouse_button(w, LEFT, PRESS, WITH_SHIFT);
    mouse_button(w, LEFT, RELEASE, WITH_SHIFT);
    // let go a few frames on, from the frame tick
    if (hold_shift)
        release_shift_at = frame + 3;
    sending = false;
}

// The drop key is down already, so it's let go and pressed again for the new slot
void drop_again() {
    void* w = window;
    if (!w || !key)
        return;
    sending = true;
    key(w, KEY_Q, 0, RELEASE, 0);
    key(w, KEY_Q, 0, PRESS, 0);
    sending = false;
}

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
