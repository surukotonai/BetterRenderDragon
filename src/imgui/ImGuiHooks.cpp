#include <windows.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

#include <atomic>
#include <cstdio>
#include <d3d12.h>
#include <dxgi1_5.h>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "backends/imgui_impl_dx12.h"
#include "backends/imgui_impl_win32.h"

#include "ImGuiHooks.h"
#include "api/memory/Hook.h"
#include "api/memory/win/Memory.h"
#include "gui/GUI.h"
#include <d3d11.h>
#include <imgui_impl_dx11.h>

static IDXGIFactory2 *factory;
static HWND g_hWnd = nullptr;
static bool imguiInitialized = false;
static WNDPROC oWndProc;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM,
                                                             LPARAM);

namespace ImGuiD3D12 {

struct FrameCommand {
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> cmdList;
};

struct ImGuiD3D12Context {
  ComPtr<ID3D12Device> device;
  ComPtr<ID3D12CommandQueue> queue;
  ComPtr<IDXGISwapChain3> swapchain;
  ComPtr<ID3D12DescriptorHeap> imguiHeap;

  HWND hwnd = nullptr;
  UINT backBufferCount = 0;
  bool initialized = false;

  std::vector<FrameCommand> frameCommands;

  std::mutex mtx;

  static ImGuiD3D12Context &Get() {
    static ImGuiD3D12Context ctx;
    return ctx;
  }
};

static void InitImGuiDX12() {
  auto &ctx = ImGuiD3D12Context::Get();
  if (ctx.initialized)
    return;

  DXGI_SWAP_CHAIN_DESC desc{};
  ctx.swapchain->GetDesc(&desc);
  ctx.hwnd = desc.OutputWindow;
  ctx.backBufferCount = desc.BufferCount;

  D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
  heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heapDesc.NumDescriptors = ctx.backBufferCount + 8;
  heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  ctx.device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&ctx.imguiHeap));

  ctx.frameCommands.resize(ctx.backBufferCount);
  for (uint32_t i = 0; i < ctx.backBufferCount; ++i) {
    FrameCommand &fc = ctx.frameCommands[i];
    ctx.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                       IID_PPV_ARGS(&fc.allocator));
    ctx.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                  fc.allocator.Get(), nullptr,
                                  IID_PPV_ARGS(&fc.cmdList));
    fc.cmdList->Close();
  }

  ImGui::CreateContext();
  initializeImGui(true);

  ImGui_ImplWin32_Init(ctx.hwnd);
  ImGui_ImplDX12_Init(ctx.device.Get(), ctx.backBufferCount,
                      DXGI_FORMAT_R8G8B8A8_UNORM, ctx.imguiHeap.Get(),
                      ctx.imguiHeap->GetCPUDescriptorHandleForHeapStart(),
                      ctx.imguiHeap->GetGPUDescriptorHandleForHeapStart());
  ImGui_ImplDX12_CreateDeviceObjects();

  ctx.initialized = true;
}

static void RenderImGuiDX12() {
  auto &ctx = ImGuiD3D12Context::Get();
  if (!ctx.initialized)
    return;

  std::lock_guard<std::mutex> lock(ctx.mtx);

  UINT backIdx = ctx.swapchain->GetCurrentBackBufferIndex();
  FrameCommand &fc = ctx.frameCommands[backIdx];

  fc.allocator->Reset();
  fc.cmdList->Reset(fc.allocator.Get(), nullptr);

  ComPtr<ID3D12Resource> backBuffer;
  ctx.swapchain->GetBuffer(backIdx, IID_PPV_ARGS(&backBuffer));

  D3D12_RESOURCE_BARRIER before{};
  before.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  before.Transition.pResource = backBuffer.Get();
  before.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
  before.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
  before.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  fc.cmdList->ResourceBarrier(1, &before);

  D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;
  {
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
    rtvDesc.NumDescriptors = 1;
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ctx.device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&rtvHeap));
    rtvHandle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    ctx.device->CreateRenderTargetView(backBuffer.Get(), nullptr, rtvHandle);
    fc.cmdList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
  }

  ID3D12DescriptorHeap *heaps[] = {ctx.imguiHeap.Get()};
  fc.cmdList->SetDescriptorHeaps(1, heaps);

  ImGui_ImplDX12_NewFrame();
  ImGui_ImplWin32_NewFrame();
  updateImGui();
  ImGui::Render();
  ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), fc.cmdList.Get());

  D3D12_RESOURCE_BARRIER after = before;
  after.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
  after.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
  fc.cmdList->ResourceBarrier(1, &after);

  fc.cmdList->Close();

  ID3D12CommandList *lists[] = {fc.cmdList.Get()};
  ctx.queue->ExecuteCommandLists(1, lists);
}

