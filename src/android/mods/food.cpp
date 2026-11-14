// Food: the player's hunger, saturation and exhaustion, read from the game's attributes,
// and where the game draws the hunger bar, so the Food mod can draw over it like AppleSkin.
// Actor::getAttribute hands back the attribute's instance, whose current value is the
// float at +0x7c. HudHungerRenderer's render, slot 17 of its vtable, writes the bar's
// area in UI units into its last argument.
// ponytail: getAttribute and the three attributes are 1.21.114.1's offsets, checked against the function's first instructions. Other versions show no Food mod; find them through the "minecraft:player.saturation" string if that matters

#include "log.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "hooks.h"
#include "shared.h"

namespace mods {

float player_hunger = -1, player_saturation = -1, player_exhaustion = -1;

namespace {

struct Handle {
    const char* instance;
    void* map;
};
Handle (*get_attribute)(void* actor, const void* attribute) = nullptr;
const void *hunger = nullptr, *saturation = nullptr, *exhaustion = nullptr;

float value(void* player, const void* attribute) {
    Handle h = get_attribute(player, attribute);
    if (!h.instance || !h.map)
        return -1;
    float v;
    std::memcpy(&v, h.instance + 0x7c, sizeof v);
    return std::isfinite(v) ? v : -1;
}

// the bar's left, right, top and bottom in UI units, and the frame the game last drew it
float bar[4] = {};
std::atomic<int> bar_drawn{-100};
std::atomic<int> frame{0};

void (*real_render)(void*, void*, void*, void*, int, float*) = nullptr;
void render_hook(void* self, void* ctx, void* client, void* control, int pass, float* area) {
    real_render(self, ctx, client, control, pass, area);
    if (area[1] - area[0] > 40 && area[3] > area[2]) {
        std::memcpy(bar, area, sizeof bar);
        bar_drawn = frame.load();
    }
}

void hook_renderer(const std::vector<Segment>& segs, Patch patch) {
    static const char name[] = "\0" "17HudHungerRenderer";
    std::byte* found = find_string(segs, name, sizeof name);
    std::byte* name_ref = found ? find_pointer(segs, found + 1) : nullptr;
    std::byte* typeinfo_ref = name_ref ? find_pointer(segs, name_ref - sizeof(void*)) : nullptr;
    if (!typeinfo_ref) {
        log_error("Food couldn't find the hunger bar");
        return;
    }
    auto** vtable = reinterpret_cast<void**>(typeinfo_ref + sizeof(void*));
    if (!in_code(segs, vtable[17]))
        return;
    real_render = reinterpret_cast<decltype(real_render)>(vtable[17]);
    void* hook = reinterpret_cast<void*>(&render_hook);
    patch(vtable + 17, &hook, sizeof hook);
}

}  // namespace

void food_install(const std::vector<Segment>& segs, Patch patch) {
    std::byte* base = segs.front().begin;
    // stp x29, x30, [sp, #-0x20]!; str x19, [sp, #0x10]; mov x29, sp; mov x19, x1
    const uint32_t start[] = {0xa9be7bfd, 0xf9000bf3, 0x910003fd, 0xaa0103f3};
    if (std::memcmp(base + 0xc95c3dc, start, sizeof start) != 0) {
        log_error("Food couldn't find the player's attributes");
        return;
    }
    get_attribute = reinterpret_cast<decltype(get_attribute)>(base + 0xc95c3dc);
    hunger = base + 0x1071b270;
    saturation = base + 0x1071b2a0;
    exhaustion = base + 0x1071b2d0;
    hook_renderer(segs, patch);
}

void read_food(void* player) {
    if (!get_attribute)
        return;
    player_hunger = value(player, hunger);
    player_saturation = value(player, saturation);
    player_exhaustion = value(player, exhaustion);
}

// The hunger bar in screen pixels, if the game drew it in the last few frames. The bar's
// right end sits 89.5 units right of the screen's middle, measured against the hotbar on
// 1.21.114, and that gives the screen's width in UI units
// ponytail: the UI scale is worked out from the hotbar's layout. Read the game's own scale if a layout ever moves the bar
bool hunger_bar(float& right, float& top, float& unit) {
    int now = ++frame;
    if (now - bar_drawn > 3)
        return false;
    float width = 2 * (bar[1] - 89.5f);
    if (width < 200)
        return false;
    unit = ImGui::GetIO().DisplaySize.x / width;
    // the icons sit two units right of and half a unit below the area the game reports, measured on 1.21.114
    right = (bar[1] + 2) * unit;
    top = (bar[2] + 0.5f) * unit;
    return true;
}

}  // namespace mods
