#pragma once

#include <cstddef>

// Fonts are baked into the library so the mod stays one file. FONT_DIR
// comes from CMake.
#ifdef _WIN32
#define EMBED_SECTION ".section .rdata,\"dr\"\n"
#define EMBED_BACK ".text\n"
#else
#define EMBED_SECTION ".section .rodata\n"
#define EMBED_BACK ".previous\n"
#endif
#define EMBED(name, path)                                                                                    \
    __asm__(EMBED_SECTION ".balign 16\n" #name ":\n.incbin \"" path "\"\n" #name "_end:\n.byte 0\n" EMBED_BACK); \
    extern "C" const unsigned char name[], name##_end[];

struct Asset {
    const unsigned char* data;
    size_t size;
};

#define ASSET(name) (Asset{name, size_t(name##_end - name)})