PFN_IDXGISwapChain_Present Original_IDXGISwapChain_Present = nullptr;
HRESULT STDMETHODCALLTYPE IDXGISwapChain_Present_Hook(IDXGISwapChain *This,
                                                      UINT SyncInterval,
                                                      UINT Flags) {
  ComPtr<IDXGISwapChain3> swapChain3;
  if (FAILED(This->QueryInterface(IID_PPV_ARGS(&swapChain3)))) {
    return Original_IDXGISwapChain_Present(This, SyncInterval, Flags);
  }

  auto &ctx = ImGuiD3D12Context::Get();
  if (!ctx.initialized && ctx.device && ctx.swapchain && ctx.queue)
    InitImGuiDX12();

  if (ctx.initialized)
    RenderImGuiDX12();

  return Original_IDXGISwapChain_Present(This, SyncInterval, Flags);
}

} // namespace ImGuiD3D12

namespace ImGuiD3D11 {
ID3D11Device *device;
ComPtr<ID3D11DeviceContext> deviceContext;

uint32_t backBufferCount = 0;
ID3D11RenderTargetView **renderTargetViews;

void createRT(IDXGISwapChain *swapChain) {
  for (uint32_t i = 0; i < backBufferCount; i++) {
    ComPtr<ID3D11Resource> backBuffer;
    swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer));

    ID3D11RenderTargetView *rtv;
    device->CreateRenderTargetView(backBuffer.Get(), nullptr, &rtv);

    renderTargetViews[i] = rtv;
  }
}

void releaseRT() {
  for (size_t i = 0; i < backBufferCount; i++) {
    if (renderTargetViews[i]) {
      renderTargetViews[i]->Release();
      renderTargetViews[i] = nullptr;
    }
  }
}

bool initializeImguiBackend(IDXGISwapChain *pSwapChain) {
  initializeImGui(false);

  DXGI_SWAP_CHAIN_DESC desc;
  pSwapChain->GetDesc(&desc);

  backBufferCount = desc.BufferCount;
  renderTargetViews = new ID3D11RenderTargetView *[backBufferCount];

  device->GetImmediateContext(&deviceContext);

  createRT(pSwapChain);

  ImGui_ImplWin32_Init(g_hWnd);
  ImGui_ImplDX11_Init(device, deviceContext.Get());
  ImGui_ImplDX11_CreateDeviceObjects();

  return true;
}

void renderImGui(IDXGISwapChain3 *swapChain) {
  ImGui_ImplDX11_NewFrame();
  ImGui_ImplWin32_NewFrame();

  updateImGui();

  ID3D11RenderTargetView *currentRTV =
      renderTargetViews[swapChain->GetCurrentBackBufferIndex()];
  deviceContext->OMSetRenderTargets(1, &currentRTV, nullptr);

  ImGui::Render();
  ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

PFN_IDXGISwapChain_Present Original_IDXGISwapChain_Present = nullptr;
HRESULT STDMETHODCALLTYPE IDXGISwapChain_Present_Hook(IDXGISwapChain *This,
                                                      UINT SyncInterval,
                                                      UINT Flags) {
  ComPtr<IDXGISwapChain3> swapChain3;
  if (FAILED(This->QueryInterface(IID_PPV_ARGS(&swapChain3)))) {
    return Original_IDXGISwapChain_Present(This, SyncInterval, Flags);
  }

  if (imguiInitialized) {
    renderImGui(swapChain3.Get());
  }

  return Original_IDXGISwapChain_Present(This, SyncInterval, Flags);
}

PFN_IDXGISwapChain_ResizeBuffers Original_IDXGISwapChain_ResizeBuffers;
HRESULT STDMETHODCALLTYPE IDXGISwapChain_ResizeBuffers_Hook(
    IDXGISwapChain *This, UINT BufferCount, UINT Width, UINT Height,
    DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
  releaseRT();
  HRESULT hResult = Original_IDXGISwapChain_ResizeBuffers(
      This, BufferCount, Width, Height, NewFormat, SwapChainFlags);
  createRT(This);
  return hResult;
}
} // namespace ImGuiD3D11

