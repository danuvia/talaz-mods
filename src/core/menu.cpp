#include "menu.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "assets.h"
#include "config.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "mods.h"
#include "theme.h"
#include "title.h"

EMBED(font_regular, FONT_DIR "/Poppins-Regular.ttf")

namespace menu {
namespace {

// Key codes and actions as the launcher's game window reports them
constexpr int KEY_ESCAPE = 27;
constexpr int KEY_PRESS = 0, KEY_RELEASE = 2;
constexpr int BUTTON_PRESS = 0;

// What the player asked for. Input arrives outside the frame, so the frame
// picks the change up from here.
std::atomic<bool> wanted{false};
bool ready = false;
float width = 0, height = 0, scale = 0;
// While the game holds the mouse it only sends relative motion, so the menu
// keeps its own cursor and draws it
float cursor_x = 0, cursor_y = 0;
bool relative_mouse = false;
std::chrono::steady_clock::time_point last_frame;
// Talaz's title screen is up, seen in the last frame, and a click on a button only the mod handles
std::atomic<bool> on_title{false}, title_click{false};
// The slider being dragged on a settings page
const mods::Setting* dragging = nullptr;
// The mod whose settings page is open, none for the grid of tiles
std::atomic<mods::Mod*> page{nullptr};
// Letters typed while the tiles are up go to the search. Keys arrive outside the frame,
// so they wait here for it; a backspace takes one off and an escape clears it
std::mutex typed_lock;
std::string typed;
std::atomic<bool> searching{false};
bool (*mouse_locked)() = nullptr;
const int* menubar_height = nullptr;
Renderer backend{};

// A world is being played with no screen over it, the game holding the mouse
bool playing() { return !mouse_locked || mouse_locked(); }

// The launcher's window coordinates put the game's top edge below its menubar
float window_y(double y) { return float(y) - (menubar_height ? float(*menubar_height) : 0.f); }

bool init() {
    if (!backend.init)
        return false;
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    if (!backend.init()) {
        ImGui::DestroyContext();
        return false;
    }
    ready = true;
    return true;
}

void load_fonts() {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;
    Asset font = ASSET(font_regular);
    io.Fonts->AddFontFromMemoryTTF((void*)font.data, int(font.size), 24 * scale, &config);
    // the title screen's wordmark, drawn large, gets a size of its own so it stays sharp
    io.Fonts->AddFontFromMemoryTTF((void*)font.data, int(font.size), 108 * scale, &config);
    backend.rebuild_fonts();
}

using namespace theme;
constexpr float PI = 3.14159265f;

// The swell: a wave line across the box from a to b, crest height amp
void wave(ImDrawList* dl, float x0, float x1, float y, float amp, float phase) {
    for (int i = 0; i <= 24; i++)
        dl->PathLineTo(ImVec2(x0 + (x1 - x0) * i / 24, y - amp * std::sin(phase + i * PI / 6)));
}

bool inside(ImVec2 p, ImVec2 a, ImVec2 b) { return p.x >= a.x && p.x < b.x && p.y >= a.y && p.y < b.y; }

// An on and off switch drawn at a, its knob sliding over with level 0 to 1
void toggle_switch(ImDrawList* dl, ImVec2 a, float u, bool on) {
    ImVec2 b(a.x + 44 * u, a.y + 24 * u);
    dl->AddRectFilled(a, b, on ? ACCENT : LINE, 12 * u);
    dl->AddCircleFilled(ImVec2(on ? b.x - 12 * u : a.x + 12 * u, a.y + 12 * u), 9 * u, on ? DEEP : MUTED);
}

// The menu is one window in the middle of the screen, laid out like TwoClient's: a
// sidebar of categories on the left, a header with the search and the close button, and
// the tiles or a mod's settings under it. Sizes are for a 720p screen and scale with u
constexpr float WINDOW_W = 1040, WINDOW_H = 612, SIDEBAR_W = 196, GUTTER = 24, HEADER_H = 76;
constexpr ImU32 PAGE = IM_COL32(12, 18, 23, 246), SIDE = IM_COL32(8, 12, 16, 150);

// ponytail: the categories live here, by mod id. A mod listed in none shows under All mods only; give mods.def a category once the launcher's page wants them too
struct Category {
    const char* name;
    const char* mods;  // ids with a space on both sides, or nothing for every mod
};
const Category CATEGORIES[] = {
        {"All mods", ""},
        {"HUD", " fps cps keystrokes clock session "},
        {"Visual", " zoom nightvision motionblur camerasway "},
        {"Gameplay", " food mousetweaks togglesprint "},
        {"Performance", " riptide fpslimit "},
};
int category = 0;
std::string search;

// The game hides the system arrow while it holds the mouse, so draw the macOS one: black
// with a white edge, in points like the real one (about 2 pixels each on a Retina screen)
void draw_arrow(ImDrawList* dl, ImVec2 at) {
    float k = 1.45f * scale;
    auto p = [&](float x, float y) { return ImVec2(at.x + x * k, at.y + y * k); };
    ImVec2 edge[] = {p(0, 0), p(0, 16), p(4, 12.5f), p(6.8f, 18.5f), p(9.2f, 17.5f), p(6.6f, 11.6f), p(11.5f, 11.6f)};
    ImVec2 body[] = {p(1, 2.4f), p(1, 13.6f), p(4.3f, 10.7f), p(9, 10.7f)};
    ImVec2 tail[] = {p(4, 11), p(7.2f, 17.2f), p(8.2f, 16.8f), p(5.4f, 10.6f)};
    ImVec2 edge_tail[] = {edge[2], edge[3], edge[4], edge[5]};
    dl->AddConvexPolyFilled(edge_tail, 4, IM_COL32_WHITE);
    ImVec2 edge_body[] = {edge[0], edge[1], edge[2], edge[6]};
    dl->AddConvexPolyFilled(edge_body, 4, IM_COL32_WHITE);
    dl->AddConvexPolyFilled(body, 4, IM_COL32_BLACK);
    dl->AddConvexPolyFilled(tail, 4, IM_COL32_BLACK);
}
bool search_focus = false;
// how far down the tiles are scrolled, and where the wheel has sent them
float scrolled = 0, scroll_to = 0;
// when the menu last opened and when its page last changed, for the two animations
float opened_at = -10, drawn_at = -10, turned_at = -10;

bool in_category(const Category& c, const char* id) {
    return !*c.mods || std::string(c.mods).find(std::string(" ") + id + " ") != std::string::npos;
}

bool contains(const char* text, const std::string& lowered) {
    std::string t = text;
    for (char& c : t)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return t.find(lowered) != std::string::npos;
}

ImU32 mix(ImU32 from, ImU32 to, float t) {
    ImVec4 a = ImGui::ColorConvertU32ToFloat4(from), b = ImGui::ColorConvertU32ToFloat4(to);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t));
}

