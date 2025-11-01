#pragma once
#include <dxgi1_6.h>
#include <string>

//=========================================================================================================================//

// CreateDXGIFactory1
typedef HRESULT(STDMETHODCALLTYPE *PFN_CreateDXGIFactory1)(
    REFIID riid, _COM_Outptr_ void **ppFactory);

// IDXGIFactory2::CreateSwapChainForHwnd
typedef HRESULT(STDMETHODCALLTYPE *PFN_IDXGIFactory2_CreateSwapChainForHwnd)(
    IDXGIFactory2 *This, _In_ IUnknown *pDevice, _In_ HWND hWnd,
    _In_ const DXGI_SWAP_CHAIN_DESC1 *pDesc,
    _In_ const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *,
    _In_opt_ IDXGIOutput *pRestrictToOutput,
    _COM_Outptr_ IDXGISwapChain1 **ppSwapChain);

// IDXGISwapChain::Present
typedef HRESULT(STDMETHODCALLTYPE *PFN_IDXGISwapChain_Present)(
    IDXGISwapChain *This, _In_ UINT SyncInterval, _In_ UINT Flags);

// IDXGISwapChain::ResizeBuffers
typedef HRESULT(STDMETHODCALLTYPE *PFN_IDXGISwapChain_ResizeBuffers)(
    IDXGISwapChain *This, _In_ UINT BufferCount, _In_ UINT Width,
    _In_ UINT Height, _In_ DXGI_FORMAT NewFormat, _In_ UINT SwapChainFlags);

//=========================================================================================================================//

void initImGuiHooks();
