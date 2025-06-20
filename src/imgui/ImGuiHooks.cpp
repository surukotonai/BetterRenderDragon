// clang-format off
#include <windows.h>
#include <psapi.h>

#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

#include <initguid.h>
#include <d3d12.h>
#include <d3d11.h>

#include <string>
#include <memory>

#include "backends/imgui_impl_dx12.h"
#include "backends/imgui_impl_dx11.h"
#include "imgui_impl_winrt.h"

#include "ImGuiHooks.h"
#include "gui/Options.h"
#include "Version.h"
#include "gui/GUI.h"

#include "api/memory/Memory.h"
#include "api/memory/Hook.h"
// clang-format on

//=======================================================================================================================================================================

static IDXGIFactory2 *factory;
static IUnknown *coreWindow;
static std::string gpuName;
static std::string rendererType;
static bool imguiInitialized = false;

//=======================================================================================================================================================================

namespace ImGuiD3D12 {
ComPtr<ID3D12Device> device;
ComPtr<ID3D12DescriptorHeap> descriptorHeapBackBuffers;
ComPtr<ID3D12DescriptorHeap> descriptorHeapImGuiRender;
ID3D12CommandQueue *commandQueue;

struct BackBufferContext {
  ComPtr<ID3D12CommandAllocator> commandAllocator;
  ComPtr<ID3D12GraphicsCommandList> commandList;
  ID3D12Resource *resource = nullptr;
  D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle = {0};
};

uint32_t backBufferCount = 0;
std::unique_ptr<BackBufferContext[]> backBufferContext;

void createRT(IDXGISwapChain *swapChain) {
  const auto rtvDescriptorSize =
      device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
      descriptorHeapBackBuffers->GetCPUDescriptorHandleForHeapStart();

  for (uint32_t i = 0; i < backBufferCount; i++) {
    ComPtr<ID3D12Resource> pBackBuffer = nullptr;
    backBufferContext[i].descriptorHandle = rtvHandle;
    swapChain->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer));
    device->CreateRenderTargetView(pBackBuffer.Get(), nullptr, rtvHandle);
    backBufferContext[i].resource = pBackBuffer.Detach();
    rtvHandle.ptr += rtvDescriptorSize;
  }
}

void releaseRT() {
  for (size_t i = 0; i < backBufferCount; i++) {
    if (backBufferContext[i].resource) {
      backBufferContext[i].resource->Release();
      backBufferContext[i].resource = nullptr;
    }
  }
}