ImU32 faded(ImU32 color, float alpha) {
    return (color & ~IM_COL32_A_MASK) | (ImU32(float((color >> IM_COL32_A_SHIFT) & 255) * std::clamp(alpha, 0.f, 1.f)) << IM_COL32_A_SHIFT);
}

// A value that eases towards its target a little every frame, one per key
float eased(const void* owner, int index, float target, float dt) {
    static std::unordered_map<uintptr_t, float> values;
    float& v = values.try_emplace(reinterpret_cast<uintptr_t>(owner) * 31 + uintptr_t(index), target).first->second;
    v += (target - v) * std::min(1.f, dt * 14);
    return v;
}

// The accent's glow behind the window, two soft blobs as in TwoClient's shell: a grid
// of vertices over the rounded window, each coloured by how far it is from the blobs
void glow(ImDrawList* dl, ImVec2 a, ImVec2 b, float r, float u) {
    constexpr int NX = 40, NY = 24;
    ImVec2 one(a.x + 90 * u, a.y - 120 * u), two(b.x - 160 * u, b.y + 110 * u);
    auto colour = [&](float x, float y) {
        float near = std::min(std::hypot(x - one.x, y - one.y), std::hypot(x - two.x, y - two.y)) / (760 * u);
        float g = std::clamp(1 - near, 0.f, 1.f);
        return faded(ACCENT, g * g * 0.2f);
    };
    dl->PrimReserve(NX * NY * 6, (NX + 1) * (NY + 1));
    ImVec2 uv = dl->_Data->TexUvWhitePixel;
    ImDrawIdx base = ImDrawIdx(dl->_VtxCurrentIdx);
    for (int j = 0; j <= NY; j++)
        for (int i = 0; i <= NX; i++) {
            float x = a.x + (b.x - a.x) * i / NX, y = a.y + (b.y - a.y) * j / NY;
            // corner vertices are pulled onto the rounded edge
            float cx = std::clamp(x, a.x + r, b.x - r), cy = std::clamp(y, a.y + r, b.y - r);
            float d = std::hypot(x - cx, y - cy);
            if (d > r)
                x = cx + (x - cx) * r / d, y = cy + (y - cy) * r / d;
            dl->PrimWriteVtx(ImVec2(x, y), uv, colour(x, y));
        }
    for (int j = 0; j < NY; j++)
        for (int i = 0; i < NX; i++) {
            ImDrawIdx p = ImDrawIdx(base + j * (NX + 1) + i);
            for (ImDrawIdx k : {ImDrawIdx(p), ImDrawIdx(p + 1), ImDrawIdx(p + NX + 2), ImDrawIdx(p), ImDrawIdx(p + NX + 2), ImDrawIdx(p + NX + 1)})
                dl->PrimWriteIdx(k);
        }
}

