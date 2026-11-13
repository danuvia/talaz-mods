// Motion Blur's drawing on Windows, with Direct3D 11 over the finished frame. How much
// to blur is worked out in mods/motionblur.cpp, the same as on Android.
// ponytail: Windows has no world pass hooked yet, so the game's own HUD is blurred with the world; move this into the world pass once there's one

#include <d3d11.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <cstring>

#include "config.h"
#include "log.h"
#include "shared.h"

namespace {

const char* SHADERS = R"(
cbuffer Blur : register(b0) { float2 offset; int samples; float keep; };
Texture2D frame : register(t0);
SamplerState linear_clamp : register(s0);
struct V { float4 pos : SV_Position; float2 uv : TEXCOORD; };
V vs(uint id : SV_VertexID) {
    V v;
    v.uv = float2((id << 1) & 2, id & 2);
    v.pos = float4(v.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return v;
}
float4 camera(V v) : SV_Target {
    float3 sum = 0;
    for (int i = 0; i < samples; i++) {
        float t = float(i) / float(samples - 1) - 0.5;
        sum += frame.Sample(linear_clamp, saturate(v.uv + offset * t)).rgb;
    }
    return float4(sum / samples, 1);
}
float4 trail(V v) : SV_Target { return float4(frame.Sample(linear_clamp, v.uv).rgb, keep); }
)";

struct Constants {
    float step[2];
    int samples;
    float keep;
};

ID3D11VertexShader* vs = nullptr;
ID3D11PixelShader *camera_ps = nullptr, *trail_ps = nullptr;
ID3D11Buffer* constants = nullptr;
ID3D11SamplerState* sampler = nullptr;
ID3D11BlendState* keep_blend = nullptr;
bool broken = false;

// a copy of the frame the shaders read, and the trail's last frame
struct Copy {
    ID3D11Texture2D* texture = nullptr;
    ID3D11ShaderResourceView* view = nullptr;
    D3D11_TEXTURE2D_DESC desc{};
} frame_copy, last;

ID3DBlob* compile(const char* entry, const char* target) {
    ID3DBlob *code = nullptr, *errors = nullptr;
    if (FAILED(D3DCompile(SHADERS, strlen(SHADERS), "motionblur", nullptr, nullptr, entry, target, 0, 0, &code, &errors))) {
        log_error("Motion Blur's shader didn't compile: %s", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "");
        code = nullptr;
    }
    if (errors)
        errors->Release();
    return code;
}

bool setup(ID3D11Device* device) {
    ID3DBlob *v = compile("vs", "vs_4_0"), *c = compile("camera", "ps_4_0"), *t = compile("trail", "ps_4_0");
    bool ok = v && c && t && SUCCEEDED(device->CreateVertexShader(v->GetBufferPointer(), v->GetBufferSize(), nullptr, &vs)) &&
              SUCCEEDED(device->CreatePixelShader(c->GetBufferPointer(), c->GetBufferSize(), nullptr, &camera_ps)) &&
              SUCCEEDED(device->CreatePixelShader(t->GetBufferPointer(), t->GetBufferSize(), nullptr, &trail_ps));
    for (auto* blob : {v, c, t})
        if (blob)
            blob->Release();
    D3D11_BUFFER_DESC bd{sizeof(Constants), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER};
    D3D11_SAMPLER_DESC sd{D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP};
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    // the last frame over this one by keep, colour only
    D3D11_BLEND_DESC blend{};
    auto& rt = blend.RenderTarget[0];
    rt = {TRUE, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_OP_ADD, D3D11_BLEND_ZERO, D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD,
          D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE};
    return ok && SUCCEEDED(device->CreateBuffer(&bd, nullptr, &constants)) && SUCCEEDED(device->CreateSamplerState(&sd, &sampler)) &&
           SUCCEEDED(device->CreateBlendState(&blend, &keep_blend));
}

// Copies the frame into copy, making it again when the frame's size or format changed
bool copy_frame(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* buffer, Copy& copy) {
    D3D11_TEXTURE2D_DESC desc{};
    buffer->GetDesc(&desc);
    bool fresh = !copy.texture || desc.Width != copy.desc.Width || desc.Height != copy.desc.Height || desc.Format != copy.desc.Format;
    if (fresh) {
        if (copy.view)
            copy.view->Release();
        if (copy.texture)
            copy.texture->Release();
        copy = {};
        D3D11_TEXTURE2D_DESC td{desc.Width, desc.Height, 1, 1, desc.Format, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE};
        if (FAILED(device->CreateTexture2D(&td, nullptr, &copy.texture)) || FAILED(device->CreateShaderResourceView(copy.texture, nullptr, &copy.view)))
            return false;
        copy.desc = desc;
    }
    context->CopyResource(copy.texture, buffer);
    return true;
}

void draw(ID3D11DeviceContext* context, ID3D11RenderTargetView* target, ID3D11PixelShader* ps, ID3D11ShaderResourceView* source,
          ID3D11BlendState* blend, const Constants& c, const D3D11_TEXTURE2D_DESC& size) {
    D3D11_VIEWPORT vp{0, 0, float(size.Width), float(size.Height), 0, 1};
    context->UpdateSubresource(constants, 0, nullptr, &c, 0, 0);
    context->OMSetRenderTargets(1, &target, nullptr);
    context->OMSetBlendState(blend, nullptr, 0xffffffff);
    context->OMSetDepthStencilState(nullptr, 0);
    context->RSSetState(nullptr);
    context->RSSetViewports(1, &vp);
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vs, nullptr, 0);
    context->PSSetShader(ps, nullptr, 0);
    context->PSSetConstantBuffers(0, 1, &constants);
    context->PSSetSamplers(0, 1, &sampler);
    context->PSSetShaderResources(0, 1, &source);
    context->Draw(3, 0);
    // the copy is written again next frame, so it can't stay bound for reading
    ID3D11ShaderResourceView* none = nullptr;
    context->PSSetShaderResources(0, 1, &none);
}

}  // namespace

namespace mods {

// Blurs the frame in buffer through target. in_world is false off a world, where the
// trail starts over so menus don't leave one
void motionblur_dx11(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Resource* buffer, ID3D11RenderTargetView* target, bool in_world) {
    if (!in_world || !motionblur_active()) {
        last.desc.Width = 0;
        return;
    }
    if (broken)
        return;
    if (!vs && !setup(device)) {
        broken = true;
        return;
    }
    ID3D11Texture2D* texture = nullptr;
    if (FAILED(buffer->QueryInterface(IID_PPV_ARGS(&texture))))
        return;
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    Constants c{};
    if (config::get("motionblur.camera") != 0) {
        if (motionblur_step(int(desc.Width), int(desc.Height), c.step[0], c.step[1]) && copy_frame(device, context, texture, frame_copy)) {
            c.samples = std::clamp(int(config::get("motionblur.samples")), 2, 64);
            draw(context, target, camera_ps, frame_copy.view, nullptr, c, desc);
        }
    } else {
        bool had_last = last.texture && last.desc.Width == desc.Width && last.desc.Height == desc.Height && last.desc.Format == desc.Format;
        if (had_last) {
            c.keep = motionblur_keep();
            draw(context, target, trail_ps, last.view, keep_blend, c, desc);
        }
        copy_frame(device, context, texture, last);
    }
    texture->Release();
}

}  // namespace mods
