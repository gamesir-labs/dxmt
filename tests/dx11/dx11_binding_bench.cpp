#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define UNICODE
#include <windows.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>

namespace {

template <typename T>
void safe_release(T *&ptr) {
  if (ptr) {
    ptr->Release();
    ptr = nullptr;
  }
}

struct Options {
  int frames = 300;
  int warmup = 30;
  int draws = 2500;
  int cb_slots = 8;
  int srv_slots = 16;
  int sampler_slots = 8;
  int variants = 64;
  int present = 1;
  int width = 64;
  int height = 64;
  std::string mode = "mixed";
};

bool has_arg(int argc, char **argv, const char *name) {
  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], name) == 0)
      return true;
  }
  return false;
}

int int_arg(int argc, char **argv, const char *name, int fallback) {
  for (int i = 1; i + 1 < argc; i++) {
    if (std::strcmp(argv[i], name) == 0)
      return std::atoi(argv[i + 1]);
  }
  return fallback;
}

std::string string_arg(int argc, char **argv, const char *name, const std::string &fallback) {
  for (int i = 1; i + 1 < argc; i++) {
    if (std::strcmp(argv[i], name) == 0)
      return argv[i + 1];
  }
  return fallback;
}

void print_usage() {
  std::puts("dx11_binding_bench options:");
  std::puts("  --mode cb|srv|sampler|mixed  Resource binding churn mode");
  std::puts("  --frames N                   Measured frames, default 300");
  std::puts("  --warmup N                   Warmup frames, default 30");
  std::puts("  --draws N                    Draws per frame, default 2500");
  std::puts("  --cb-slots N                 Pixel constant buffer slots, max 8");
  std::puts("  --srv-slots N                Pixel SRV slots, max 16");
  std::puts("  --sampler-slots N            Pixel sampler slots, max 8");
  std::puts("  --variants N                 Distinct resource variants per slot, default 64");
  std::puts("  --present 0|1                Present each frame, default 1");
}

const char *kShader = R"HLSL(
cbuffer C0 : register(b0) { float4 c0; };
cbuffer C1 : register(b1) { float4 c1; };
cbuffer C2 : register(b2) { float4 c2; };
cbuffer C3 : register(b3) { float4 c3; };
cbuffer C4 : register(b4) { float4 c4; };
cbuffer C5 : register(b5) { float4 c5; };
cbuffer C6 : register(b6) { float4 c6; };
cbuffer C7 : register(b7) { float4 c7; };

Texture2D<float4> t0  : register(t0);
Texture2D<float4> t1  : register(t1);
Texture2D<float4> t2  : register(t2);
Texture2D<float4> t3  : register(t3);
Texture2D<float4> t4  : register(t4);
Texture2D<float4> t5  : register(t5);
Texture2D<float4> t6  : register(t6);
Texture2D<float4> t7  : register(t7);
Texture2D<float4> t8  : register(t8);
Texture2D<float4> t9  : register(t9);
Texture2D<float4> t10 : register(t10);
Texture2D<float4> t11 : register(t11);
Texture2D<float4> t12 : register(t12);
Texture2D<float4> t13 : register(t13);
Texture2D<float4> t14 : register(t14);
Texture2D<float4> t15 : register(t15);

SamplerState s0 : register(s0);
SamplerState s1 : register(s1);
SamplerState s2 : register(s2);
SamplerState s3 : register(s3);
SamplerState s4 : register(s4);
SamplerState s5 : register(s5);
SamplerState s6 : register(s6);
SamplerState s7 : register(s7);

struct VSOut {
  float4 pos : SV_Position;
  float2 uv  : TEXCOORD0;
};

VSOut vs_main(uint id : SV_VertexID) {
  float2 p[3] = { float2(-1.0, -1.0), float2(-1.0, 3.0), float2(3.0, -1.0) };
  VSOut o;
  o.pos = float4(p[id], 0.0, 1.0);
  o.uv = p[id] * 0.5 + 0.5;
  return o;
}