// One mod's own page under the header: its note, its switch and every setting it has
void draw_page(ImDrawList* dl, mods::Mod& m, float left, float top, float width, float u) {
    ImFont* font = ImGui::GetFont();
    ImVec2 mouse = ImGui::GetIO().MousePos;
    bool click = ImGui::IsMouseClicked(0);

    ImVec2 ns = font->CalcTextSizeA(14 * u, FLT_MAX, width, m.note);
    dl->AddText(font, 14 * u, ImVec2(left, top), MUTED, m.note, nullptr, width);

    // the rows sit in one panel, the mod's own switch first
    auto settings = mods::settings_of(m.id);
    float row = 58 * u, y = top + ns.y + 22 * u;
    ImVec2 pa(left, y), pb(left + width, y + row * (1 + settings.size()));
    dl->AddRectFilled(pa, pb, INK, 16 * u);
    dl->AddRect(pa, pb, LINE, 16 * u, 0, std::max(1.f, u));
    auto label = [&](float ry, const char* text) {
        dl->AddText(font, 15 * u, ImVec2(left + 20 * u, ry + (row - 15 * u) / 2 - 2 * u), TEXT, text);
    };
    auto switch_at = [&](float ry, bool on) {
        ImVec2 a(left + width - 64 * u, ry + (row - 24 * u) / 2);
        toggle_switch(dl, a, u, on);
        return click && inside(mouse, ImVec2(left, ry), ImVec2(left + width, ry + row));
    };

    label(y, "Enabled");
    if (switch_at(y, mods::enabled(m.id)))
        mods::toggle(m.id);
    for (const mods::Setting* s : settings) {
        y += row;
        dl->AddLine(ImVec2(left + 20 * u, y), ImVec2(left + width - 20 * u, y), LINE, std::max(1.f, u));
        label(y, s->name);
        float value = config::get(s->key);
        if (s->is_switch()) {
            if (switch_at(y, value != 0))
                mods::set_setting(*s, value != 0 ? 0 : 1);
            continue;
        }
        // a slider: drag along it, the value reads to its left
        float tw = 260 * u, tx = left + width - 28 * u - tw, ty = y + row / 2;
        float part = std::clamp((value - s->min) / (s->max - s->min), 0.f, 1.f);
        ImVec2 ta(tx - 10 * u, ty - 14 * u), tb(tx + tw + 10 * u, ty + 14 * u);
        bool over = inside(mouse, ta, tb);
        if (over && click)
            dragging = s;
        if (dragging == s) {
            float at = std::clamp((mouse.x - tx) / tw, 0.f, 1.f);
            bool held = ImGui::IsMouseDown(0);
            mods::set_setting(*s, s->min + at * (s->max - s->min), !held);
            if (!held)
                dragging = nullptr;
            value = config::get(s->key);
            part = std::clamp((value - s->min) / (s->max - s->min), 0.f, 1.f);
        }
        dl->AddRectFilled(ImVec2(tx, ty - 2 * u), ImVec2(tx + tw, ty + 2 * u), LINE, 2 * u);
        dl->AddRectFilled(ImVec2(tx, ty - 2 * u), ImVec2(tx + tw * part, ty + 2 * u), ACCENT, 2 * u);
        dl->AddCircleFilled(ImVec2(tx + tw * part, ty), (over || dragging == s ? 9 : 8) * u, ACCENT);
        std::string text = mods::setting_text(*s, value);
        ImVec2 ts = font->CalcTextSizeA(14 * u, FLT_MAX, 0, text.c_str());
        dl->AddText(font, 14 * u, ImVec2(tx - 20 * u - ts.x, ty - ts.y / 2), ACCENT, text.c_str());
    }
    if (settings.empty())
        dl->AddText(font, 13 * u, ImVec2(left, pb.y + 14 * u), MUTED, "This mod has nothing to set besides on and off.");
}

// The water of the launcher's tiles: a spring moves the level so it sloshes and
// settles, and turning a mod on sends a wave in from the right. It moves every
// frame, menu open or not, so leaving the menu mid-wave doesn't freeze it
void step_water(float dt) {
    for (mods::Mod& m : mods::all()) {
        bool on = mods::enabled(m.id);
        // the first look settles the water where it belongs, without a wave
        if (!m.seen)
            m.seen = true, m.was_on = on, m.level = on ? 0.4f : -0.2f;
        if (on && !m.was_on)
            m.cover = 0, m.level = 0.9f, m.vel = 0;
        m.was_on = on;
        if (m.cover < 1)
            m.cover = std::min(1.f, m.cover + dt / 0.45f);
        float target = !on ? -0.2f : m.cover < 1 ? 0.92f : 0.4f;
        m.vel += ((target - m.level) * 60 - m.vel * 8) * dt;
        m.level += m.vel * dt;
    }
}

