// The Windows entry point. The launcher loads the DLL into Minecraft; it hooks
// DXGI's Present to draw the menu and HUDs over each frame, and the game window's
// messages for input. The game draws with Direct3D 12, so the menu draws with
// Direct3D 11 on top of the game's own D3D12 queue (D3D11On12); a game on D3D11
// is drawn on directly.

#include <windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <d3d11on12.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

#include "config.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "log.h"
#include "menu.h"
#include "mods.h"

namespace {

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using ExecuteFn = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
PresentFn real_present = nullptr;
ResizeBuffersFn real_resize = nullptr;
ExecuteFn real_execute = nullptr;

// the game's direct command queue, which D3D11On12 has to share
ID3D12CommandQueue* game_queue = nullptr;

struct {
    IDXGISwapChain* chain = nullptr;
    bool d3d12 = false;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    ID3D11On12Device* on12 = nullptr;
    std::vector<ID3D11Resource*> buffers;  // the swap chain's buffers, wrapped for D3D11 when on D3D12
    std::vector<ID3D11RenderTargetView*> targets;
} gfx;

HWND window = nullptr;
WNDPROC real_wndproc = nullptr;

template <typename T>
void release(T*& p) {
    if (p)
        p->Release();
    p = nullptr;
}

void release_targets() {
    for (auto*& t : gfx.targets)
        release(t);
    for (auto*& b : gfx.buffers)
        release(b);
    gfx.targets.clear();
    gfx.buffers.clear();
    if (gfx.context)
        gfx.context->Flush();
}

bool make_targets(IDXGISwapChain* chain) {
    DXGI_SWAP_CHAIN_DESC desc{};
    chain->GetDesc(&desc);
    UINT count = gfx.d3d12 ? desc.BufferCount : 1;
    for (UINT i = 0; i < count; i++) {
        ID3D11Resource* buffer = nullptr;
        if (gfx.d3d12) {
            ID3D12Resource* resource = nullptr;
            if (FAILED(chain->GetBuffer(i, IID_PPV_ARGS(&resource))))
                return false;
            D3D11_RESOURCE_FLAGS flags{D3D11_BIND_RENDER_TARGET};
            HRESULT hr = gfx.on12->CreateWrappedResource(resource, &flags, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_PRESENT, IID_PPV_ARGS(&buffer));
            resource->Release();
            if (FAILED(hr))
                return false;
        } else if (FAILED(chain->GetBuffer(0, IID_PPV_ARGS(&buffer)))) {
            return false;
        }
        ID3D11RenderTargetView* target = nullptr;
        if (FAILED(gfx.device->CreateRenderTargetView(buffer, nullptr, &target))) {
            buffer->Release();
            return false;
        }
        gfx.buffers.push_back(buffer);
        gfx.targets.push_back(target);
    }
    return true;
}

bool make_device(IDXGISwapChain* chain) {
    ID3D12Device* d12 = nullptr;
    if (SUCCEEDED(chain->GetDevice(IID_PPV_ARGS(&d12)))) {
        // the queue shows up with the game's first ExecuteCommandLists
        if (!game_queue) {
            d12->Release();
            return false;
        }
        IUnknown* queues[] = {game_queue};
        HRESULT hr = D3D11On12CreateDevice(d12, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, queues, 1, 0, &gfx.device, &gfx.context, nullptr);
        d12->Release();
        if (FAILED(hr) || FAILED(gfx.device->QueryInterface(IID_PPV_ARGS(&gfx.on12))))
            return false;
        gfx.d3d12 = true;
    } else if (SUCCEEDED(chain->GetDevice(IID_PPV_ARGS(&gfx.device)))) {
        gfx.device->GetImmediateContext(&gfx.context);
    } else {
        return false;
    }
    gfx.chain = chain;
    log_info("Drawing over Direct3D %d", gfx.d3d12 ? 12 : 11);
    return true;
}

const menu::Renderer dx11_renderer{
        [] { return ImGui_ImplDX11_Init(gfx.device, gfx.context); },
        [] { ImGui_ImplDX11_NewFrame(); },
        [](ImDrawData* data) { ImGui_ImplDX11_RenderDrawData(data); },
        [] {
            ImGui_ImplDX11_InvalidateDeviceObjects();
            ImGui_ImplDX11_CreateDeviceObjects();
        },
        // ponytail: no title screen on Windows yet, so the pack's mark is never looked for
        [](float, float, uint8_t rgba[4]) { rgba[0] = rgba[1] = rgba[2] = rgba[3] = 0; },
};

HRESULT STDMETHODCALLTYPE present(IDXGISwapChain* chain, UINT sync, UINT flags) {
    if (!gfx.device && !make_device(chain))
        return real_present(chain, sync, flags);
    // the menu draws on the swap chain it was made for
    if (chain == gfx.chain && (gfx.targets.size() || make_targets(chain))) {
        UINT index = 0;
        if (gfx.d3d12) {
            IDXGISwapChain3* chain3 = nullptr;
            if (SUCCEEDED(chain->QueryInterface(IID_PPV_ARGS(&chain3)))) {
                index = chain3->GetCurrentBackBufferIndex();
                chain3->Release();
            }
        }
        if (index < gfx.targets.size()) {
            if (gfx.d3d12)
                gfx.on12->AcquireWrappedResources(&gfx.buffers[index], 1);
            gfx.context->OMSetRenderTargets(1, &gfx.targets[index], nullptr);
            DXGI_SWAP_CHAIN_DESC desc{};
            chain->GetDesc(&desc);
            menu::frame(int(desc.BufferDesc.Width), int(desc.BufferDesc.Height));
            if (gfx.d3d12)
                gfx.on12->ReleaseWrappedResources(&gfx.buffers[index], 1);
            gfx.context->Flush();
        }
    }
    return real_present(chain, sync, flags);
}

HRESULT STDMETHODCALLTYPE resize_buffers(IDXGISwapChain* chain, UINT count, UINT w, UINT h, DXGI_FORMAT format, UINT flags) {
    if (chain == gfx.chain)
        release_targets();
    return real_resize(chain, count, w, h, format, flags);
}

void STDMETHODCALLTYPE execute(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists) {
    if (!game_queue && queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
        game_queue = queue;
    real_execute(queue, count, lists);
}

// --- input --------------------------------------------------------------------

// Key actions as the core expects them, after mcpelauncher's
constexpr int PRESS = 0, RELEASE = 2;

// the core's codes are Windows' virtual keys, with Right Shift as 16 | 256 like mcpelauncher reports it
int key_code(WPARAM vk, LPARAM lp) {
    if (vk == VK_SHIFT && MapVirtualKeyA((lp >> 16) & 0xff, MAPVK_VSC_TO_VK_EX) == VK_RSHIFT)
        return VK_SHIFT | 256;
    return int(vk);
}

// The game hides the cursor while a world is played with no screen open
bool mouse_locked() {
    CURSORINFO info{sizeof info};
    return GetCursorInfo(&info) && !(info.flags & CURSOR_SHOWING);
}

LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    bool eaten = false;
    double x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
    switch (msg) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        eaten = menu::key(key_code(wp, lp), PRESS);
        break;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        eaten = menu::key(key_code(wp, lp), RELEASE);
        break;
    case WM_LBUTTONDOWN: eaten = menu::mouse_button(x, y, 1, PRESS); break;
    case WM_LBUTTONUP: eaten = menu::mouse_button(x, y, 1, RELEASE); break;
    case WM_RBUTTONDOWN: eaten = menu::mouse_button(x, y, 2, PRESS); break;
    case WM_RBUTTONUP: eaten = menu::mouse_button(x, y, 2, RELEASE); break;
    case WM_MOUSEMOVE:
        if (!mouse_locked())
            eaten = menu::mouse_move(x, y, false);
        break;
    case WM_MOUSEWHEEL:
        eaten = menu::scroll(0, GET_WHEEL_DELTA_WPARAM(wp) / double(WHEEL_DELTA));
        break;
    case WM_INPUT: {
        // the game turns the camera from raw mouse motion; the menu reads it while the cursor is hidden
        RAWINPUT raw{};
        UINT size = sizeof raw;
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) != UINT(-1) &&
            raw.header.dwType == RIM_TYPEMOUSE && !(raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) && mouse_locked())
            eaten = menu::mouse_move(raw.data.mouse.lLastX, raw.data.mouse.lLastY, true);
        break;
    }
    default:
        break;
    }
    return eaten ? 0 : CallWindowProcW(real_wndproc, hwnd, msg, wp, lp);
}