float4 ps_main(VSOut input) : SV_Target {
  float4 cb = c0 + c1 + c2 + c3 + c4 + c5 + c6 + c7;
  float4 tex = 0.0;
  tex += t0.SampleLevel(s0, input.uv, 0);
  tex += t1.SampleLevel(s1, input.uv, 0);
  tex += t2.SampleLevel(s2, input.uv, 0);
  tex += t3.SampleLevel(s3, input.uv, 0);
  tex += t4.SampleLevel(s4, input.uv, 0);
  tex += t5.SampleLevel(s5, input.uv, 0);
  tex += t6.SampleLevel(s6, input.uv, 0);
  tex += t7.SampleLevel(s7, input.uv, 0);
  tex += t8.SampleLevel(s0, input.uv, 0);
  tex += t9.SampleLevel(s1, input.uv, 0);
  tex += t10.SampleLevel(s2, input.uv, 0);
  tex += t11.SampleLevel(s3, input.uv, 0);
  tex += t12.SampleLevel(s4, input.uv, 0);
  tex += t13.SampleLevel(s5, input.uv, 0);
  tex += t14.SampleLevel(s6, input.uv, 0);
  tex += t15.SampleLevel(s7, input.uv, 0);
  return frac(cb * 0.01 + tex * 0.02);
}
)HLSL";

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  if (msg == WM_DESTROY) {
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

HWND create_window(HINSTANCE instance, int width, int height) {
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = wnd_proc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = L"DXMTBindingBenchWindow";
  RegisterClassExW(&wc);

  RECT rect = {0, 0, width, height};
  AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0);
  return CreateWindowExW(
      0, wc.lpszClassName, L"DXMT Binding Benchmark", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
      CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
      nullptr, nullptr, instance, nullptr
  );
}

bool compile_shader(const char *entry, const char *target, ID3DBlob **blob) {
  ID3DBlob *errors = nullptr;
  HRESULT hr = D3DCompile(kShader, std::strlen(kShader), "binding_bench.hlsl", nullptr, nullptr, entry, target, 0, 0, blob, &errors);
  if (FAILED(hr)) {
    if (errors) {
      std::fprintf(stderr, "%s\n", static_cast<const char *>(errors->GetBufferPointer()));
      errors->Release();
    }
    std::fprintf(stderr, "D3DCompile failed for %s (%08lx)\n", entry, static_cast<unsigned long>(hr));
    return false;
  }
  safe_release(errors);
  return true;
}

bool create_device(ID3D11Device1 **device, ID3D11DeviceContext1 **context) {
  ID3D11Device *base_device = nullptr;
  ID3D11DeviceContext *base_context = nullptr;
  D3D_FEATURE_LEVEL feature_levels[] = {D3D_FEATURE_LEVEL_11_0};
  HRESULT hr = D3D11CreateDevice(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
      feature_levels, ARRAYSIZE(feature_levels), D3D11_SDK_VERSION,
      &base_device, nullptr, &base_context
  );
  if (FAILED(hr)) {
    std::fprintf(stderr, "D3D11CreateDevice failed (%08lx)\n", static_cast<unsigned long>(hr));
    return false;
  }
  hr = base_device->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void **>(device));
  if (FAILED(hr)) {
    std::fprintf(stderr, "ID3D11Device1 query failed (%08lx)\n", static_cast<unsigned long>(hr));
    return false;
  }
  hr = base_context->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void **>(context));
  if (FAILED(hr)) {
    std::fprintf(stderr, "ID3D11DeviceContext1 query failed (%08lx)\n", static_cast<unsigned long>(hr));
    return false;
  }
  safe_release(base_context);
  safe_release(base_device);
  return true;
}