// One mod's tile from a to b: its name, what it is set to, its note, and the water that
// fills it while the mod is on. A click turns the mod on or off; the dots in the corner,
// or a right click, open its page. active is false while the mouse is outside the tiles
void draw_tile(ImDrawList* dl, mods::Mod& m, ImVec2 a, ImVec2 b, float u, float time, int i, bool active) {
    ImFont* font = ImGui::GetFont();
    ImVec2 mouse = ImGui::GetIO().MousePos;
    float w = b.x - a.x, r = 14 * u;
    bool hover = active && inside(mouse, a, b);
    if (hover && ImGui::IsMouseClicked(0) && !inside(mouse, ImVec2(b.x - 40 * u, a.y + 8 * u), ImVec2(b.x, a.y + 36 * u)))
        mods::toggle(m.id);
    // three dots in the corner, or a right click, open the mod's own page
    ImVec2 dots(b.x - 26 * u, a.y + 22 * u);
    bool over_dots = inside(mouse, ImVec2(dots.x - 12 * u, dots.y - 12 * u), ImVec2(dots.x + 18 * u, dots.y + 12 * u));
    if ((over_dots && ImGui::IsMouseClicked(0)) || (hover && ImGui::IsMouseClicked(1))) {
        page = &m;
        return;
    }
    bool on = mods::enabled(m.id);
    dl->AddRectFilled(a, b, INK, r);
    if (m.level > -0.08f) {
        // the inside of the rounded tile at x, so the water never leaves it
        auto edge = [&](float x) {
            float d = std::min(x - a.x, b.x - x);
            return d >= r ? 0.f : r - std::sqrt(std::max(0.f, r * r - (r - d) * (r - d)));
        };
        float H = b.y - a.y, W = b.x - a.x, amp = 2.5f * u + std::min(12 * u, std::abs(m.vel) * 16 * u);
        auto surface = [&](float x) {
            float y = b.y - H * m.level + amp * std::sin((x - a.x) / u * 0.04f + time * 2.4f + i) +
                      amp * 0.45f * std::sin((x - a.x) / u * 0.09f - time * 3.3f);
            return std::clamp(y, a.y + edge(x), b.y - edge(x));
        };
        float x0 = a.x + W * std::pow(1 - m.cover, 3.f), step = 4 * u;
        if (m.cover < 1) {
            // the front curls forward at the top
            float y0 = surface(x0);
            for (float y = b.y - edge(x0); y > y0; y -= step) {
                float f = (b.y - y) / std::max(1.f, b.y - y0);
                float x = std::max(a.x + edge(x0), x0 - 18 * u * f * f - 4 * u * std::sin(y / u * 0.15f + time * 10));
                dl->PathLineTo(ImVec2(x, std::clamp(y, a.y + edge(x), b.y - edge(x))));
            }
        }
        for (float x = x0; x < b.x; x += step)
            dl->PathLineTo(ImVec2(x, surface(x)));
        dl->PathLineTo(ImVec2(b.x, surface(b.x)));
        for (float x = b.x; x > x0; x -= step)
            dl->PathLineTo(ImVec2(x, b.y - edge(x)));
        dl->PathLineTo(ImVec2(x0, b.y - edge(x0)));
        dl->PathFillConcave(WATER);
        // Riptide's current: streaks racing through its water while it's on
        if (on && m.cover >= 1 && std::strcmp(m.id, "riptide") == 0) {
            // in the band of water above the note, so they never cross its words
            float top = b.y - H * m.level + 5 * u, floor = b.y - 46 * u;
            for (int k = 0; k < 3; k++) {
                float lane = top + (floor - top) * (0.15f + 0.35f * k), len = 26 * u + 9 * u * (k % 3);
                float x = a.x + std::fmod(time * (220 + 70 * k) * u + k * 97 * u, W + len) - len;
                float x1 = std::clamp(x, a.x + r, b.x - r), x2 = std::clamp(x + len, a.x + r, b.x - r);
                if (x2 > x1 && lane < b.y - edge(x2) - 2 * u)
                    dl->AddLine(ImVec2(x1, lane), ImVec2(x2, lane), IM_COL32(79, 216, 196, 150), std::max(1.f, 1.6f * u));
            }
        }
    }
    dl->AddRect(a, b, on ? ACCENT : hover ? MUTED : LINE, r, 0, std::max(1.f, u));
    dl->AddText(font, 15 * u, ImVec2(a.x + 16 * u, a.y + 14 * u), TEXT, m.name);
    for (int d = 0; d < 3; d++)
        dl->AddCircleFilled(ImVec2(b.x - 32 * u + d * 6 * u, a.y + 22 * u), 1.8f * u, over_dots ? TEXT : MUTED);
    // the first setting under the name, so the tile tells what it is set to
    if (std::string opt = mods::option_text(m.id); !opt.empty())
        dl->AddText(font, 12.5f * u, ImVec2(a.x + 16 * u, a.y + 36 * u), on ? ACCENT : MUTED, opt.c_str());
    float note = 12.5f * u;
    ImVec2 ns = font->CalcTextSizeA(note, FLT_MAX, w - 32 * u, m.note);
    dl->AddText(font, note, ImVec2(a.x + 16 * u, b.y - 14 * u - ns.y), on ? TEXT : MUTED, m.note, nullptr, w - 32 * u);
}

