#include <windows.h>
#include <wrl/client.h>

#include "MinHook.h"
#include "api/memory/HookAPI.hpp"
using Microsoft::WRL::ComPtr;

#include <atomic>
#include <cstdio>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_5.h>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "backends/imgui_impl_dx12.h"
#include "backends/imgui_impl_win32.h"
#include <imgui_impl_dx11.h>

#include "ImGuiHooks.h"
#include "api/memory/Hook.h"
#include "gui/GUI.h"

static HWND g_hWnd = nullptr;
static bool imguiInitialized = false;
static WNDPROC oWndProc;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM,
                                                             LPARAM);

namespace ImGuiD3D12 {

struct ImGuiDX12Resources {
  ComPtr<ID3D12Device> device;
  ComPtr<ID3D12CommandQueue> queue;
  ComPtr<IDXGISwapChain3> swapchain;

  ComPtr<ID3D12DescriptorHeap> imguiSrvHeap;

  UINT backBufferCount = 0;
  HWND hwnd = nullptr;
  bool initialized = false;

  std::vector<ComPtr<ID3D12CommandAllocator>> commandAllocators;
  std::vector<ComPtr<ID3D12GraphicsCommandList>> commandLists;
  std::vector<ComPtr<ID3D12DescriptorHeap>> rtvHeaps;

  std::mutex mtx;

  static ImGuiDX12Resources &Get() {
    static ImGuiDX12Resources ctx;
    return ctx;
  }
};

static void InitImGuiDX12() {
  auto &ctx = ImGuiDX12Resources::Get();
  if (ctx.initialized)
    return;

  if (!ctx.device || !ctx.swapchain || !ctx.queue) {
    Logger::log("InitImGuiDX12 failed!!!");
    return;
  }

  DXGI_SWAP_CHAIN_DESC desc{};
  if (FAILED(ctx.swapchain->GetDesc(&desc))) {
    Logger::log("swapchain->GetDesc failed");
    return;
  }
  ctx.hwnd = desc.OutputWindow;
  ctx.backBufferCount = desc.BufferCount;

  D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
  heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heapDesc.NumDescriptors = ctx.backBufferCount + 8;
  heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  ctx.device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&ctx.imguiSrvHeap));

  ctx.commandAllocators.resize(ctx.backBufferCount);
  ctx.commandLists.resize(ctx.backBufferCount);
  ctx.rtvHeaps.resize(ctx.backBufferCount);

  for (uint32_t i = 0; i < ctx.backBufferCount; ++i) {
    ctx.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                       IID_PPV_ARGS(&ctx.commandAllocators[i]));
    ctx.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                  ctx.commandAllocators[i].Get(), nullptr,
                                  IID_PPV_ARGS(&ctx.commandLists[i]));
    ctx.commandLists[i]->Close();
  }

  initializeImGui(true);

  ImGui_ImplWin32_Init(ctx.hwnd);

  ImGui_ImplDX12_Init(ctx.device.Get(), ctx.backBufferCount,
                      desc.BufferDesc.Format, ctx.imguiSrvHeap.Get(),
                      ctx.imguiSrvHeap->GetCPUDescriptorHandleForHeapStart(),
                      ctx.imguiSrvHeap->GetGPUDescriptorHandleForHeapStart());
  ImGui_ImplDX12_CreateDeviceObjects();

  ctx.initialized = true;
}

