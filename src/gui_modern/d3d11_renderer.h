#pragma once
#include <windows.h>
#include <d3d11.h>

namespace d3d {
    extern ID3D11Device*           g_pd3dDevice;
    extern ID3D11DeviceContext*    g_pd3dDeviceContext;
    extern IDXGISwapChain*         g_pSwapChain;
    extern ID3D11RenderTargetView* g_mainRenderTargetView;

    bool InitD3D(HWND hWnd);
    void CleanupD3D();
    void CreateRenderTarget();
    void CleanupRenderTarget();
    void OnResize(UINT width, UINT height);

    void BeginFrame();
    void EndFrame();
}