void draw(ImVec2 size, float dt) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mouse = io.MousePos;
    bool click = ImGui::IsMouseClicked(0);
    float time = float(ImGui::GetTime());
    // drawn for a 720p screen, and shrinks to fit a small window
    float u = std::min({scale * 1.5f, size.x / (WINDOW_W + 48), size.y / (WINDOW_H + 48)});
    float line = std::max(1.f, u);

    // a gap in drawing means the menu was closed in between, so it opens again
    if (time - drawn_at > 0.1f)
        opened_at = turned_at = time;
    drawn_at = time;
    float open = 1 - std::pow(1 - std::clamp((time - opened_at) / 0.24f, 0.f, 1.f), 3.f);
    dl->AddRectFilled(ImVec2(0, 0), size, IM_COL32(6, 10, 14, int(176 * open)));
    int first = dl->VtxBuffer.Size;

    ImVec2 a(std::round((size.x - WINDOW_W * u) / 2), std::round((size.y - WINDOW_H * u) / 2)), b(a.x + std::round(WINDOW_W * u), a.y + std::round(WINDOW_H * u));
    float r = 20 * u, side = std::round(a.x + SIDEBAR_W * u);
    // a soft shadow, rings fading out around the window and a little below it
    for (int i = 0; i < 16; i++) {
        float d = (i + 0.5f) * 3 * u, fade = 1 - (i + 0.5f) / 16;
        dl->AddRect(ImVec2(a.x - d, a.y - d + 8 * u), ImVec2(b.x + d, b.y + d + 8 * u), IM_COL32(0, 0, 0, int(46 * fade * fade)), r + d, 0, 3 * u + 1);
    }
    dl->AddRectFilled(a, b, PAGE, r);
    glow(dl, a, b, r, u);
    dl->AddRectFilled(a, ImVec2(side, b.y), SIDE, r, ImDrawFlags_RoundCornersLeft);
    dl->AddLine(ImVec2(side, a.y), ImVec2(side, b.y), LINE, line);
    dl->AddRect(a, b, LINE, r, 0, line);

    // what was typed since the last frame
    {
        std::lock_guard lock(typed_lock);
        for (char c : typed) {
            if (c == '\b' && !search.empty())
                search.pop_back();
            else if (c == 27)
                search.clear();
            else if (c >= ' ' && search.size() < 24)
                search += c;
        }
        if (!typed.empty())
            scroll_to = 0;
        typed.clear();
    }
    searching = !search.empty();
    mods::Mod* open_mod = page;

    // --- the sidebar: the name, the categories, the profile ---
    float bx = a.x + 24 * u, by = a.y + 30 * u;
    wave(dl, bx, bx + 24 * u, by + 6 * u, 3 * u, 0);
    dl->PathStroke(ACCENT, 0, 2.4f * u);
    wave(dl, bx, bx + 24 * u, by + 16 * u, 3 * u, 0);
    dl->PathStroke(ACCENT, 0, 2.4f * u);
    dl->AddText(font, 22 * u, ImVec2(bx + 36 * u, by - 5 * u), TEXT, "talaz");
    float ny = a.y + 96 * u;
    dl->AddText(font, 10.5f * u, ImVec2(a.x + 24 * u, ny), MUTED, "MODS");
    ny += 26 * u;
    for (int c = 0; c < int(std::size(CATEGORIES)); c++) {
        ImVec2 ea(a.x + 12 * u, ny), eb(side - 12 * u, ny + 34 * u);
        bool over = inside(mouse, ea, eb), selected = category == c;
        float chosen = eased(&CATEGORIES[c], 0, selected ? 1.f : 0.f, dt), lit = eased(&CATEGORIES[c], 1, over ? 1.f : 0.f, dt);
        dl->AddRectFilled(ea, eb, faded(ACCENT, chosen), 9 * u);
        dl->AddRect(ea, eb, faded(LINE, lit * (1 - chosen)), 9 * u, 0, line);
        dl->AddText(font, 14 * u, ImVec2(ea.x + 14 * u, ea.y + 8 * u), mix(mix(MUTED, TEXT, lit), DEEP, chosen), CATEGORIES[c].name);
        int count = 0;
        for (mods::Mod& m : mods::all())
            count += in_category(CATEGORIES[c], m.id);
        std::string number = std::to_string(count);
        ImVec2 cs = font->CalcTextSizeA(12 * u, FLT_MAX, 0, number.c_str());
        dl->AddText(font, 12 * u, ImVec2(eb.x - 14 * u - cs.x, ea.y + 10 * u), mix(MUTED, DEEP, chosen), number.c_str());
        if (over && click && (!selected || open_mod)) {
            category = c;
            page = nullptr;
            open_mod = nullptr;
            scrolled = scroll_to = 0;
            turned_at = time;
        }
        ny += 40 * u;
    }
    dl->AddLine(ImVec2(a.x, b.y - 66 * u), ImVec2(side, b.y - 66 * u), LINE, line);
    dl->AddText(font, 10.5f * u, ImVec2(a.x + 24 * u, b.y - 52 * u), MUTED, "PROFILE");
    dl->AddText(font, 13.5f * u, ImVec2(a.x + 24 * u, b.y - 36 * u), TEXT, config::profile().c_str());

    // --- the header: where you are, the search, the way out ---
    float left = side + GUTTER * u, right = b.x - GUTTER * u, mid = a.y + 38 * u;
    float title_x = left;
    if (open_mod) {
        ImVec2 ba(left - 6 * u, mid - 16 * u), bb(left + 26 * u, mid + 16 * u);
        bool over = inside(mouse, ba, bb);
        dl->AddRect(ba, bb, faded(LINE, eased(&page, 0, over ? 1.f : 0.f, dt)), 9 * u, 0, line);
        dl->PathLineTo(ImVec2(left + 13 * u, mid - 7 * u));
        dl->PathLineTo(ImVec2(left + 6 * u, mid));
        dl->PathLineTo(ImVec2(left + 13 * u, mid + 7 * u));
        dl->PathStroke(over ? TEXT : MUTED, 0, 2 * u);
        if (over && click) {
            page = nullptr;
            open_mod = nullptr;
            turned_at = time;
        }
        title_x = left + 40 * u;
    }
    const char* title = open_mod ? open_mod->name : CATEGORIES[category].name;
    dl->AddText(font, 24 * u, ImVec2(title_x, mid - 16 * u), TEXT, title);

    ImVec2 xa(right - 32 * u, mid - 16 * u), xb(right, mid + 16 * u);
    bool over_close = inside(mouse, xa, xb);
    dl->AddRect(xa, xb, faded(LINE, eased(&wanted, 0, over_close ? 1.f : 0.f, dt)), 9 * u, 0, line);
    ImVec2 xc((xa.x + xb.x) / 2, mid);
    dl->AddLine(ImVec2(xc.x - 6 * u, xc.y - 6 * u), ImVec2(xc.x + 6 * u, xc.y + 6 * u), over_close ? TEXT : MUTED, 2 * u);
    dl->AddLine(ImVec2(xc.x + 6 * u, xc.y - 6 * u), ImVec2(xc.x - 6 * u, xc.y + 6 * u), over_close ? TEXT : MUTED, 2 * u);
    if (over_close && click)
        wanted = false;

    if (!open_mod) {
        // typing anywhere on the tiles fills this in
        ImVec2 sa(xa.x - 12 * u - 232 * u, mid - 16 * u), sb(xa.x - 12 * u, mid + 16 * u);
        dl->AddRectFilled(sa, sb, IM_COL32(16, 23, 29, 200), 10 * u);
        // clicking it shows where the letters go; clicking anywhere else lets go of it
        if (click)
            search_focus = inside(mouse, sa, sb);
        bool hover_box = inside(mouse, sa, sb);
        dl->AddRect(sa, sb, search.empty() && !search_focus ? (hover_box ? MUTED : LINE) : ACCENT, 10 * u, 0, line);
        dl->AddCircle(ImVec2(sa.x + 18 * u, mid - 1 * u), 5.5f * u, MUTED, 0, 1.8f * u);
        dl->AddLine(ImVec2(sa.x + 22 * u, mid + 3 * u), ImVec2(sa.x + 26 * u, mid + 7 * u), MUTED, 1.8f * u);
        const char* shown = search.empty() ? "Type to search" : search.c_str();
        dl->AddText(font, 13.5f * u, ImVec2(sa.x + 36 * u, mid - 9 * u), search.empty() ? MUTED : TEXT, shown);
        if ((search_focus || !search.empty()) && std::fmod(time, 1.f) < 0.5f) {
            float end = sa.x + 36 * u + (search.empty() ? 0 : font->CalcTextSizeA(13.5f * u, FLT_MAX, 0, shown).x + 2 * u);
            dl->AddLine(ImVec2(end, mid - 8 * u), ImVec2(end, mid + 8 * u), ACCENT, line);
        }
    }

    // --- the page, sliding in a little from the right when it changes ---
    ImVec2 pa(left, a.y + HEADER_H * u), pb(right, b.y - 20 * u);
    int page_first = dl->VtxBuffer.Size;
    dl->PushClipRect(ImVec2(side + line, pa.y - 2 * u), ImVec2(b.x - line, b.y - line), true);
    if (open_mod) {
        draw_page(dl, *open_mod, pa.x, pa.y, pb.x - pa.x, u);
    } else {
        std::vector<mods::Mod*> shown;
        for (mods::Mod& m : mods::all())
            if (in_category(CATEGORIES[category], m.id) && (search.empty() || contains(m.name, search) || contains(m.note, search)))
                shown.push_back(&m);
        float gap = 12 * u, w = std::floor((pb.x - pa.x - 2 * gap) / 3), h = 132 * u;
        float rows = float((shown.size() + 2) / 3), content = rows * (h + gap) + 26 * u, view = pb.y - pa.y;
        bool in_view = inside(mouse, ImVec2(side, pa.y), ImVec2(b.x, b.y));
        if (in_view)
            scroll_to -= io.MouseWheel * 64 * u;
        scroll_to = std::clamp(scroll_to, 0.f, std::max(0.f, content - view));
        scrolled += (scroll_to - scrolled) * std::min(1.f, dt * 16);
        for (size_t i = 0; i < shown.size(); i++) {
            ImVec2 ta(std::round(pa.x + float(i % 3) * (w + gap)), std::round(pa.y + float(i / 3) * (h + gap) - scrolled));
            ImVec2 tb(ta.x + w, ta.y + std::round(h));
            if (tb.y < pa.y - 4 * u || ta.y > b.y)
                continue;
            draw_tile(dl, *shown[i], ta, tb, u, time, int(i), in_view);
        }
        if (shown.empty())
            dl->AddText(font, 14 * u, pa, MUTED, "No mod matches that.");
        else
            dl->AddText(font, 12.5f * u, ImVec2(pa.x, pa.y + rows * (h + gap) - scrolled + 2 * u), MUTED,
                        "Click a tile to turn it on. Its dots, or a right click, open its settings.");
        // a thin bar on the right while there is more than fits
        if (content > view) {
            float track = view - 8 * u, length = std::max(28 * u, track * view / content);
            float at = pa.y + 4 * u + (track - length) * scrolled / (content - view);
            dl->AddRectFilled(ImVec2(b.x - 9 * u, at), ImVec2(b.x - 6 * u, at + length), LINE, 2 * u);
        }
    }
    dl->PopClipRect();
    float turn = 1 - std::pow(1 - std::clamp((time - turned_at) / 0.2f, 0.f, 1.f), 3.f);
    for (int i = page_first; turn < 1 && i < dl->VtxBuffer.Size; i++) {
        ImDrawVert& v = dl->VtxBuffer[i];
        v.pos.x += (1 - turn) * 26 * u;
        v.col = faded(v.col, turn);
    }

    // the whole window rises into place and fades in as the menu opens
    for (int i = first; open < 1 && i < dl->VtxBuffer.Size; i++) {
        ImDrawVert& v = dl->VtxBuffer[i];
        v.pos.y += (1 - open) * 16 * u;
        v.col = faded(v.col, open);
    }
}

}  // namespace

