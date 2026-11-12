// The HUDs: FPS, CPS, Clock, Session and Keystrokes, and what they count.

#include <android/log.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <mutex>

#include "config.h"
#include "shared.h"
#include "theme.h"

namespace mods {
namespace {

// The mods that draw on screen
const char* const HUDS[] = {"fps", "cps", "clock", "session", "keystrokes"};

size_t cps[2] = {};
int fps = 0, frames = 0;
float fps_window = 0;
const Clock::time_point started = Clock::now();
// clicks arrive with input and are counted in the frame
std::mutex clicks_lock;
std::deque<Clock::time_point> clicks[2];

}  // namespace

void huds_click(int index) {
    std::lock_guard lock(clicks_lock);
    clicks[index].push_back(Clock::now());
}

bool ab_test = false;

void huds_tick(float dt) {
    frames++;
    fps_window += dt;
    if (fps_window >= 1) {
        fps = int(std::lround(frames / fps_window));
        // TALAZ_BENCH=1 logs the frame rate every second, for comparing settings
        static const bool bench = std::getenv("TALAZ_BENCH") != nullptr;
        // TALAZ_AB=1 also flips ab_test every 3 seconds, so an optimisation can be compared within one run
        static const bool ab = std::getenv("TALAZ_AB") != nullptr;
        static int seconds = 0;
        if (bench)
            __android_log_print(ANDROID_LOG_INFO, CLIENT_NAME, "bench fps %d ab %d", fps, ab_test ? 1 : 0);
        if (ab && ++seconds % 3 == 0)
            ab_test = !ab_test;
        frames = 0;
        fps_window = 0;
    }
    auto cutoff = Clock::now() - std::chrono::seconds(1);
    std::lock_guard lock(clicks_lock);
    for (int i = 0; i < 2; i++) {
        while (!clicks[i].empty() && clicks[i].front() < cutoff)
            clicks[i].pop_front();
        cps[i] = clicks[i].size();
    }
}

bool huds_visible() {
    return std::any_of(std::begin(HUDS), std::end(HUDS), [](const char* hud) { return enabled(hud); });
}

// The HUDs stack down from the top left in a fixed order, each below the one
// before, so they never overlap. The game's paper doll only shows while the player
// moves, so when it is turned on the HUDs keep beside its spot rather than below
// ponytail: the paper doll's width is guessed from the screen height. Ask the game for its HUD layout once the mod can
void huds_draw(ImDrawList* dl, float u, ImFont* font) {
    using namespace theme;
    ImVec2 display = ImGui::GetIO().DisplaySize;
    float x = std::round(paper_doll() ? display.y * 0.12f : 12 * u), y = std::round(12 * u), gap = 6 * u;
    auto label = [&](const char* str) {
        float size = 14 * u, px = 10 * u, py = 6 * u;
        ImVec2 box = font->CalcTextSizeA(size, FLT_MAX, 0, str);
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + box.x + 2 * px, y + box.y + 2 * py), PANEL, 8 * u);
        dl->AddText(font, size, ImVec2(std::round(x + px), std::round(y + py)), TEXT, str);
        y = std::round(y + box.y + 2 * py + gap);
    };

    char buf[64];
    if (enabled("fps")) {
        std::snprintf(buf, sizeof buf, "%d FPS", fps);
        label(buf);
    }
    if (enabled("cps")) {
        std::snprintf(buf, sizeof buf, "%zu | %zu CPS", cps[0], cps[1]);
        label(buf);
    }
    if (enabled("clock")) {
        time_t now = time(nullptr);
        tm parts{};
        localtime_r(&now, &parts);
        if (config::get("clock.24h") != 0)
            std::snprintf(buf, sizeof buf, "%02d:%02d", parts.tm_hour, parts.tm_min);
        else
            std::snprintf(buf, sizeof buf, "%d:%02d %s", (parts.tm_hour + 11) % 12 + 1, parts.tm_min, parts.tm_hour < 12 ? "AM" : "PM");
        label(buf);
    }
    if (enabled("session")) {
        long s = std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - started).count();
        if (s >= 3600)
            std::snprintf(buf, sizeof buf, "%ld:%02ld:%02ld", s / 3600, s / 60 % 60, s % 60);
        else
            std::snprintf(buf, sizeof buf, "%ld:%02ld", s / 60, s % 60);
        label(buf);
    }
    if (enabled("keystrokes")) {
        ImVec2 o(x, y);
        float k = 34 * u, gap = 4 * u, size = 12.5f * u;
        // a pressed key fills with the accent, like a tile that is on
        auto key = [&](float x, float y, float w, float h, bool down, const char* str) {
            ImVec2 a(o.x + x, o.y + y), b(a.x + w, a.y + h);
            dl->AddRectFilled(a, b, down ? ACCENT : PANEL, 8 * u);
            ImVec2 box = font->CalcTextSizeA(size, FLT_MAX, 0, str);
            dl->AddText(font, size, ImVec2(std::round((a.x + b.x - box.x) / 2), std::round((a.y + b.y - box.y) / 2)),
                        down ? DEEP : TEXT, str);
        };
        float row = k + gap, wide = (3 * k + gap) / 2;
        key(row, 0, k, k, keys[0], "W");
        key(0, row, k, k, keys[1], "A");
        key(row, row, k, k, keys[2], "S");
        key(2 * row, row, k, k, keys[3], "D");
        float next = 2 * row;
        if (config::get("keystrokes.mouse") != 0) {
            key(0, next, wide, k, buttons[0], "LMB");
            key(wide + gap, next, wide, k, buttons[1], "RMB");
            next += row;
        }
        if (config::get("keystrokes.space") != 0)
            key(0, next, 3 * k + 2 * gap, k / 2, keys[4], "");
    }
}

}  // namespace mods
