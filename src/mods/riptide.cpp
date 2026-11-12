// Riptide. mcpelauncher decides on a Mac that vsync is broken and paces frames itself,
// sleeping after each one, which held the game near 190 fps; a swap interval of 0
// turns that off, and 1 puts it back. The game's own vsync setting never reaches the
// window, so the mod sets it on the window directly, through the launcher's own
// GLFWGameWindow::setSwapInterval. It also cuts two kinds of wasted work in the
// game itself, below: GPU timing queries, and threads spinning on their waits.

#include <android/log.h>

#include <pthread.h>
#include <time.h>

#include <cstring>

#include "mods.h"
#include "shared.h"

namespace mods {
namespace {

using SetSwapInterval = void (*)(void*, int);
SetSwapInterval set_swap_interval = nullptr;

// The game times its GPU work every frame with timer queries, for Mojang's own
// profiling. Nothing a player sees uses them, and they cost the render thread about
// a twentieth of its time. With Riptide on, the queries do nothing and always read
// as finished in no time
using QueryCounter = void (*)(GLuint, GLenum);
using GetQueryInt = void (*)(GLuint, GLenum, GLint*);
using GetQuery64 = void (*)(GLuint, GLenum, uint64_t*);
QueryCounter real_counter = nullptr;
GetQueryInt real_query_int = nullptr;
GetQuery64 real_query_64 = nullptr;
void* (*real_get_proc)(const char*) = nullptr;

void query_counter(GLuint id, GLenum target) {
    if (!enabled("riptide") || ab_test)
        real_counter(id, target);
}
void get_query_int(GLuint id, GLenum name, GLint* out) {
    if (enabled("riptide") && !ab_test)
        *out = 1;  // available
    else
        real_query_int(id, name, out);
}
void get_query_64(GLuint id, GLenum name, uint64_t* out) {
    if (enabled("riptide") && !ab_test)
        *out = 0;
    else
        real_query_64(id, name, out);
}

void* get_proc(const char* name) {
    void* real = real_get_proc(name);
    if (!real || !name)
        return real;
    if (!std::strcmp(name, "glQueryCounterEXT"))
        return real_counter = reinterpret_cast<QueryCounter>(real), reinterpret_cast<void*>(&query_counter);
    if (!std::strcmp(name, "glGetQueryObjectivEXT"))
        return real_query_int = reinterpret_cast<GetQueryInt>(real), reinterpret_cast<void*>(&get_query_int);
    if (!std::strcmp(name, "glGetQueryObjectui64vEXT"))
        return real_query_64 = reinterpret_cast<GetQuery64>(real), reinterpret_cast<void*>(&get_query_64);
    return real;
}

// The game's threads wait on condition variables set to the monotonic clock, with
// deadlines on that clock. mcpelauncher's shim hands them to the host as if they
// were on the wall clock, so a deadline about now on the monotonic clock is decades
// ago on the wall clock: the wait returns at once and the thread spins. NetherNet's
// signalling thread burned a whole core this way. A deadline that far in the past is
// moved onto the wall clock
int (*real_timedwait)(pthread_cond_t*, pthread_mutex_t*, const timespec*) = nullptr;

int timedwait(pthread_cond_t* cond, pthread_mutex_t* mutex, const timespec* deadline) {
    timespec wall;
    clock_gettime(CLOCK_REALTIME, &wall);
    // ponytail: "a year before now" tells the clocks apart, true unless the Mac has been up for decades
    if (deadline && !ab_test && deadline->tv_sec < wall.tv_sec - 365 * 86400) {
        timespec mono;
        clock_gettime(CLOCK_MONOTONIC, &mono);
        long long ns = (deadline->tv_sec - mono.tv_sec) * 1000000000LL + (deadline->tv_nsec - mono.tv_nsec);
        ns += wall.tv_sec * 1000000000LL + wall.tv_nsec;
        timespec moved{time_t(ns / 1000000000LL), long(ns % 1000000000LL)};
        return real_timedwait(cond, mutex, &moved);
    }
    return real_timedwait(cond, mutex, deadline);
}

}  // namespace

void riptide_install(Patch patch) {
    real_timedwait = reinterpret_cast<decltype(real_timedwait)>(hook_import("pthread_cond_timedwait", reinterpret_cast<void*>(&timedwait), patch));
    real_get_proc = reinterpret_cast<decltype(real_get_proc)>(hook_import("eglGetProcAddress", reinterpret_cast<void*>(&get_proc), patch));
    if (!real_get_proc)
        __android_log_print(ANDROID_LOG_ERROR, CLIENT_NAME, "Riptide couldn't find eglGetProcAddress");
}

void set_swap_interval_function(void* function) {
    set_swap_interval = reinterpret_cast<SetSwapInterval>(function);
}

void riptide_apply(void* window) {
    static int applied = -1;
    int want = enabled("riptide") ? 0 : 1;
    // only on the window class whose method this is: its vtable's swap interval slot must hold it
    if (!set_swap_interval || !window || want == applied || (*static_cast<void***>(window))[14] != reinterpret_cast<void*>(set_swap_interval))
        return;
    set_swap_interval(window, want);
    applied = want;
    __android_log_print(ANDROID_LOG_INFO, CLIENT_NAME, "Riptide %s", want ? "off" : "on");
}

}  // namespace mods