void connect(bool (*locked)(), const int* menubar) {
    mouse_locked = locked;
    menubar_height = menubar;
    // for looking at the menu without entering a world
    // TALAZ_PREVIEW=zoom opens that mod's page
    const char* preview = std::getenv("TALAZ_PREVIEW");
    wanted = preview != nullptr;
    for (mods::Mod& m : mods::all())
        if (preview && std::string(preview) == m.id)
            page = &m;
}

bool key(int key, int action) {
    bool eaten = mods::on_key(key, action, playing() && !wanted);
    // The menu key opens the menu in a world and not over other screens; the
    // game only holds the mouse while nothing is open
    if (action == KEY_PRESS && key == int(config::get("key.menu")) && (wanted || playing())) {
        wanted = !wanted;
        return true;
    }
    if (wanted && action == KEY_PRESS && key == KEY_ESCAPE && page) {
        page = nullptr;
        return true;
    }
    // on the tiles, letters and numbers search them, and escape clears the search first
    if (wanted && action == KEY_PRESS && !page) {
        char typed_now = key >= 'A' && key <= 'Z'                       ? char(key + 32)
                         : (key >= '0' && key <= '9') || key == ' '     ? char(key)
                         : key == 8                                     ? '\b'
                         : key == KEY_ESCAPE && searching               ? char(27)
                                                                        : 0;
        if (typed_now) {
            std::lock_guard lock(typed_lock);
            typed += typed_now;
            return true;
        }
    }
    if (wanted && action == KEY_PRESS && key == KEY_ESCAPE) {
        wanted = false;
        return true;
    }
    // the menu keeps key presses from the game but lets releases through, so a key
    // held when the menu opened doesn't stay held in the game after it closes
    return (wanted && action != KEY_RELEASE) || eaten;
}