static LRESULT WINAPI WndProc(HWND hWnd, UINT uMsg, WPARAM wParam,
                              LPARAM lParam) {

  if (ImGui::GetCurrentContext())
    ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);

  return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
}

PFN_IDXGIFactory2_CreateSwapChainForHwnd
    Original_IDXGIFactory2_CreateSwapChainForHwnd;
HRESULT STDMETHODCALLTYPE IDXGIFactory2_CreateSwapChainForHwnd_Hook(
    IDXGIFactory2 *This, IUnknown *pDevice, HWND hWnd,
    const DXGI_SWAP_CHAIN_DESC1 *pDesc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pFullDesc,
    IDXGIOutput *pRestrictToOutput, IDXGISwapChain1 **ppSwapChain) {
  ;
  g_hWnd = hWnd;
  oWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtr(
      g_hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProc)));
  HRESULT hr = Original_IDXGIFactory2_CreateSwapChainForHwnd(
      This, pDevice, hWnd, pDesc, pFullDesc, pRestrictToOutput, ppSwapChain);

  if (SUCCEEDED(hr)) {
    IDXGISwapChain1 *swapChain = *ppSwapChain;
    ComPtr<ID3D12CommandQueue> d3d12CommandQueue;
    ComPtr<ID3D11Device> d3d11Device;

    if (SUCCEEDED(pDevice->QueryInterface(IID_PPV_ARGS(&d3d12CommandQueue)))) {
      auto &ctx = ImGuiD3D12::ImGuiD3D12Context::Get();
      swapChain->GetDevice(IID_PPV_ARGS(&ctx.device));
      ctx.queue = (ID3D12CommandQueue *)pDevice;
      ctx.swapchain = (IDXGISwapChain3 *)swapChain;
      memory::ReplaceVtable(
          *(void **)swapChain, 8,
          (void **)&ImGuiD3D12::Original_IDXGISwapChain_Present,
          ImGuiD3D12::IDXGISwapChain_Present_Hook);
    } else if (SUCCEEDED(pDevice->QueryInterface(IID_PPV_ARGS(&d3d11Device)))) {
      ImGuiD3D11::device = (ID3D11Device *)pDevice;
      memory::ReplaceVtable(
          *(void **)swapChain, 8,
          (void **)&ImGuiD3D11::Original_IDXGISwapChain_Present,
          ImGuiD3D11::IDXGISwapChain_Present_Hook);
      memory::ReplaceVtable(
          *(void **)swapChain, 13,
          (void **)&ImGuiD3D11::Original_IDXGISwapChain_ResizeBuffers,
          ImGuiD3D11::IDXGISwapChain_ResizeBuffers_Hook);
      if (!(imguiInitialized = ImGuiD3D11::initializeImguiBackend(swapChain)))
        printf("Failed to initialize ImGui on Direct3D 11\n");
    }
  }
  return hr;
}

HRESULT (*createDXGIFactory1Original)(REFIID riid, void **ppFactory) = nullptr;
HRESULT createDXGIFactory1Hook(REFIID riid, void **ppFactory) {
  HRESULT hResult = createDXGIFactory1Original(riid, ppFactory);
  if (SUCCEEDED(hResult)) {
    IDXGIFactory2 *factory2 = (IDXGIFactory2 *)*ppFactory;
    if (!Original_IDXGIFactory2_CreateSwapChainForHwnd) {
      memory::ReplaceVtable(
          *(void **)factory2, 15,
          (void **)&Original_IDXGIFactory2_CreateSwapChainForHwnd,
          IDXGIFactory2_CreateSwapChainForHwnd_Hook);
    }
    factory = factory2;
  }
  return hResult;
}

void initImGuiHooks() {
  memory::hook(CreateDXGIFactory1, createDXGIFactory1Hook,
               (memory::FuncPtr *)&createDXGIFactory1Original,
               memory::HookPriority::Normal);
}

SKY_AUTO_STATIC_HOOK(
    Mouse, memory::HookPriority::Normal,
    "4C 8B DC 49 89 5B ? 49 89 6B ? 56 57 41 56 48 81 EC A0 00 00 00 48 8B 05 "
    "? ? ? ? 48 33 C4 48 89 84 24 ? ? ? ? 41 0F B7 E9",
    void, void *a1, void *a2, void *a3, void *a4) {
  if (ImGui::GetCurrentContext()) {
    ImGuiIO &io = ImGui::GetIO();
    if (io.WantCaptureMouse) {
      return;
    }
  }
  return origin(a1, a2, a3, a4);
}