bool create_swapchain(ID3D11Device1 *device, HWND hwnd, int width, int height, IDXGISwapChain1 **swapchain) {
  IDXGIDevice1 *dxgi_device = nullptr;
  IDXGIAdapter *adapter = nullptr;
  IDXGIFactory2 *factory = nullptr;
  HRESULT hr = device->QueryInterface(__uuidof(IDXGIDevice1), reinterpret_cast<void **>(&dxgi_device));
  if (SUCCEEDED(hr))
    hr = dxgi_device->GetAdapter(&adapter);
  if (SUCCEEDED(hr))
    hr = adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void **>(&factory));
  if (FAILED(hr)) {
    std::fprintf(stderr, "DXGI factory creation failed (%08lx)\n", static_cast<unsigned long>(hr));
    return false;
  }

  DXGI_SWAP_CHAIN_DESC1 desc = {};
  desc.Width = width;
  desc.Height = height;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = 2;
  desc.Scaling = DXGI_SCALING_STRETCH;
  desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  hr = factory->CreateSwapChainForHwnd(device, hwnd, &desc, nullptr, nullptr, swapchain);
  safe_release(factory);
  safe_release(adapter);
  safe_release(dxgi_device);
  if (FAILED(hr)) {
    std::fprintf(stderr, "CreateSwapChainForHwnd failed (%08lx)\n", static_cast<unsigned long>(hr));
    return false;
  }
  return true;
}

bool create_render_target(ID3D11Device1 *device, IDXGISwapChain1 *swapchain, ID3D11RenderTargetView **rtv) {
  ID3D11Texture2D *backbuffer = nullptr;
  HRESULT hr = swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&backbuffer));
  if (SUCCEEDED(hr))
    hr = device->CreateRenderTargetView(backbuffer, nullptr, rtv);
  safe_release(backbuffer);
  if (FAILED(hr)) {
    std::fprintf(stderr, "CreateRenderTargetView failed (%08lx)\n", static_cast<unsigned long>(hr));
    return false;
  }
  return true;
}

bool create_constant_buffers(ID3D11Device1 *device, int slots, int variants, std::vector<ID3D11Buffer *> &buffers) {
  buffers.resize(slots * variants, nullptr);
  for (int i = 0; i < slots * variants; i++) {
    float data[4] = {float(i), float(i * 3), float(i * 7), 1.0f};
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(data);
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA init = {data, 0, 0};
    HRESULT hr = device->CreateBuffer(&desc, &init, &buffers[i]);
    if (FAILED(hr)) {
      std::fprintf(stderr, "CreateBuffer failed (%08lx)\n", static_cast<unsigned long>(hr));
      return false;
    }
  }
  return true;
}

bool create_srvs(ID3D11Device1 *device, int slots, int variants, std::vector<ID3D11ShaderResourceView *> &srvs) {
  srvs.resize(slots * variants, nullptr);
  for (int i = 0; i < slots * variants; i++) {
    uint32_t pixel = 0xff000000u | ((i * 37u) & 0xffu) | (((i * 73u) & 0xffu) << 8) | (((i * 109u) & 0xffu) << 16);
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = 1;
    desc.Height = 1;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init = {&pixel, sizeof(pixel), 0};
    ID3D11Texture2D *texture = nullptr;
    HRESULT hr = device->CreateTexture2D(&desc, &init, &texture);
    if (SUCCEEDED(hr))
      hr = device->CreateShaderResourceView(texture, nullptr, &srvs[i]);
    safe_release(texture);
    if (FAILED(hr)) {
      std::fprintf(stderr, "CreateShaderResourceView failed (%08lx)\n", static_cast<unsigned long>(hr));
      return false;
    }
  }
  return true;
}

