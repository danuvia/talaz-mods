// Night Vision makes the game believe the player has the effect: every place that
// asks "has night vision?" before asking "how much?" hears yes, and "how much" hears
// full. It looks the same as the potion, leaves the HUD alone and does not stack with it.

#include <android/log.h>

#include <cstring>
#include <utility>

#include "shared.h"

namespace mods {
namespace {

uint32_t* night_vision_scale = nullptr;
uint32_t night_vision_original[2];
std::vector<std::pair<uint32_t*, uint32_t>> night_vision_checks;
Patch patch_code = nullptr;

void set_night_vision(bool on) {
    static bool now = false;
    if (!night_vision_scale || on == now)
        return;
    const uint32_t full[2] = {0x1E2E1000, 0xD65F03C0};  // fmov s0, #1.0; ret
    patch_code(night_vision_scale, const_cast<uint32_t*>(on ? full : night_vision_original), sizeof full);
    for (auto& [at, original] : night_vision_checks) {
        uint32_t yes = 0x52800020;  // mov w0, #1
        patch_code(at, on ? &yes : &original, sizeof yes);
    }
    __android_log_print(ANDROID_LOG_INFO, CLIENT_NAME, "Night Vision %s at %p: %08x %08x", on ? "on" : "off", night_vision_scale, night_vision_scale[0], night_vision_scale[1]);
    now = on;
}

}  // namespace

// found by the formula's 0.2 pi constant: mov w8, #0xd97c; movk w8, #0x3f20, lsl #16
// ponytail: the start sits 0xd4 before it on 1.21.114.1. Scan back for the prologue if other versions move it
void nightvision_install(const std::vector<Segment>& segs, Patch patch) {
    for (const Segment& seg : segs) {
        if (!seg.exec)
            continue;
        auto* w = reinterpret_cast<uint32_t*>(seg.begin);
        for (size_t i = 0x40, n = seg.size / 4 - 1; i < n; i++) {
            if (w[i] != 0x529B2F88 || w[i + 1] != 0x72A7E408 || w[i - 0xd4 / 4] != 0xD10143FF)
                continue;
            night_vision_scale = w + i - 0xd4 / 4;
            std::memcpy(night_vision_original, night_vision_scale, sizeof night_vision_original);
            // the callers: the closest bl before each call is the "has the effect?" check
            for (size_t j = 0; j < n; j++) {
                if ((w[j] & 0xfc000000) != 0x94000000 || w + j + (int32_t(w[j] << 6) >> 6) != night_vision_scale)
                    continue;
                for (size_t k = j - 1; k + 8 > j && k > 0; k--)
                    if ((w[k] & 0xfc000000) == 0x94000000) {
                        night_vision_checks.emplace_back(w + k, w[k]);
                        break;
                    }
            }
            __android_log_print(ANDROID_LOG_INFO, CLIENT_NAME, "Night Vision found %zu checks", night_vision_checks.size());
            patch_code = patch;
            return;
        }
    }
    __android_log_print(ANDROID_LOG_ERROR, CLIENT_NAME, "Night Vision couldn't find the game's night vision");
}

void nightvision_tick() {
    set_night_vision(enabled("nightvision"));
}

}  // namespace mods