static void RenderImGuiDX12() {
  auto &ctx = ImGuiDX12Resources::Get();
  if (!ctx.initialized)
    return;

  std::lock_guard<std::mutex> lock(ctx.mtx);

  UINT backIdx = ctx.swapchain->GetCurrentBackBufferIndex();
  ComPtr<ID3D12CommandAllocator> &allocator = ctx.commandAllocators[backIdx];
  ComPtr<ID3D12GraphicsCommandList> &cmdList = ctx.commandLists[backIdx];
  ComPtr<ID3D12DescriptorHeap> &rtvHeap = ctx.rtvHeaps[backIdx];

  allocator->Reset();
  cmdList->Reset(allocator.Get(), nullptr);

  ComPtr<ID3D12Resource> backBuffer;
  ctx.swapchain->GetBuffer(backIdx, IID_PPV_ARGS(&backBuffer));

  D3D12_RESOURCE_BARRIER beforeBarrier = {};
  beforeBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  beforeBarrier.Transition.pResource = backBuffer.Get();
  beforeBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
  beforeBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
  beforeBarrier.Transition.Subresource =
      D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  cmdList->ResourceBarrier(1, &beforeBarrier);

  D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
  rtvDesc.NumDescriptors = 1;
  rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  if (!rtvHeap) {
    ctx.device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&rtvHeap));
  }

  D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
      rtvHeap->GetCPUDescriptorHandleForHeapStart();
  ctx.device->CreateRenderTargetView(backBuffer.Get(), nullptr, rtvHandle);

  cmdList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

  ID3D12DescriptorHeap *heaps[] = {ctx.imguiSrvHeap.Get()};
  cmdList->SetDescriptorHeaps(1, heaps);


  ImGui_ImplDX12_NewFrame();
  ImGui_ImplWin32_NewFrame();
  updateImGui();
  ImGui::Render();

  ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList.Get());

  D3D12_RESOURCE_BARRIER afterBarrier = beforeBarrier;
  afterBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
  afterBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
  cmdList->ResourceBarrier(1, &afterBarrier);

  cmdList->Close();

  ID3D12CommandList *lists[] = {cmdList.Get()};
  ctx.queue->ExecuteCommandLists(1, lists);
}

PFN_IDXGISwapChain_Present Original_IDXGISwapChain_Present = nullptr;
PFN_IDXGISwapChain_ResizeBuffers Original_IDXGISwapChain_ResizeBuffers_DX12 =
    nullptr;

HRESULT STDMETHODCALLTYPE IDXGISwapChain_Present_Hook(IDXGISwapChain *This,
                                                      UINT SyncInterval,
                                                      UINT Flags) {
  if (!This) {
    return Original_IDXGISwapChain_Present(This, SyncInterval, Flags);
  }

  ComPtr<IDXGISwapChain3> swapChain3;
  if (FAILED(This->QueryInterface(IID_PPV_ARGS(&swapChain3)))) {
    return Original_IDXGISwapChain_Present(This, SyncInterval, Flags);
  }

  try {
    auto &ctx = ImGuiD3D12::ImGuiDX12Resources::Get();
    if (!ctx.initialized && ctx.device && ctx.swapchain && ctx.queue) {
      InitImGuiDX12();
    }

    if (ctx.initialized) {
      RenderImGuiDX12();
    }
  } catch (const std::exception& e) {
    Logger::log("Exception: %s", e.what());
  }

  return Original_IDXGISwapChain_Present(This, SyncInterval, Flags);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_ResizeBuffers_Hook(
    IDXGISwapChain *This, UINT BufferCount, UINT Width, UINT Height,
    DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
  Logger::log("IDXGISwapChain_ResizeBuffers_Hook called");
  auto &ctx = ImGuiD3D12::ImGuiDX12Resources::Get();

  ComPtr<ID3D12Fence> fence;
  ctx.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
  HANDLE evt = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  UINT64 val = 1;
  ctx.queue->Signal(fence.Get(), val);
  fence->SetEventOnCompletion(val, evt);
  WaitForSingleObject(evt, INFINITE);
  CloseHandle(evt);

  HRESULT hr = Original_IDXGISwapChain_ResizeBuffers_DX12(
      This, BufferCount, Width, Height, NewFormat, SwapChainFlags);

  if (SUCCEEDED(hr)) {
    DXGI_SWAP_CHAIN_DESC desc{};
    ctx.swapchain->GetDesc(&desc);
    ctx.backBufferCount = desc.BufferCount;

    ctx.commandAllocators.clear();
    ctx.commandLists.clear();
    ctx.rtvHeaps.clear();

    ctx.commandAllocators.resize(ctx.backBufferCount);
    ctx.commandLists.resize(ctx.backBufferCount);
    ctx.rtvHeaps.resize(ctx.backBufferCount);

    for (uint32_t i = 0; i < ctx.backBufferCount; ++i) {
      ctx.device->CreateCommandAllocator(
          D3D12_COMMAND_LIST_TYPE_DIRECT,
          IID_PPV_ARGS(&ctx.commandAllocators[i]));
      ctx.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                    ctx.commandAllocators[i].Get(), nullptr,
                                    IID_PPV_ARGS(&ctx.commandLists[i]));
      ctx.commandLists[i]->Close();
    }

    ImGui_ImplDX12_InvalidateDeviceObjects();
    ImGui_ImplDX12_CreateDeviceObjects();
  }

  return hr;
}

}

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
  Logger::log("IDXGISwapChain_ResizeBuffers_Hook called");
  releaseRT();
  HRESULT hResult = Original_IDXGISwapChain_ResizeBuffers(
      This, BufferCount, Width, Height, NewFormat, SwapChainFlags);
  createRT(This);
  return hResult;
}
} // namespace ImGuiD3D11