bool initializeImguiBackend(IDXGISwapChain *pSwapChain) {
  if (SUCCEEDED(pSwapChain->GetDevice(IID_PPV_ARGS(&device)))) {
    brd::Options::vanilla2DeferredAvailable = true;
    rendererType = "Direct3D 12";

    initializeImGui();

    DXGI_SWAP_CHAIN_DESC desc;
    pSwapChain->GetDesc(&desc);

    backBufferCount = desc.BufferCount;
    backBufferContext.reset(new BackBufferContext[backBufferCount]);

    for (size_t i = 0; i < backBufferCount; i++) {
      if (device->CreateCommandAllocator(
              D3D12_COMMAND_LIST_TYPE_DIRECT,
              IID_PPV_ARGS(&backBufferContext[i].commandAllocator)) != S_OK) {
        return false;
      }

      if (device->CreateCommandList(
              0, D3D12_COMMAND_LIST_TYPE_DIRECT,
              backBufferContext[i].commandAllocator.Get(), nullptr,
              IID_PPV_ARGS(&backBufferContext[i].commandList)) != S_OK ||
          backBufferContext[i].commandList->Close() != S_OK) {
        return false;
      }
    }

    D3D12_DESCRIPTOR_HEAP_DESC descriptorImGuiRender = {};
    descriptorImGuiRender.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    descriptorImGuiRender.NumDescriptors = backBufferCount;
    descriptorImGuiRender.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    if (device->CreateDescriptorHeap(
            &descriptorImGuiRender, IID_PPV_ARGS(&descriptorHeapImGuiRender)) !=
        S_OK) {
      return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC descriptorBackBuffers;
    descriptorBackBuffers.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    descriptorBackBuffers.NumDescriptors = backBufferCount;
    descriptorBackBuffers.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    descriptorBackBuffers.NodeMask = 1;

    if (device->CreateDescriptorHeap(
            &descriptorBackBuffers, IID_PPV_ARGS(&descriptorHeapBackBuffers)) !=
        S_OK) {
      return false;
    }

    createRT(pSwapChain);

    ImGui_ImplWinRT_Init(coreWindow);
    ImGui_ImplDX12_Init(
        device.Get(), backBufferCount, DXGI_FORMAT_R8G8B8A8_UNORM,
        descriptorHeapImGuiRender.Get(),
        descriptorHeapImGuiRender->GetCPUDescriptorHandleForHeapStart(),
        descriptorHeapImGuiRender->GetGPUDescriptorHandleForHeapStart());
    ImGui_ImplDX12_CreateDeviceObjects();
  }
  return true;
}

void renderImGui(IDXGISwapChain3 *swapChain) {
  ImGui_ImplDX12_NewFrame();
  ImGui_ImplWinRT_NewFrame();

  updateImGui();

  BackBufferContext &currentBufferContext =
      backBufferContext[swapChain->GetCurrentBackBufferIndex()];
  currentBufferContext.commandAllocator->Reset();

  D3D12_RESOURCE_BARRIER barrier;
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier.Transition.pResource = currentBufferContext.resource;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

  ID3D12DescriptorHeap *descriptorHeaps[1] = {descriptorHeapImGuiRender.Get()};
  currentBufferContext.commandList->Reset(
      currentBufferContext.commandAllocator.Get(), nullptr);
  currentBufferContext.commandList->ResourceBarrier(1, &barrier);
  currentBufferContext.commandList->OMSetRenderTargets(
      1, &currentBufferContext.descriptorHandle, FALSE, nullptr);
  currentBufferContext.commandList->SetDescriptorHeaps(1, descriptorHeaps);

  ImGui::Render();
  ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(),
                                currentBufferContext.commandList.Get());

  ID3D12CommandList *commandLists[1] = {currentBufferContext.commandList.Get()};
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
  currentBufferContext.commandList->ResourceBarrier(1, &barrier);
  currentBufferContext.commandList->Close();
  commandQueue->ExecuteCommandLists(1, commandLists);
}

PFN_IDXGISwapChain_Present Original_IDXGISwapChain_Present = nullptr;
HRESULT STDMETHODCALLTYPE IDXGISwapChain_Present_Hook(IDXGISwapChain *This,
                                                      UINT SyncInterval,
                                                      UINT Flags) {
  ComPtr<IDXGISwapChain3> swapChain3;
  if (FAILED(This->QueryInterface(IID_PPV_ARGS(&swapChain3)))) {
    return Original_IDXGISwapChain_Present(This, SyncInterval, Flags);
  }

  if (!imguiInitialized) {
    printf("Initializing ImGui on Direct3D 12\n");
    if (initializeImguiBackend(This)) {
      imguiInitialized = true;
    } else {
      printf("ImGui is not initialized\n");
      return Original_IDXGISwapChain_Present(This, SyncInterval, Flags);
    }
  }

  renderImGui(swapChain3.Get());

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
} // namespace ImGuiD3D12

//=======================================================================================================================================================================

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
  brd::Options::vanilla2DeferredAvailable = false;
  rendererType = "Direct3D 11";

  initializeImGui();

  DXGI_SWAP_CHAIN_DESC desc;
  pSwapChain->GetDesc(&desc);

  backBufferCount = desc.BufferCount;
  renderTargetViews = new ID3D11RenderTargetView *[backBufferCount];

  device->GetImmediateContext(&deviceContext);

  createRT(pSwapChain);

  ImGui_ImplWinRT_Init(coreWindow);
  ImGui_ImplDX11_Init(device, deviceContext.Get());
  ImGui_ImplDX11_CreateDeviceObjects();

  return true;
}

