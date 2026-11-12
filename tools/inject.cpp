// talaz-inject: loads libtalaz_bedrock.dll, next to this program, into a running
// Minecraft. A stand-in for the Windows launcher while the Windows side is tested.

#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>
#include <string>

int wmain() {
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring dll = self;
    dll = dll.substr(0, dll.rfind(L'\\') + 1) + L"libtalaz_bedrock.dll";
    if (GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::printf("libtalaz_bedrock.dll isn't next to this program\n");
        return 1;
    }

    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W entry{sizeof entry};
    for (BOOL ok = Process32FirstW(snap, &entry); ok; ok = Process32NextW(snap, &entry))
        if (!_wcsicmp(entry.szExeFile, L"Minecraft.Windows.exe"))
            pid = entry.th32ProcessID;
    CloseHandle(snap);
    if (!pid) {
        std::printf("Start Minecraft first\n");
        return 1;
    }

    HANDLE game = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (!game) {
        std::printf("Couldn't open Minecraft (error %lu)\n", GetLastError());
        return 1;
    }
    size_t bytes = (dll.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(game, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    WriteProcessMemory(game, remote, dll.c_str(), bytes, nullptr);
    auto load = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    HANDLE thread = CreateRemoteThread(game, nullptr, 0, load, remote, 0, nullptr);
    if (!thread) {
        std::printf("Couldn't start the loader in Minecraft (error %lu)\n", GetLastError());
        return 1;
    }
    WaitForSingleObject(thread, 10000);
    DWORD loaded = 0;
    GetExitCodeThread(thread, &loaded);
    std::printf(loaded ? "Talaz is loaded. Right Shift in a world opens the menu\n" : "Minecraft refused the DLL\n");
    CloseHandle(thread);
    VirtualFreeEx(game, remote, 0, MEM_RELEASE);
    CloseHandle(game);
    return loaded ? 0 : 1;
}
