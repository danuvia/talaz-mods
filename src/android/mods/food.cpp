// Food: the player's hunger, saturation and exhaustion, read from the game's attributes,
// and their marks drawn over the game's hunger bar, like AppleSkin.
// Actor::getAttribute hands back the attribute's instance, whose current value is the
// float at +0x7c. HudHungerRenderer's render, slot 17 of its vtable, is where the marks
// are drawn.
// ponytail: getAttribute and the three attributes are 1.21.114.1's offsets, checked against the function's first instructions. Other versions show no Food mod; find them through the "minecraft:player.saturation" string if that matters

#include "log.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "config.h"
#include "hooks.h"
#include "shared.h"

#include <algorithm>

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

// Drawing over the hunger bar the way the bar draws itself, with the game's own functions,
// so the marks land on the same pixels as the shanks. All of these are 1.21.114.1's
// ponytail: fixed offsets, checked against two functions' first instructions before use. Other versions draw nothing extra
std::byte* base = nullptr;
template <class T>
T fn(uintptr_t offset) { return reinterpret_cast<T>(base + offset); }

// what the game hands back in memory rather than registers
struct Blob {
    alignas(16) unsigned char bytes[0x40] = {};
    ~Blob() {}
};
struct View {
    const char* data;
    size_t size;
};

// the bar's three textures: a full and a half saturation edge, and exhaustion's checks
const char* const TEXTURES[3] = {"textures/ui/talaz/saturation_full", "textures/ui/talaz/saturation_half", "textures/ui/talaz/exhaustion"};
Blob textures[3];
bool loaded = false, drawable = false;

// the way HudHungerRenderer's update loads its textures, through the client's texture group
void load_textures(void* client) {
    loaded = true;
    auto group_of = reinterpret_cast<Blob (*)(void*)>((*static_cast<void***>(client))[0x368 / 8]);
    Blob group = group_of(client);  // ponytail: the shared pointer is kept, once
    void* textures_group = *reinterpret_cast<void**>(group.bytes);
    for (int i = 0; i < 3; i++) {
        Blob location;
        View view{TEXTURES[i], std::strlen(TEXTURES[i])};
        fn<void (*)(void*, const View*)>(0xcfa571c)(location.bytes, &view);
        Blob texture = fn<Blob (*)(void*, void*, int, void*, int)>(0x67f6ff0)(textures_group, location.bytes, 0, nullptr, 0);
        fn<void (*)(void*, void*)>(0xcfa5734)(textures[i].bytes, texture.bytes);
        fn<void (*)(void*)>(0xcfa5740)(texture.bytes);
    }
}

// The bar keeps its shanks' places at +0xdd8, 12 bytes each, the first shank's first,
// and draws through six batches at +0xe8, one per texture. A hunger effect uses the odd
// ones and otherwise the even ones, so the three it isn't using are free for ours
void draw_extra(char* self, void* ctx, void* client, void* control) {
    if (!loaded)
        load_textures(client);
    void* screen = fn<void* (*)(void*)>(0x7b2ebb0)(ctx);
    const float* at = fn<const float* (*)(void*)>(0x63aab08)(control);
    void* stack = *reinterpret_cast<char**>(static_cast<char*>(screen) + 0x18) + 0x48;
    Blob pushed = fn<Blob (*)(void*, int)>(0xcfa9f4c)(stack, 0);
    float* m = fn<float* (*)(Blob*)>(0xcfa9f94)(&pushed);
    for (int i = 0; i < 4; i++)
        m[12 + i] += m[i] * at[0] + m[4 + i] * at[1];

    auto draw = fn<void (*)(void*, void*, void*, size_t, const void*, float)>(0x6360ffc);
    int spare = self[0x12] ? 0 : 1;
    auto batch = [&](int k) { return self + 0xe8 + (k + spare) * 0x228; };
    const char* places = self + 0xdd8;
    float alpha;
    std::memcpy(&alpha, self + 8, sizeof alpha);
    if (config::get("food.exhaustion") != 0) {
        size_t n = size_t(std::clamp(std::lround(player_exhaustion / 4 * 10), 0L, 10L));
        if (n)
            draw(screen, batch(0), textures[2].bytes, n, places, alpha);
    }
    size_t full = size_t(std::clamp(player_saturation / 2, 0.f, 10.f));
    if (full)
        draw(screen, batch(2), textures[0].bytes, full, places, alpha);
    if (full < 10 && player_saturation - 2 * full >= 1)
        draw(screen, batch(4), textures[1].bytes, 1, places + full * 12, alpha);
    fn<void (*)(Blob*)>(0xcfa9f70)(&pushed);
}

void (*real_render)(void*, void*, void*, void*, int, float*) = nullptr;
void render_hook(void* self, void* ctx, void* client, void* control, int pass, float* area) {
    real_render(self, ctx, client, control, pass, area);
    auto* bar = static_cast<char*>(self);
    if (drawable && enabled("food") && bar[0x11] && player_saturation >= 0 && area[1] - area[0] > 40)
        draw_extra(bar, ctx, client, control);
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
    // sub sp, sp, #0x80 opens the render this was read from, and sub sp, sp, #0x160 the icon drawing
    drawable = static_cast<std::byte*>(vtable[17]) == base + 0x636ec24 && *reinterpret_cast<uint32_t*>(base + 0x636ec24) == 0xd10203ff &&
               *reinterpret_cast<uint32_t*>(base + 0x6360ffc) == 0xd10583ff;
    if (!drawable)
        log_error("Food couldn't find how the hunger bar draws");
    void* hook = reinterpret_cast<void*>(&render_hook);
    patch(vtable + 17, &hook, sizeof hook);
}

}  // namespace

void food_install(const std::vector<Segment>& segs, Patch patch) {
    base = segs.front().begin;
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

}  // namespace mods