bool mouse_button(double x, double y, int button, int action) {
    if (!wanted)
        mods::on_mouse_button(button, action);
    // the title screen's Mods button has no game button under it, so the mod takes its clicks
    if (!wanted && on_title && button == 1 && title::owns(ImVec2(float(x), window_y(y)), ImVec2(width, height))) {
        if (action == BUTTON_PRESS)
            title_click = true;
        return true;
    }
    if (!wanted || !ready)
        return wanted;
    // The launcher numbers buttons from 1, left first
    int index = button - 1;
    if (index >= 0 && index < 3) {
        if (!relative_mouse) {
            cursor_x = float(x);
            cursor_y = window_y(y);
        }
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(cursor_x, cursor_y);
        io.AddMouseButtonEvent(index, action == BUTTON_PRESS);
    }
    return true;
}

bool mouse_move(double x, double y, bool relative) {
    relative_mouse = relative;
    if (relative && !wanted && playing())
        mods::on_look(x, y);
    if (relative && wanted) {
        cursor_x = std::clamp(cursor_x + float(x), 0.f, width);
        cursor_y = std::clamp(cursor_y + float(y), 0.f, height);
    } else if (!relative) {
        cursor_x = float(x);
        cursor_y = window_y(y);
        if (!wanted)
            mods::on_cursor(cursor_x, cursor_y);
    }
    if (wanted && ready)
        ImGui::GetIO().AddMousePosEvent(cursor_x, cursor_y);
    return wanted;
}

