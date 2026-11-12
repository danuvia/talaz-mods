// Night Vision makes the game believe the player has the effect: every place that
// asks "has night vision?" before asking "how much?" hears yes, and "how much" hears
// full. It looks the same as the potion, leaves the HUD alone and does not stack with it.

#include "log.h"

#include <cstring>
#include <utility>

#include "hooks.h"

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
    log_info("Night Vision %s at %p: %08x %08x", on ? "on" : "off", night_vision_scale, night_vision_scale[0], night_vision_scale[1]);
    now = on;
}

}  // namespace

// found by the formula's constants in any register: 0.2 pi (mov #0xd97c; movk #0x3f20,
// lsl #16), then 0.3 and 0.7 (movk #0x3e99 and #0x3f33) right after it. The function
// starts at the closest sub sp before them, 0xd4 back on 1.21.114.1
// ponytail: 1.26 builds the formula another way and Night Vision logs that it found nothing there
void nightvision_install(const std::vector<Segment>& segs, Patch patch) {
    auto has = [](const uint32_t* w, uint32_t op) {
        for (int k = 0; k < 16; k++)
            if ((w[k] & ~31u) == op)
                return true;
        return false;
    };
    for (const Segment& seg : segs) {
        if (!seg.exec)
            continue;
        auto* w = reinterpret_cast<uint32_t*>(seg.begin);
        for (size_t i = 0x80, n = seg.size / 4 - 16; i < n; i++) {
            if ((w[i] & ~31u) != 0x529B2F80 || !has(w + i, 0x72A7E400) || !has(w + i, 0x72A7D320) || !has(w + i, 0x72A7E660))
                continue;
            size_t start = i;
            while (start > i - 0x80 && (w[start] & 0xFF8003FF) != 0xD10003FF)  // sub sp, sp, #n
                start--;
            if (start == i - 0x80)
                continue;
            night_vision_scale = w + start;
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
            log_info("Night Vision found %zu checks", night_vision_checks.size());
            patch_code = patch;
            return;
        }
    }
    log_error("Night Vision couldn't find the game's night vision");
}

void nightvision_tick() {
    set_night_vision(enabled("nightvision"));
}

}  // namespace mods