static LRESULT WINAPI WndProc(HWND hWnd, UINT uMsg, WPARAM wParam,
                              LPARAM lParam) {
  if (hWnd == g_hWnd && ImGui::GetCurrentContext()) {
    try {
      ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);
    } catch (...) {
      Logger::log("ImGui exception");
    }
  }

  if (oWndProc) {
    return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
  }

  return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

PFN_IDXGIFactory2_CreateSwapChainForHwnd
    Original_IDXGIFactory2_CreateSwapChainForHwnd;

HRESULT STDMETHODCALLTYPE IDXGIFactory2_CreateSwapChainForHwnd_Hook(
    IDXGIFactory2 *This, IUnknown *pDevice, HWND hWnd,
    const DXGI_SWAP_CHAIN_DESC1 *pDesc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pFullDesc,
    IDXGIOutput *pRestrictToOutput, IDXGISwapChain1 **ppSwapChain) {
  Logger::log("IDXGIFactory2_CreateSwapChainForHwnd_Hook called");

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
      Logger::log("dx12");
      auto &ctx = ImGuiD3D12::ImGuiDX12Resources::Get();
      swapChain->GetDevice(IID_PPV_ARGS(&ctx.device));
      ctx.queue = d3d12CommandQueue.Get();
      ctx.swapchain = (IDXGISwapChain3 *)swapChain;

      memory::ReplaceVtable(
          *(void **)swapChain, 8,
          (void **)&ImGuiD3D12::Original_IDXGISwapChain_Present,
          ImGuiD3D12::IDXGISwapChain_Present_Hook);
      memory::ReplaceVtable(
          *(void **)swapChain, 13,
          (void **)&ImGuiD3D12::Original_IDXGISwapChain_ResizeBuffers_DX12,
          ImGuiD3D12::IDXGISwapChain_ResizeBuffers_Hook);
    } else if (SUCCEEDED(pDevice->QueryInterface(IID_PPV_ARGS(&d3d11Device)))) {
      Logger::log("dx11");
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
        Logger::log("Failed to initialize ImGui on Direct3D 11");
    }
  }

  return hr;
}

HRESULT (*createDXGIFactory1Original)(REFIID riid, void **ppFactory) = nullptr;

HRESULT createDXGIFactory1Hook(REFIID riid, void **ppFactory) {
  Logger::log("createDXGIFactory1Hook called");
  HRESULT hResult = createDXGIFactory1Original(riid, ppFactory);
  if (SUCCEEDED(hResult)) {
    IDXGIFactory2 *factory2 = (IDXGIFactory2 *)*ppFactory;
    if (!Original_IDXGIFactory2_CreateSwapChainForHwnd) {
      if (factory2) {
        factory2->AddRef();

        memory::ReplaceVtable(
            *(void **)factory2, 15,
            (void **)&Original_IDXGIFactory2_CreateSwapChainForHwnd,
            IDXGIFactory2_CreateSwapChainForHwnd_Hook);
      }
    }
  }
  return hResult;
}

DeclareHook(Mouse,void, void *a1, void *a2, void *a3, void *a4) {
  if (ImGui::GetCurrentContext()) {
    ImGuiIO &io = ImGui::GetIO();
    if (io.WantCaptureMouse) {
      return;
    }
  }
  return original(a1, a2, a3, a4);
}

void initImGuiHooks() {
  HMODULE dxgiModule = GetModuleHandleA("dxgi.dll");
  if (!dxgiModule)
    dxgiModule = LoadLibraryA("dxgi.dll");

  auto pCreateDXGIFactory1 = GetProcAddress(dxgiModule, "CreateDXGIFactory1");

  if (!pCreateDXGIFactory1)
    return;

  MH_CreateHook(
      pCreateDXGIFactory1,
      &createDXGIFactory1Hook,
      reinterpret_cast<void**>(&createDXGIFactory1Original)
  );
  MH_EnableHook(pCreateDXGIFactory1);
  Logger::log("Hooked CreateDXGIFactory1");

  TrySigHook(Mouse,
    //1.21.130
    "4C 8B ? 49 89 ? ? 49 89 ? ? 56 57 41 ? 48 81 EC ? ? ? ? 41 0F ? ? 45 0F"
  );
}