bool create_samplers(ID3D11Device1 *device, int slots, int variants, std::vector<ID3D11SamplerState *> &samplers) {
  samplers.resize(slots * variants, nullptr);
  for (int i = 0; i < slots * variants; i++) {
    D3D11_SAMPLER_DESC desc = {};
    desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    desc.MinLOD = 0.0f;
    desc.MaxLOD = D3D11_FLOAT32_MAX;
    desc.MipLODBias = float(i % 4) * 0.125f;
    HRESULT hr = device->CreateSamplerState(&desc, &samplers[i]);
    if (FAILED(hr)) {
      std::fprintf(stderr, "CreateSamplerState failed (%08lx)\n", static_cast<unsigned long>(hr));
      return false;
    }
  }
  return true;
}

} // namespace

int main(int argc, char **argv) {
  if (has_arg(argc, argv, "--help") || has_arg(argc, argv, "-h")) {
    print_usage();
    return 0;
  }

  Options opt;
  opt.frames = std::max(1, int_arg(argc, argv, "--frames", opt.frames));
  opt.warmup = std::max(0, int_arg(argc, argv, "--warmup", opt.warmup));
  opt.draws = std::max(1, int_arg(argc, argv, "--draws", opt.draws));
  opt.cb_slots = std::clamp(int_arg(argc, argv, "--cb-slots", opt.cb_slots), 0, 8);
  opt.srv_slots = std::clamp(int_arg(argc, argv, "--srv-slots", opt.srv_slots), 0, 16);
  opt.sampler_slots = std::clamp(int_arg(argc, argv, "--sampler-slots", opt.sampler_slots), 0, 8);
  opt.variants = std::max(1, int_arg(argc, argv, "--variants", opt.variants));
  opt.present = int_arg(argc, argv, "--present", opt.present) ? 1 : 0;
  opt.mode = string_arg(argc, argv, "--mode", opt.mode);

  bool churn_cb = opt.mode == "cb" || opt.mode == "mixed";
  bool churn_srv = opt.mode == "srv" || opt.mode == "mixed";
  bool churn_sampler = opt.mode == "sampler" || opt.mode == "mixed";
  if (!churn_cb && !churn_srv && !churn_sampler) {
    std::fprintf(stderr, "Unknown mode: %s\n", opt.mode.c_str());
    print_usage();
    return 2;
  }

  HINSTANCE instance = GetModuleHandleW(nullptr);
  HWND hwnd = create_window(instance, opt.width, opt.height);
  if (!hwnd) {
    std::fprintf(stderr, "CreateWindow failed (%lu)\n", GetLastError());
    return 1;
  }

  ID3D11Device1 *device = nullptr;
  ID3D11DeviceContext1 *context = nullptr;
  IDXGISwapChain1 *swapchain = nullptr;
  ID3D11RenderTargetView *rtv = nullptr;
  ID3D11VertexShader *vs = nullptr;
  ID3D11PixelShader *ps = nullptr;
  ID3DBlob *vs_blob = nullptr;
  ID3DBlob *ps_blob = nullptr;

  if (!create_device(&device, &context) || !create_swapchain(device, hwnd, opt.width, opt.height, &swapchain) ||
      !create_render_target(device, swapchain, &rtv) || !compile_shader("vs_main", "vs_5_0", &vs_blob) ||
      !compile_shader("ps_main", "ps_5_0", &ps_blob)) {
    return 1;
  }
  HRESULT hr = device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &vs);
  if (SUCCEEDED(hr))
    hr = device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &ps);
  if (FAILED(hr)) {
    std::fprintf(stderr, "CreateShader failed (%08lx)\n", static_cast<unsigned long>(hr));
    return 1;
  }

  std::vector<ID3D11Buffer *> cbuffers;
  std::vector<ID3D11ShaderResourceView *> srvs;
  std::vector<ID3D11SamplerState *> samplers;
  if (!create_constant_buffers(device, std::max(1, opt.cb_slots), opt.variants, cbuffers) ||
      !create_srvs(device, std::max(1, opt.srv_slots), opt.variants, srvs) ||
      !create_samplers(device, std::max(1, opt.sampler_slots), opt.variants, samplers)) {
    return 1;
  }

  D3D11_VIEWPORT viewport = {0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
  context->RSSetViewports(1, &viewport);
  context->OMSetRenderTargets(1, &rtv, nullptr);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(vs, nullptr, 0);
  context->PSSetShader(ps, nullptr, 0);

  std::vector<ID3D11Buffer *> cb_bindings(std::max(1, opt.cb_slots), nullptr);
  std::vector<ID3D11ShaderResourceView *> srv_bindings(std::max(1, opt.srv_slots), nullptr);
  std::vector<ID3D11SamplerState *> sampler_bindings(std::max(1, opt.sampler_slots), nullptr);

  std::printf(
      "dx11_binding_bench mode=%s frames=%d warmup=%d draws=%d cb_slots=%d srv_slots=%d sampler_slots=%d variants=%d present=%d\n",
      opt.mode.c_str(), opt.frames, opt.warmup, opt.draws, opt.cb_slots, opt.srv_slots, opt.sampler_slots,
      opt.variants, opt.present
  );

  auto measured_start = std::chrono::steady_clock::time_point{};
  auto measured_end = std::chrono::steady_clock::time_point{};
  int total_frames = opt.warmup + opt.frames;
  for (int frame = 0; frame < total_frames; frame++) {
    MSG msg = {};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }

    if (frame == opt.warmup)
      measured_start = std::chrono::steady_clock::now();

    float clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    context->ClearRenderTargetView(rtv, clear);

    for (int draw = 0; draw < opt.draws; draw++) {
      if (churn_cb && opt.cb_slots > 0) {
        for (int slot = 0; slot < opt.cb_slots; slot++)
          cb_bindings[slot] = cbuffers[slot * opt.variants + ((frame * opt.draws + draw + slot) % opt.variants)];
        context->PSSetConstantBuffers(0, opt.cb_slots, cb_bindings.data());
      }
      if (churn_srv && opt.srv_slots > 0) {
        for (int slot = 0; slot < opt.srv_slots; slot++)
          srv_bindings[slot] = srvs[slot * opt.variants + ((frame * opt.draws + draw + slot) % opt.variants)];
        context->PSSetShaderResources(0, opt.srv_slots, srv_bindings.data());
      }
      if (churn_sampler && opt.sampler_slots > 0) {
        for (int slot = 0; slot < opt.sampler_slots; slot++)
          sampler_bindings[slot] = samplers[slot * opt.variants + ((frame * opt.draws + draw + slot) % opt.variants)];
        context->PSSetSamplers(0, opt.sampler_slots, sampler_bindings.data());
      }
      context->Draw(3, 0);
    }

    if (opt.present)
      swapchain->Present(0, 0);
    else
      context->Flush();
  }
  measured_end = std::chrono::steady_clock::now();

  context->ClearState();
  context->Flush();

  double elapsed_ms = std::chrono::duration<double, std::milli>(measured_end - measured_start).count();
  double total_draws = double(opt.frames) * double(opt.draws);
  std::printf("elapsed_ms=%.3f\n", elapsed_ms);
  std::printf("avg_frame_ms=%.3f\n", elapsed_ms / double(opt.frames));
  std::printf("avg_draw_us=%.3f\n", elapsed_ms * 1000.0 / total_draws);
  std::printf("draws_per_sec=%.0f\n", total_draws * 1000.0 / elapsed_ms);

  for (auto *p : samplers)
    if (p) p->Release();
  for (auto *p : srvs)
    if (p) p->Release();
  for (auto *p : cbuffers)
    if (p) p->Release();
  safe_release(ps_blob);
  safe_release(vs_blob);
  safe_release(ps);
  safe_release(vs);
  safe_release(rtv);
  safe_release(swapchain);
  safe_release(context);
  safe_release(device);
  DestroyWindow(hwnd);
  return 0;
}
