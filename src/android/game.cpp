// The game's library: finding things in it, hooking its imports, and installing
// every mod's hooks once it is loaded. Also what the core fixes in the game itself.

#include "log.h"
#include <dlfcn.h>
#include <link.h>

#include <cstring>

#include "mods.h"
#include "hooks.h"

namespace mods {

std::vector<Segment> segments_of(void* lib) {
    struct Search {
        void* lib;
        std::vector<Segment> out;
    } search{lib, {}};
    dl_iterate_phdr(
            [](dl_phdr_info* info, size_t, void* data) {
                auto* s = static_cast<Search*>(data);
                void* h = dlopen(info->dlpi_name, RTLD_NOLOAD);
                if (h)
                    dlclose(h);
                if (h != s->lib)
                    return 0;
                for (int i = 0; i < info->dlpi_phnum; i++) {
                    const ElfW(Phdr)& ph = info->dlpi_phdr[i];
                    if (ph.p_type == PT_LOAD && (ph.p_flags & PF_R))
                        s->out.push_back({reinterpret_cast<std::byte*>(info->dlpi_addr + ph.p_vaddr), ph.p_memsz,
                                          (ph.p_flags & PF_X) != 0});
                }
                return 1;
            },
            &search);
    return search.out;
}

std::byte* find_string(const std::vector<Segment>& segs, const void* needle, size_t n) {
    for (const Segment& seg : segs)
        if (void* hit = memmem(seg.begin, seg.size, needle, n))
            return static_cast<std::byte*>(hit);
    return nullptr;
}

std::byte* find_pointer(const std::vector<Segment>& segs, const void* value) {
    for (const Segment& seg : segs) {
        auto* words = reinterpret_cast<const void* const*>(seg.begin);
        for (size_t i = 0; i < seg.size / sizeof(void*); i++)
            if (words[i] == value)
                return seg.begin + i * sizeof(void*);
    }
    return nullptr;
}

bool in_code(const std::vector<Segment>& segs, const void* p) {
    for (const Segment& seg : segs)
        if (seg.exec && p >= seg.begin && p < seg.begin + seg.size)
            return true;
    return false;
}

// Rewrites the import's slot in the game's table of imports
void* hook_import(const char* name, void* replacement, Patch patch) {
    struct Search {
        const char* name;
        void* slot = nullptr;
    } search{name};
    dl_iterate_phdr(
            [](dl_phdr_info* info, size_t, void* data) {
                auto* s = static_cast<Search*>(data);
                if (!info->dlpi_name || !std::strstr(info->dlpi_name, "libminecraftpe.so"))
                    return 0;
                const ElfW(Sym)* symtab = nullptr;
                const char* strtab = nullptr;
                const ElfW(Rela)* plt = nullptr;
                size_t pltsize = 0;
                for (int i = 0; i < info->dlpi_phnum; i++) {
                    if (info->dlpi_phdr[i].p_type != PT_DYNAMIC)
                        continue;
                    for (auto* d = reinterpret_cast<const ElfW(Dyn)*>(info->dlpi_addr + info->dlpi_phdr[i].p_vaddr); d->d_tag != DT_NULL; d++) {
                        if (d->d_tag == DT_SYMTAB)
                            symtab = reinterpret_cast<const ElfW(Sym)*>(info->dlpi_addr + d->d_un.d_ptr);
                        else if (d->d_tag == DT_STRTAB)
                            strtab = reinterpret_cast<const char*>(info->dlpi_addr + d->d_un.d_ptr);
                        else if (d->d_tag == DT_JMPREL)
                            plt = reinterpret_cast<const ElfW(Rela)*>(info->dlpi_addr + d->d_un.d_ptr);
                        else if (d->d_tag == DT_PLTRELSZ)
                            pltsize = d->d_un.d_val;
                    }
                }
                if (!symtab || !strtab || !plt)
                    return 1;
                for (size_t i = 0; i < pltsize / sizeof(ElfW(Rela)); i++)
                    if (std::strcmp(strtab + symtab[ELF64_R_SYM(plt[i].r_info)].st_name, s->name) == 0)
                        s->slot = reinterpret_cast<void*>(info->dlpi_addr + plt[i].r_offset);
                return 1;
            },
            &search);
    if (!search.slot)
        return nullptr;
    void* original = *static_cast<void**>(search.slot);
    patch(search.slot, &replacement, sizeof replacement);
    return original;
}

namespace {

// The game builds a texture each frame from an image stored inside it that it
// can't read under mcpelauncher, and logs a failure every time. Point it at an
// empty PNG instead: nothing it showed goes away, and the retries stop
// ponytail: the offsets are 1.21.114.1's, checked against the instructions before patching. Find them by pattern for other versions
const unsigned char blank_png[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00, 0x0b, 0x49,
    0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x60, 0x00, 0x02, 0x00, 0x00, 0x05, 0x00, 0x01, 0x7a, 0x5e, 0xab, 0x3f, 0x00,
    0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};

void fix_unreadable_image(std::byte* base, Patch patch) {
    auto* at = reinterpret_cast<uint32_t*>(base + 0x64a1978);
    // adrp x1; mov w2, #0x530c; add x1, x1; ...; movk w2, #1, lsl #16
    if (at[0] != 0x90050d21 || at[1] != 0x528a6182 || at[2] != 0x913d0021 || at[5] != 0x72a00022)
        return;
    auto pc = reinterpret_cast<uintptr_t>(at), png = reinterpret_cast<uintptr_t>(blank_png);
    int64_t pages = int64_t(png >> 12) - int64_t(pc >> 12);
    if (pages >= (1 << 20) || pages < -(1 << 20))
        return;
    uint32_t adrp = 0x90000001 | uint32_t(pages & 3) << 29 | uint32_t((pages >> 2) & 0x7ffff) << 5;
    uint32_t add = 0x91000021 | uint32_t(png & 0xfff) << 10;
    uint32_t len = sizeof blank_png;
    uint32_t code[3] = {adrp, 0x52800002 | (len & 0xffff) << 5, add};
    uint32_t high = 0x72a00002 | (len >> 16) << 5;
    patch(at, code, sizeof code);
    patch(at + 5, &high, sizeof high);
    log_info("Pointed the unreadable built-in image at a blank one");
}

// The game's "hide paper doll" setting, caught as the game registers it: the
// Options object's registerOption is called once per setting, and the one called
// from the paper doll's line is this setting. Its value is the byte at 0x10
// ponytail: the offsets are 1.21.114.1's, checked before patching. Unknown versions keep the HUDs beside the paper doll's spot
const unsigned char* hide_paper_doll = nullptr;
void* (*real_register_option)(void*, void*) = nullptr;
uintptr_t paper_doll_call = 0;

void* register_option_hook(void* options, void* option) {
    void* registered = real_register_option(options, option);
    if (reinterpret_cast<uintptr_t>(__builtin_return_address(0)) == paper_doll_call)
        hide_paper_doll = static_cast<const unsigned char*>(registered) + 0x10,
        log_info("Found the paper doll setting");
    return registered;
}

void watch_paper_doll(std::byte* base, Patch patch) {
    auto** slot = reinterpret_cast<void**>(base + 0x1003e290 + 0x80);
    if (*slot != base + 0x6c6a868)
        return;
    real_register_option = reinterpret_cast<decltype(real_register_option)>(*slot);
    paper_doll_call = reinterpret_cast<uintptr_t>(base + 0x6c7f2ac);
    void* hook = reinterpret_cast<void*>(&register_option_hook);
    patch(slot, &hook, sizeof hook);
}

}  // namespace

bool paper_doll() {
    return !hide_paper_doll || *hide_paper_doll == 0;
}

void install() {
    void* game = dlopen("libminecraftpe.so", 0);
    void* api = dlopen("libmcpelauncher_mod.so", 0);
    if (!game || !api)
        return;
    auto patch = reinterpret_cast<Patch>(dlsym(api, "mcpelauncher_patch"));
    auto* major = static_cast<int*>(dlsym(api, "mcpelauncher_package_version_major"));
    auto* minor = static_cast<int*>(dlsym(api, "mcpelauncher_package_version_minor"));
    auto* patch_v = static_cast<int*>(dlsym(api, "mcpelauncher_package_version_patch"));
    if (!patch || !major || !minor || !patch_v)
        return;
    std::vector<Segment> segs = segments_of(game);
    if (!segs.empty() && segs.front().exec) {
        fix_unreadable_image(segs.front().begin, patch);
        watch_paper_doll(segs.front().begin, patch);
    }
    world_install(patch);
    riptide_install(patch);
    nightvision_install(segs, patch);
    zoom_install(segs, patch, *major, *minor, *patch_v);
}

}  // namespace mods
