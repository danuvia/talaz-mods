// FPS Limit: holds each frame back so the game runs at the cap.

#include <algorithm>
#include <cstdlib>
#include <thread>

#include "config.h"
#include "shared.h"

namespace mods {

void fpslimit_wait() {
    // a benchmark measures the game, not the cap
    static const bool bench = std::getenv("TALAZ_BENCH") != nullptr;
    if (bench)
        return;
    static Clock::time_point next;
    auto now = Clock::now();
    if (!enabled("fpslimit")) {
        next = now;
        return;
    }
    next += std::chrono::duration_cast<Clock::duration>(std::chrono::duration<float>(1 / std::max(10.f, config::get("fpslimit.cap"))));
    // a slow frame is not made up for by rushing the next ones
    if (next < now)
        next = now;
    else
        std::this_thread::sleep_until(next);
}

}  // namespace mods