bool scroll(double dx, double dy) {
    if (!wanted && mods::on_scroll(dy, playing()))
        return true;
    if (wanted && ready)
        ImGui::GetIO().AddMouseWheelEvent(float(dx), float(dy));
    return wanted;
}

void set_renderer(const Renderer& r) { backend = r; }
const Renderer& renderer() { return backend; }

void frame(int w, int h) {
    auto now = std::chrono::steady_clock::now();
    float dt = last_frame.time_since_epoch().count() ? std::chrono::duration<float>(now - last_frame).count() : 1 / 60.f;
    dt = std::clamp(dt, 1 / 1000.f, 0.25f);
    last_frame = now;
    mods::tick(dt);
    step_water(dt);
    mods::limit_fps();
    // the HUDs show over a world being played, not over the game's screens; off a
    // world, the frame is looked at for the title screen's mark
    bool hud = (mods::hud_visible() || mods::effects_visible()) && playing();
    if (!wanted && !hud && playing())
        return;

    if (w <= 0 || h <= 0)
        return;
    width = float(w);
    height = float(h);
    if (!ready && !init()) {
        wanted = false;
        return;
    }

    ImVec2 size(width, height);
    // sized for a 1080p window
    float s = std::clamp(height / 1080, 0.5f, 3.f);
    if (s != scale) {
        scale = s;
        load_fonts();
    }

    on_title = !playing() && title::showing();
    if (!wanted && !hud && !on_title)
        return;

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = size;
    io.DeltaTime = dt;
    io.MouseDrawCursor = false;

    backend.new_frame();
    ImGui::NewFrame();
    if (on_title && !wanted && title::draw(ImGui::GetBackgroundDrawList(), size, ImVec2(cursor_x, cursor_y), title_click.exchange(false), float(ImGui::GetTime())))
        wanted = true;
    if (wanted)
        draw(size, dt);
    if (relative_mouse && wanted)
        draw_arrow(ImGui::GetForegroundDrawList(), ImVec2(cursor_x, cursor_y));
    else if (playing())
        mods::draw_hud(ImGui::GetForegroundDrawList(), scale * 1.5f, ImGui::GetFont());
    ImGui::Render();
    backend.render(ImGui::GetDrawData());
}

}  // namespace menu