// --- finding the hooks ----------------------------------------------------------

void patch_slot(void** slot, void* hook, void** real) {
    DWORD old;
    VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &old);
    *real = *slot;
    *slot = hook;
    VirtualProtect(slot, sizeof *slot, old, &old);
}

// Swap chains and queues of one kind share a vtable, so a throwaway pair made on a
// hidden window gives the slots every swap chain and queue uses
bool hook_dxgi() {
    WNDCLASSEXW wc{sizeof wc, 0, DefWindowProcW, 0, 0, GetModuleHandleW(nullptr), nullptr, nullptr, nullptr, nullptr, L"TalazDummy"};
    RegisterClassExW(&wc);
    HWND dummy = CreateWindowW(L"TalazDummy", L"", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);
    bool done = false;
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    IDXGIFactory4* factory = nullptr;
    IDXGISwapChain1* chain = nullptr;
    D3D12_COMMAND_QUEUE_DESC qd{D3D12_COMMAND_LIST_TYPE_DIRECT};
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.BufferCount = 2;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.SampleDesc.Count = 1;
    if (SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))) &&
        SUCCEEDED(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue))) && SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->CreateSwapChainForHwnd(queue, dummy, &sd, nullptr, nullptr, &chain))) {
        void** chain_vt = *reinterpret_cast<void***>(chain);
        void** queue_vt = *reinterpret_cast<void***>(queue);
        // IDXGISwapChain: Present is slot 8, ResizeBuffers 13; ID3D12CommandQueue: ExecuteCommandLists is 10
        patch_slot(chain_vt + 8, reinterpret_cast<void*>(&present), reinterpret_cast<void**>(&real_present));
        patch_slot(chain_vt + 13, reinterpret_cast<void*>(&resize_buffers), reinterpret_cast<void**>(&real_resize));
        patch_slot(queue_vt + 10, reinterpret_cast<void*>(&execute), reinterpret_cast<void**>(&real_execute));
        done = true;
    }
    release(chain);
    release(factory);
    release(queue);
    release(device);
    DestroyWindow(dummy);
    UnregisterClassW(L"TalazDummy", wc.hInstance);
    return done;
}

