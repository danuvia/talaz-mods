// Zoom: hold C to zoom in, scroll while holding to zoom further. The camera's
// tryGetFOV is found through CameraAPI's vtable and answered with a narrower field.

#include <android/log.h>

#include <algorithm>
#include <cmath>
#include <optional>

#include "config.h"
#include "shared.h"

namespace mods {

float last_fov = 70;

namespace {

std::atomic<bool> zoom_held{false};
float zoom_now = 1;  // the multiplier applied this frame, eased towards the target
std::optional<float> (*try_get_fov)(void*) = nullptr;

// If a game update moves tryGetFOV again the hooked slot is some other
// function, so Zoom only touches values that look like a field of view and
// gives up after a run of ones that don't
std::atomic<int> implausible{0};
constexpr int GIVE_UP_AFTER = 120;

std::optional<float> zoomed_fov(void* camera) {
    std::optional<float> fov = try_get_fov(camera);
    if (implausible >= GIVE_UP_AFTER)
        return fov;
    if (!fov || !(*fov > 1 && *fov < 179)) {
        if (fov && ++implausible == GIVE_UP_AFTER)
            __android_log_print(ANDROID_LOG_ERROR, CLIENT_NAME, "Zoom turned itself off, this game version moved the camera's field of view");
        return fov;
    }
    implausible = 0;
    last_fov = *fov * zoom_now;
    *fov *= zoom_now;
    return fov;
}

}  // namespace

void zoom_install(const std::vector<Segment>& segs, Patch patch, int major, int minor, int patch_version) {
    // CameraAPI's vtable, found through its RTTI: the type name string, the
    // typeinfo that points at it, then the vtable that points at the typeinfo
    static const char name[] = "\0" "9CameraAPI";
    std::byte* found = find_string(segs, name, sizeof name);
    if (!found)
        return;
    std::byte* name_ref = find_pointer(segs, found + 1);
    if (!name_ref)
        return;
    std::byte* typeinfo_ref = find_pointer(segs, name_ref - sizeof(void*));
    if (!typeinfo_ref)
        return;
    auto** vtable = reinterpret_cast<void**>(typeinfo_ref + sizeof(void*));

    // tryGetFOV moved one slot down in 1.21.130
    bool newer = major > 1 || minor > 21 || (minor == 21 && patch_version >= 130);
    void** slot = vtable + (newer ? 7 : 6);
    if (!in_code(segs, *slot)) {
        __android_log_print(ANDROID_LOG_ERROR, CLIENT_NAME, "Zoom couldn't find the camera on %d.%d.%d", major, minor, patch_version);
        return;
    }
    try_get_fov = reinterpret_cast<decltype(try_get_fov)>(*slot);
    void* hook = reinterpret_cast<void*>(&zoomed_fov);
    patch(slot, &hook, sizeof hook);
}

void zoom_key(int key, bool down, bool playing) {
    if (key == int(config::get("key.zoom")))
        zoom_held = down && playing && enabled("zoom");
}

bool zoom_scroll(double dy, bool playing) {
    if (!zoom_held || !playing)
        return false;
    // each notch zooms by a tenth, in half steps
    float f = config::get("zoom.factor") * (dy > 0 ? 1.1f : 1 / 1.1f);
    config::set("zoom.factor", std::clamp(std::round(f * 2) / 2, 1.5f, 50.f));
    return true;
}

void zoom_tick(float dt) {
    float target = zoom_held ? 1 / config::get("zoom.factor") : 1;
    zoom_now = config::get("zoom.smooth") != 0 ? zoom_now + (target - zoom_now) * (1 - std::exp(-dt * 12)) : target;
}

}  // namespace mods
