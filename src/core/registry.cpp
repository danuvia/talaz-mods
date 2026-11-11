// The mods and their settings, from mods.def, which the launcher reads too: the
// menu's tiles and pages, the defaults, and turning mods on and off.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "config.h"
#include "mods.h"
#include "shared.h"

namespace mods {
namespace {

Mod list[] = {
#define MOD(id, name, note, on) {#id, name, note},
#define SLIDER(...)
#define SWITCH(...)
#include "mods.def"
#undef MOD
#undef SLIDER
#undef SWITCH
};

// a slider from min to max, or a switch when there is no step
const Setting SETTINGS[] = {
#define MOD(...)
#define SLIDER(mod, name, key, unit, min, max, step, value) {#mod, name, key, unit, min, max, step},
#define SWITCH(mod, name, key, value) {#mod, name, key},
#include "mods.def"
#undef MOD
#undef SLIDER
#undef SWITCH
};

// what each setting and mod starts at
struct Declared {
    const char* key;
    float value;
};
const Declared DEFAULTS[] = {
#define MOD(id, name, note, on) {#id ".enabled", on},
#define SLIDER(mod, name, key, unit, min, max, step, value) {key, value},
#define SWITCH(mod, name, key, value) {key, value},
#include "mods.def"
#undef MOD
#undef SLIDER
#undef SWITCH
};

}  // namespace

std::span<Mod> all() { return list; }

// TALAZ_BENCH_ONLY=fps,motionblur limits a benchmark to those mods; "none" turns them all off
bool enabled(const char* id) {
    static const char* only = std::getenv("TALAZ_BENCH_ONLY");
    if (only)
        return (std::string(",") + only + ",").find(std::string(",") + id + ",") != std::string::npos;
    return config::on(std::string(id) + ".enabled");
}

void toggle(const char* id) {
    config::set(std::string(id) + ".enabled", enabled(id) ? 0 : 1);
    config::save();
}

std::vector<const Setting*> settings_of(const char* id) {
    std::vector<const Setting*> out;
    for (const Setting& s : SETTINGS)
        if (std::strcmp(s.mod, id) == 0)
            out.push_back(&s);
    return out;
}

std::string setting_text(const Setting& s, float value) {
    if (s.is_switch())
        return value != 0 ? "On" : "Off";
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g%s", value, s.unit);
    return buf;
}

std::string option_text(const char* id) {
    auto list = settings_of(id);
    return list.empty() || list[0]->is_switch() ? std::string() : setting_text(*list[0], config::get(list[0]->key));
}

void set_setting(const Setting& s, float value, bool save) {
    if (!s.is_switch())
        value = std::clamp(s.min + std::round((value - s.min) / s.step) * s.step, s.min, s.max);
    config::set(s.key, value);
    if (save)
        config::save();
}

void declare() {
    for (const Declared& d : DEFAULTS)
        config::declare(d.key, d.value);
    config::declare("key.zoom", 'C');
    config::declare("key.sprint", 17);
}

}  // namespace mods