void renderImGui(IDXGISwapChain3 *swapChain) {
  ImGui_ImplDX11_NewFrame();
  ImGui_ImplWinRT_NewFrame();

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

//=======================================================================================================================================================================

PFN_IDXGIFactory2_CreateSwapChainForCoreWindow
    Original_IDXGIFactory2_CreateSwapChainForCoreWindow;
HRESULT STDMETHODCALLTYPE IDXGIFactory2_CreateSwapChainForCoreWindow_Hook(
    IDXGIFactory2 *This, IUnknown *pDevice, IUnknown *pWindow,
    const DXGI_SWAP_CHAIN_DESC1 *pDesc, IDXGIOutput *pRestrictToOutput,
    IDXGISwapChain1 **ppSwapChain) {
  if (pWindow)
    coreWindow = pWindow;

  HRESULT hResult = Original_IDXGIFactory2_CreateSwapChainForCoreWindow(
      This, pDevice, pWindow, pDesc, pRestrictToOutput, ppSwapChain);
  if (SUCCEEDED(hResult)) {
    IDXGISwapChain1 *swapChain = *ppSwapChain;
    ComPtr<ID3D12CommandQueue> d3d12CommandQueue;
    ComPtr<ID3D11Device> d3d11Device;
    if (SUCCEEDED(pDevice->QueryInterface(IID_PPV_ARGS(&d3d12CommandQueue)))) {
      // Direct3D 12
      ImGuiD3D12::commandQueue = (ID3D12CommandQueue *)pDevice;
      if (!ImGuiD3D12::Original_IDXGISwapChain_Present)
        memory::ReplaceVtable(
            *(void **)swapChain, 8,
            (void **)&ImGuiD3D12::Original_IDXGISwapChain_Present,
            ImGuiD3D12::IDXGISwapChain_Present_Hook);
      if (!ImGuiD3D12::Original_IDXGISwapChain_ResizeBuffers)
        memory::ReplaceVtable(
            *(void **)swapChain, 13,
            (void **)&ImGuiD3D12::Original_IDXGISwapChain_ResizeBuffers,
            ImGuiD3D12::IDXGISwapChain_ResizeBuffers_Hook);
      // When the graphics API used by RenderDragon is D3D12, this function will
      // be called in a non-main thread and later IDXGISwapChain::Present will
      // be called three times in the main thread, so initialize ImGui later in
      // IDXGISwapChain::Present
    } else if (SUCCEEDED(pDevice->QueryInterface(IID_PPV_ARGS(&d3d11Device)))) {
      // Direct3D 11
      ImGuiD3D11::device = (ID3D11Device *)pDevice;
      if (!ImGuiD3D11::Original_IDXGISwapChain_Present)
        memory::ReplaceVtable(
            *(void **)swapChain, 8,
            (void **)&ImGuiD3D11::Original_IDXGISwapChain_Present,
            ImGuiD3D11::IDXGISwapChain_Present_Hook);
      if (!ImGuiD3D11::Original_IDXGISwapChain_ResizeBuffers)
        memory::ReplaceVtable(
            *(void **)swapChain, 13,
            (void **)&ImGuiD3D11::Original_IDXGISwapChain_ResizeBuffers,
            ImGuiD3D11::IDXGISwapChain_ResizeBuffers_Hook);
      // When the graphics API used by RenderDragon is D3D11, this function will
      // be called in the main thread, and IDXGISwapChain::Present will all be
      // called in a non-main thread, so initialize ImGui here immediately
      printf("Initializing ImGui on Direct3D 11\n");
      if (!(imguiInitialized = ImGuiD3D11::initializeImguiBackend(swapChain))) {
        printf("Failed to initialize ImGui on Direct3D 11\n");
      }
    } else {
    }
  }
  return hResult;
}

//=======================================================================================================================================================================

HRESULT (*createDXGIFactory1Original)(REFIID riid, void **ppFactory) = nullptr;
HRESULT createDXGIFactory1Hook(REFIID riid, void **ppFactory) {
  HRESULT hResult = createDXGIFactory1Original(riid, ppFactory);
  if (IsEqualIID(IID_IDXGIFactory2, riid) && SUCCEEDED(hResult)) {
    printf("CreateDXGIFactory1 riid=IID_IDXGIFactory2\n");
    IDXGIFactory2 *factory2 = (IDXGIFactory2 *)*ppFactory;
    if (!Original_IDXGIFactory2_CreateSwapChainForCoreWindow) {
      memory::ReplaceVtable(
          *(void **)factory2, 16,
          (void **)&Original_IDXGIFactory2_CreateSwapChainForCoreWindow,
          IDXGIFactory2_CreateSwapChainForCoreWindow_Hook);
    }
    factory = factory2;
  }
  return hResult;
}

//=======================================================================================================================================================================

void initImGuiHooks() {
  memory::hook(CreateDXGIFactory1, createDXGIFactory1Hook,
               (memory::FuncPtr *)&createDXGIFactory1Original,
               memory::HookPriority::Normal);
}

//=======================================================================================================================================================================

std::string getGPUName() { return gpuName; }

std::string getRendererType() { return rendererType; }

//=======================================================================================================================================================================