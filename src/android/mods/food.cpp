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
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

#include "config.h"
#include "hooks.h"
#include "shared.h"

#include <algorithm>
#include <cmath>

namespace mods {

void hook_hover(void* any_item);

float player_hunger = -1, player_saturation = -1, player_exhaustion = -1;

namespace {

struct Handle {
    const char* instance;
    void* map;
};
Handle (*get_attribute)(void* actor, const void* attribute) = nullptr;
const void *hunger = nullptr, *saturation = nullptr, *exhaustion = nullptr, *health = nullptr;
float player_health = -1, player_max_health = 20;

// an attribute's current value, or its current maximum at +0x78
float value(void* player, const void* attribute, int at = 0x7c) {
    Handle h = get_attribute(player, attribute);
    if (!h.instance || !h.map)
        return -1;
    float v;
    std::memcpy(&v, h.instance + at, sizeof v);
    return std::isfinite(v) ? v : -1;
}

// What the held food gives: the item in the selected hotbar slot, looked up by name in
// the vanilla foods. The player's inventory is at +0x570, its selected slot at +0x10 and
// its container at +0xb8, whose vtable's +0x40 hands back a slot's ItemStack; the stack's
// +8 leads to its Item, whose full name is the string at +0xf0. 1.21.114.1's, like the rest
// ponytail: vanilla foods only, by name. Read the item's food component to cover add-ons' foods
struct Food {
    std::string_view name;
    int nutrition;
    float saturation;  // what eating it adds to saturation
};
const Food FOODS[] = {
    {"apple", 4, 2.4f}, {"baked_potato", 5, 6}, {"beef", 3, 1.8f}, {"beetroot", 1, 1.2f}, {"beetroot_soup", 6, 7.2f},
    {"bread", 5, 6}, {"carrot", 3, 3.6f}, {"chicken", 2, 1.2f}, {"chorus_fruit", 4, 2.4f}, {"cod", 2, 0.4f},
    {"cooked_beef", 8, 12.8f}, {"cooked_chicken", 6, 7.2f}, {"cooked_cod", 5, 6}, {"cooked_mutton", 6, 9.6f},
    {"cooked_porkchop", 8, 12.8f}, {"cooked_rabbit", 5, 6}, {"cooked_salmon", 6, 9.6f}, {"cookie", 2, 0.4f},
    {"dried_kelp", 1, 0.6f}, {"enchanted_golden_apple", 4, 9.6f}, {"glow_berries", 2, 0.4f}, {"golden_apple", 4, 9.6f},
    {"golden_carrot", 6, 14.4f}, {"honey_bottle", 6, 1.2f}, {"melon_slice", 2, 1.2f}, {"mushroom_stew", 6, 7.2f},
    {"mutton", 2, 1.2f}, {"poisonous_potato", 2, 1.2f}, {"porkchop", 3, 1.8f}, {"potato", 1, 0.6f}, {"pufferfish", 1, 0.2f},
    {"pumpkin_pie", 8, 4.8f}, {"rabbit", 3, 1.8f}, {"rabbit_stew", 10, 12}, {"rotten_flesh", 4, 0.8f}, {"salmon", 2, 0.4f},
    {"spider_eye", 2, 3.2f}, {"suspicious_stew", 6, 7.2f}, {"sweet_berries", 2, 0.4f}, {"tropical_fish", 1, 0.2f},
};
std::atomic<const Food*> held{nullptr};

std::string_view text_at(const char* s) {
    auto first = static_cast<unsigned char>(s[0]);
    if (!(first & 1))
        return {s + 1, size_t(first >> 1)};
    return {*reinterpret_cast<const char* const*>(s + 16), *reinterpret_cast<const size_t*>(s + 8)};
}

void read_held(char* player) {
    const Food* found = nullptr;
    char* inventory = *reinterpret_cast<char**>(player + 0x570);
    void* container = inventory ? *reinterpret_cast<void**>(inventory + 0xb8) : nullptr;
    if (container) {
        auto slot_item = reinterpret_cast<char* (*)(void*, int)>((*static_cast<void***>(container))[0x40 / 8]);
        char* stack = slot_item(container, *reinterpret_cast<int*>(inventory + 0x10));
        void** counter = stack && stack[0x22] ? *reinterpret_cast<void***>(stack + 8) : nullptr;
        char* item = counter ? static_cast<char*>(*counter) : nullptr;
        if (item) {
            std::string_view name = text_at(item + 0xf0);
            if (name.substr(0, 10) == "minecraft:")
                for (const Food& food : FOODS)
                    if (name.substr(10) == food.name)
                        found = &food;
            // foods are plain Items, so a food's vtable has Item's own tooltip function
            if (found)
                hook_hover(item);
        }
    }
    held = found;
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

// the bar's textures: a full and a half saturation edge, then exhaustion's checks over the
// right 1 to 8 columns of a shank, the 8 a shank's whole step along the bar
const char* const TEXTURES[10] = {"textures/ui/talaz/saturation_full", "textures/ui/talaz/saturation_half",
                                  "textures/ui/talaz/exhaustion_1", "textures/ui/talaz/exhaustion_2", "textures/ui/talaz/exhaustion_3",
                                  "textures/ui/talaz/exhaustion_4", "textures/ui/talaz/exhaustion_5", "textures/ui/talaz/exhaustion_6",
                                  "textures/ui/talaz/exhaustion_7", "textures/ui/talaz/exhaustion_8"};
constexpr int TEXTURE_COUNT = sizeof TEXTURES / sizeof *TEXTURES;
Blob textures[TEXTURE_COUNT];
bool loaded = false, drawable = false;

// the way HudHungerRenderer's update loads its textures, through the client's texture group
void load_textures(void* client) {
    loaded = true;
    auto group_of = reinterpret_cast<Blob (*)(void*)>((*static_cast<void***>(client))[0x368 / 8]);
    Blob group = group_of(client);  // ponytail: the shared pointer is kept, once
    void* textures_group = *reinterpret_cast<void**>(group.bytes);
    for (int i = 0; i < TEXTURE_COUNT; i++) {
        Blob location;
        View view{TEXTURES[i], std::strlen(TEXTURES[i])};
        fn<void (*)(void*, const View*)>(0xcfa571c)(location.bytes, &view);
        Blob texture = fn<Blob (*)(void*, void*, int, void*, int)>(0x67f6ff0)(textures_group, location.bytes, 0, nullptr, 0);
        fn<void (*)(void*, void*)>(0xcfa5734)(textures[i].bytes, texture.bytes);
        fn<void (*)(void*)>(0xcfa5740)(texture.bytes);
    }
}

// Moves the drawing to a HUD control's corner, the way its renderer does, keeping the
// pushed transform in transform until it's popped
Blob transform;
void* moved_to(void* ctx, void* control) {
    void* screen = fn<void* (*)(void*)>(0x7b2ebb0)(ctx);
    const float* at = fn<const float* (*)(void*)>(0x63aab08)(control);
    void* stack = *reinterpret_cast<char**>(static_cast<char*>(screen) + 0x18) + 0x48;
    transform = fn<Blob (*)(void*, int)>(0xcfa9f4c)(stack, 0);
    float* m = fn<float* (*)(Blob*)>(0xcfa9f94)(&transform);
    for (int i = 0; i < 4; i++)
        m[12 + i] += m[i] * at[0] + m[4 + i] * at[1];
    return screen;
}

// Exhaustion's checks, drawn before the game draws the bar so the shanks sit over them.
// Exhaustion grows from the right a column at a time, 80 columns at 4
void draw_exhaustion(char* self, void* ctx, void* control) {
    int spare = self[0x12] ? 0 : 1;
    char* batch = self + 0xe8 + spare * 0x228;
    if (!fn<bool (*)(void*)>(0xcfaad5c)(batch))
        return;
    long columns = std::clamp(std::lround(player_exhaustion / 4 * 80), 0L, 80L);
    size_t whole = size_t(columns / 8), part = size_t(columns % 8);
    if (!whole && !part)
        return;
    void* screen = moved_to(ctx, control);
    auto draw = fn<void (*)(void*, void*, void*, size_t, const void*, float)>(0x6360ffc);
    const char* places = self + 0xdd8;
    float alpha;
    std::memcpy(&alpha, self + 8, sizeof alpha);
    if (whole)
        draw(screen, batch, textures[9].bytes, whole, places, alpha);
    if (part)
        draw(screen, batch, textures[1 + part].bytes, 1, places + whole * 12, alpha);
    fn<void (*)(Blob*)>(0xcfa9f70)(&transform);
}

// The bar keeps its shanks' places at +0xdd8, 12 bytes each, the first shank's first,
// and draws through six batches at +0xe8, one per texture. A hunger effect uses the odd
// ones and otherwise the even ones, so the three it isn't using are free for ours
void draw_extra(char* self, void* ctx, void* client, void* control) {
    if (!loaded)
        load_textures(client);
    void* screen = moved_to(ctx, control);

    auto draw = fn<void (*)(void*, void*, void*, size_t, const void*, float)>(0x6360ffc);
    int spare = self[0x12] ? 0 : 1;
    auto batch = [&](int k) { return self + 0xe8 + (k + spare) * 0x228; };
    auto ready = fn<bool (*)(void*)>(0xcfaad5c);
    if (!ready(batch(0)) || !ready(batch(2)) || !ready(batch(4))) {
        fn<void (*)(Blob*)>(0xcfa9f70)(&transform);
        return;
    }
    const char* places = self + 0xdd8;
    float alpha;
    std::memcpy(&alpha, self + 8, sizeof alpha);
    size_t full = size_t(std::clamp(player_saturation / 2, 0.f, 10.f));
    if (full)
        draw(screen, batch(2), textures[0].bytes, full, places, alpha);
    if (full < 10 && player_saturation - 2 * full >= 1)
        draw(screen, batch(4), textures[1].bytes, 1, places + full * 12, alpha);
    // the held food's worth, flashing: the shanks it would fill, drawn with the bar's own
    // full and half textures, and the saturation it would add
    if (const Food* food = held.load(); food && config::get("food.preview") != 0) {
        double t = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        float flash = alpha * float(0.25 + 0.5 * (0.5 + 0.5 * std::sin(t * 5)));
        int hunger_now = std::clamp(int(player_hunger), 0, 20), hunger_then = std::min(20, hunger_now + food->nutrition);
        auto shanks = [&](float from, float to, int full_texture, int half_texture, bool game) {
            auto texture = [&](int i) { return game ? self + 0x28 + (i + (self[0x12] ? 1 : 0)) * 32 : reinterpret_cast<char*>(textures[i].bytes); };
            size_t first = size_t(from / 2), last = size_t(to / 2);
            if (last > first)
                draw(screen, batch(2), texture(full_texture), last - first, places + first * 12, flash);
            if (last < 10 && to - 2 * last >= 1)
                draw(screen, batch(4), texture(half_texture), 1, places + last * 12, flash);
        };
        shanks(float(hunger_now & ~1), float(hunger_then), 2, 4, true);
        float saturation_then = std::min(float(hunger_then), player_saturation + food->saturation);
        if (saturation_then > player_saturation)
            shanks(float(int(player_saturation) & ~1), saturation_then, 0, 1, false);
    }
    fn<void (*)(Blob*)>(0xcfa9f70)(&transform);
}

// How much health the held food would win back: health comes back while hunger is 18 or
// more, half a heart for 6 exhaustion, and every 4 exhaustion takes a point of saturation
// and then of hunger, so the points spent before hunger falls under 18 pay for it
// ponytail: an estimate like AppleSkin's, ignoring how fast health comes back and peaceful mode
float health_back(const Food& food) {
    float hunger_then = std::min(20.f, player_hunger + food.nutrition);
    if (hunger_then < 18)
        return 0;
    float saturation_then = std::min(hunger_then, player_saturation + food.saturation);
    float points = saturation_then + (hunger_then - 17);
    return std::max(0.f, std::floor((points * 4 - std::max(player_exhaustion, 0.f)) / 6));
}

// The hearts keep seven groups of a texture, a first heart and a count from +0x10, 0x18
// apart, the fifth the full hearts and the sixth the half ones, their textures from +0xb8
// and batches from +0x338, and their places in a vector at +0x2e58
void draw_hearts(char* self, void* ctx, void* control) {
    const Food* food = held.load();
    if (!food || config::get("food.preview") == 0 || player_health <= 0)
        return;
    float then = std::min(player_max_health, std::ceil(player_health) + health_back(*food));
    int from = int(std::ceil(player_health));
    const char* places = *reinterpret_cast<char**>(self + 0x2e58);
    size_t room = size_t(*reinterpret_cast<char**>(self + 0x2e60) - places) / 12;
    if (then <= from || !places || room == 0)
        return;
    // each texture's batch is set up for that texture, so the preview draws through the
    // batches the full and half hearts just used
    int full = *reinterpret_cast<int*>(self + 0x10 + 5 * 0x18), half = *reinterpret_cast<int*>(self + 0x10 + 6 * 0x18);
    if (full < 0 || full >= 20 || half < 0 || half >= 20)
        return;
    // the batches are set up over the first frames, and the hearts skip drawing until they are
    auto ready = fn<bool (*)(void*)>(0xcfaad5c);
    if (!ready(self + 0x338 + full * 0x228) || !ready(self + 0x338 + half * 0x228))
        return;
    void* screen = moved_to(ctx, control);
    auto draw = fn<void (*)(void*, void*, void*, size_t, const void*, float)>(0x6360ffc);
    float alpha;
    std::memcpy(&alpha, self + 8, sizeof alpha);
    double t = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    float flash = alpha * float(0.25 + 0.5 * (0.5 + 0.5 * std::sin(t * 5)));
    size_t first = size_t(from / 2), last = std::min(size_t(then / 2), room);
    if (last > first)
        draw(screen, self + 0x338 + full * 0x228, self + 0xb8 + full * 32, last - first, places + first * 12, flash);
    if (last < room && int(then) % 2)
        draw(screen, self + 0x338 + half * 0x228, self + 0xb8 + half * 32, 1, places + last * 12, flash);
    fn<void (*)(Blob*)>(0xcfa9f70)(&transform);
}

void (*real_render)(void*, void*, void*, void*, int, float*) = nullptr;
bool bar_ready = false;  // the bar has drawn once, so its places and batches are set
void render_hook(void* self, void* ctx, void* client, void* control, int pass, float* area) {
    auto* bar = static_cast<char*>(self);
    if (bar_ready && drawable && enabled("food") && bar[0x11] && player_exhaustion > 0 && config::get("food.exhaustion") != 0)
        draw_exhaustion(bar, ctx, control);
    real_render(self, ctx, client, control, pass, area);
    bar_ready = bar_ready || area[1] - area[0] > 40;
    if (drawable && enabled("food") && bar[0x11] && player_saturation >= 0 && area[1] - area[0] > 40)
        draw_extra(bar, ctx, client, control);
}

void (*real_hearts)(void*, void*, void*, void*, int, float*) = nullptr;
void hearts_hook(void* self, void* ctx, void* client, void* control, int pass, float* area) {
    real_hearts(self, ctx, client, control, pass, area);
    if (drawable && enabled("food") && static_cast<char*>(self)[0xd])
        draw_hearts(static_cast<char*>(self), ctx, control);
}

void** vtable_of(const std::vector<Segment>& segs, const char* name, size_t size) {
    std::byte* found = find_string(segs, name, size);
    std::byte* name_ref = found ? find_pointer(segs, found + 1) : nullptr;
    std::byte* typeinfo_ref = name_ref ? find_pointer(segs, name_ref - sizeof(void*)) : nullptr;
    return typeinfo_ref ? reinterpret_cast<void**>(typeinfo_ref + sizeof(void*)) : nullptr;
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
    // sub sp, sp, #0x90 opens the hearts' render
    static const char hearts_name[] = "\0" "16HudHeartRenderer";
    void** hearts = vtable_of(segs, hearts_name, sizeof hearts_name);
    if (drawable && hearts && static_cast<std::byte*>(hearts[17]) == base + 0x63a2ad8 && *reinterpret_cast<uint32_t*>(base + 0x63a2ad8) == 0xd10243ff) {
        real_hearts = reinterpret_cast<decltype(real_hearts)>(hearts[17]);
        void* hook = reinterpret_cast<void*>(&hearts_hook);
        patch(hearts + 17, &hook, sizeof hook);
    }
    void* hook = reinterpret_cast<void*>(&render_hook);
    patch(vtable + 17, &hook, sizeof hook);
}

}  // namespace


// The inventory's tooltips: the game asks the item to write its lines through slot 53 of
// its vtable, and bread and most foods are plain Items, so every vtable that still has
// Item's own function there gets this one, which adds what the food gives
// ponytail: items that write their own lines, like stews and potions, get no Food line
void (*real_hover)(void*, void*, void*, std::string&, bool) = nullptr;
// Food's icons on the glyph page the launcher builds, font/glyph_E2.png
std::string glyph(int cell) {
    char32_t c = 0xe200 + cell;
    return {char(0xe0 | (c >> 12)), char(0x80 | ((c >> 6) & 0x3f)), char(0x80 | (c & 0x3f))};
}

// A row of shanks for points, two to a shank, the last one half when points are odd
std::string shanks(int points, int full_cell) {
    std::string row;
    for (int i = 0; i + 1 < points; i += 2)
        row += glyph(full_cell);
    if (points % 2)
        row += glyph(full_cell + 1);
    return row;
}

// the foods that make you hungry, shown with the game's green shanks
bool hungering(std::string_view name) {
    for (std::string_view bad : {"chicken", "rotten_flesh", "spider_eye", "poisonous_potato", "pufferfish"})
        if (name == bad)
            return true;
    return false;
}

void hover_hook(void* item, void* stack, void* level, std::string& text, bool advanced) {
    real_hover(item, stack, level, text, advanced);
    if (!enabled("food"))
        return;
    std::string_view name = text_at(static_cast<char*>(item) + 0xf0);
    if (name.substr(0, 10) != "minecraft:")
        return;
    for (const Food& food : FOODS)
        if (name.substr(10) == food.name) {
            int saturation = std::max(1, int(std::lround(food.saturation)));
            text += "\n" + shanks(food.nutrition, hungering(food.name) ? 2 : 0) + "\n" + shanks(saturation, 4);
        }
}

// Hooked the first time an item is read, from that item's own vtable
std::vector<Segment> hover_segs;
Patch hover_patch = nullptr;
void hook_hover(void* any_item) {
    static bool done = false;
    if (done || !hover_patch)
        return;
    void** vtable = *static_cast<void***>(any_item);
    // only a plain Item's vtable has Item's own function; foods with effects are subclasses
    // that write their own lines, so this waits until a plain one is held
    const char* type = *reinterpret_cast<const char* const*>(static_cast<char*>(vtable[-1]) + 8);
    if (std::strcmp(type, "4Item") != 0)
        return;
    done = true;
    void* original = vtable[53];
    if (!in_code(hover_segs, original))
        return;
    real_hover = reinterpret_cast<decltype(real_hover)>(original);
    void* hook = reinterpret_cast<void*>(&hover_hook);
    int count = 0;
    for (const Segment& seg : hover_segs) {
        if (seg.exec)
            continue;
        auto** words = reinterpret_cast<void**>(seg.begin);
        for (size_t i = 0; i < seg.size / sizeof(void*); i++)
            if (words[i] == original)
                hover_patch(words + i, &hook, sizeof hook), count++;
    }
    log_info("Food put its line in %d item tooltips", count);
}

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
    health = base + 0x1071a700;
    hook_renderer(segs, patch);
    hover_segs = segs;
    hover_patch = patch;
}

void read_food(void* player) {
    if (!get_attribute)
        return;
    player_hunger = value(player, hunger);
    player_saturation = value(player, saturation);
    player_exhaustion = value(player, exhaustion);
    read_held(static_cast<char*>(player));
    player_health = value(player, health);
    player_max_health = value(player, health, 0x78);
}

}  // namespace mods