BOOL CALLBACK find_window(HWND hwnd, LPARAM) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER))
        return TRUE;
    window = hwnd;
    return FALSE;
}

DWORD WINAPI start(void*) {
    config::declare("key.menu", VK_SHIFT | 256);
    mods::declare();
    config::load();
    // the game's window comes up a little after the process starts
    for (int i = 0; i < 300 && !window; i++, Sleep(100))
        EnumWindows(find_window, 0);
    if (!window) {
        log_error("Couldn't find the game's window");
        return 0;
    }
    menu::set_renderer(dx11_renderer);
    menu::connect(mouse_locked, nullptr);
    mods::install();
    if (!hook_dxgi()) {
        log_error("Couldn't hook Direct3D");
        return 0;
    }
    real_wndproc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&wndproc)));
    log_info("Talaz is in");
    return 0;
}

}  // namespace

void log_write(const char* level, const char* format, ...) {
    static FILE* file = nullptr;
    if (!file) {
        char path[MAX_PATH];
        HMODULE self = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(&log_write), &self);
        GetModuleFileNameA(self, path, MAX_PATH);
        std::string name = path;
        name = name.substr(0, name.rfind('.')) + ".log";
        file = std::fopen(name.c_str(), "w");
        if (!file)
            return;
    }
    SYSTEMTIME t;
    GetLocalTime(&t);
    std::fprintf(file, "%02d:%02d:%02d %s [%s] ", t.wHour, t.wMinute, t.wSecond, level, CLIENT_NAME);
    va_list args;
    va_start(args, format);
    std::vfprintf(file, format, args);
    va_end(args);
    std::fputc('\n', file);
    std::fflush(file);
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH)
        CloseHandle(CreateThread(nullptr, 0, start, nullptr, 0, nullptr));
    return TRUE;
}
