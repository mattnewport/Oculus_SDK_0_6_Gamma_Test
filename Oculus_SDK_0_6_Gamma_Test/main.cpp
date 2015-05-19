// Modified from Oculus Tiny Room sample to demonstrate sRGB display issues with 0.6.0 SDK

/************************************************************************************
Filename    :   Win32_RoomTiny_Main.cpp
Content     :   First-person view test application for Oculus Rift
Created     :   18th Dec 2014
Authors     :   Tom Heath
Copyright   :   Copyright 2012 Oculus, Inc. All Rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*************************************************************************************/
/// This is an entry-level sample, showing a minimal VR sample,
/// in a simple environment.  Use WASD keys to move around, and cursor keys.
/// Dismiss the health and safety warning by tapping the headset,
/// or pressing any key.
/// It runs with DirectX11.

#include <memory>
#include <regex>
#include <unordered_map>

#include <comdef.h>
#include <comip.h>

#include <d3d11_1.h>
#include <d3dcompiler.h>

_COM_SMARTPTR_TYPEDEF(IDXGIFactory, __uuidof(IDXGIFactory));
_COM_SMARTPTR_TYPEDEF(IDXGIAdapter, __uuidof(IDXGIAdapter));
_COM_SMARTPTR_TYPEDEF(IDXGIDevice1, __uuidof(IDXGIDevice1));
_COM_SMARTPTR_TYPEDEF(IDXGISwapChain, __uuidof(IDXGISwapChain));
_COM_SMARTPTR_TYPEDEF(ID3D11Device, __uuidof(ID3D11Device));
_COM_SMARTPTR_TYPEDEF(ID3D11DeviceContext, __uuidof(ID3D11DeviceContext));
_COM_SMARTPTR_TYPEDEF(ID3D11Texture2D, __uuidof(ID3D11Texture2D));
_COM_SMARTPTR_TYPEDEF(ID3D11RenderTargetView, __uuidof(ID3D11RenderTargetView));
_COM_SMARTPTR_TYPEDEF(ID3D11ShaderResourceView, __uuidof(ID3D11ShaderResourceView));
_COM_SMARTPTR_TYPEDEF(ID3D11Resource, __uuidof(ID3D11Resource));
_COM_SMARTPTR_TYPEDEF(ID3D11BlendState, __uuidof(ID3D11BlendState));

// Include the Oculus SDK
#include "OVR_CAPI.h"
#define OVR_D3D_VERSION 11
#include "OVR_CAPI_D3D.h"

#include "DDSTextureLoader.h"

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")

#ifndef VALIDATE
#define VALIDATE(x, msg)                                                  \
    if (!(x)) {                                                           \
        MessageBoxA(NULL, (msg), "OculusRoomTiny", MB_ICONERROR | MB_OK); \
        exit(-1);                                                         \
    }
#endif

using namespace std;

unordered_map<string, string> parseArgs(const char* args) {
    unordered_map<string, string> argMap;
    regex re{R"(-((\?)|(\w+))(\s+(\w+))?)"};
    for_each(cregex_iterator{args, args + strlen(args), re}, cregex_iterator{},
             [&](const cmatch m) {
                 argMap[string{m[1].first, m[1].second}] = string{m[5].first, m[5].second};
             });
    return argMap;
}

struct DirectX11 {
    HWND Window;
    bool Running;
    bool Key[256];
    ovrSizei WinSize;
    ID3D11DevicePtr Device;
    ID3D11DeviceContextPtr Context;
    IDXGISwapChainPtr SwapChain;
    ID3D11Texture2DPtr BackBuffer;
    ID3D11RenderTargetViewPtr BackBufferRT;

    static LRESULT CALLBACK
    WindowProc(_In_ HWND hWnd, _In_ UINT Msg, _In_ WPARAM wParam, _In_ LPARAM lParam) {
        DirectX11* p = (DirectX11*)GetWindowLongPtr(hWnd, 0);
        switch (Msg) {
            case WM_KEYDOWN:
                p->Key[wParam] = true;
                break;
            case WM_KEYUP:
                p->Key[wParam] = false;
                break;
            case WM_DESTROY:
                p->Running = false;
                break;
            default:
                return DefWindowProcW(hWnd, Msg, wParam, lParam);
        }

        if ((p->Key['Q'] && p->Key[VK_CONTROL]) || p->Key[VK_ESCAPE]) p->Running = false;

        return 0;
    }

    bool InitWindowAndDevice(const HINSTANCE hinst, const ovrRecti vp,
                             const DXGI_FORMAT backBufferFormat, LPCWSTR title = nullptr) {
        Running = true;

        // Clear input
        for (int i = 0; i < 256; i++) Key[i] = false;

        WNDCLASSW wc;
        memset(&wc, 0, sizeof(wc));
        wc.lpszClassName = L"OVRAppWindow";
        wc.style = CS_OWNDC;
        wc.lpfnWndProc = WindowProc;
        wc.cbWndExtra = sizeof(struct DirectX11*);
        RegisterClassW(&wc);

        const DWORD wsStyle = WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME;
        RECT winSize = {0, 0, vp.Size.w, vp.Size.h};
        AdjustWindowRect(&winSize, wsStyle, FALSE);
        Window = CreateWindowW(L"OVRAppWindow", (title ? title : L"OculusRoomTiny (DX11)"),
                               wsStyle | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                               winSize.right - winSize.left, winSize.bottom - winSize.top, NULL,
                               NULL, hinst, NULL);
        if (!Window) return (false);
        SetWindowLongPtr(Window, 0, LONG_PTR(this));

        WinSize = vp.Size;

        IDXGIFactoryPtr DXGIFactory;
        IDXGIAdapterPtr Adapter;
        if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory), (void**)(&DXGIFactory))))
            return (false);
        if (FAILED(DXGIFactory->EnumAdapters(0, &Adapter))) return (false);

#ifdef _DEBUG
        const auto deviceCreationFlags = D3D11_CREATE_DEVICE_DEBUG;
#else
        const auto deviceCreationFlags = 0;
#endif
        if (FAILED(D3D11CreateDevice(
                Adapter, Adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, NULL,
                deviceCreationFlags, NULL, 0, D3D11_SDK_VERSION, &Device, NULL, &Context)))
            return (false);

        // Create swap chain
        DXGI_SWAP_CHAIN_DESC scDesc;
        memset(&scDesc, 0, sizeof(scDesc));
        scDesc.BufferCount = 2;
        scDesc.BufferDesc.Width = WinSize.w;
        scDesc.BufferDesc.Height = WinSize.h;
        scDesc.BufferDesc.Format = backBufferFormat;
        scDesc.BufferDesc.RefreshRate.Numerator = 0;
        scDesc.BufferDesc.RefreshRate.Denominator = 1;
        scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scDesc.OutputWindow = Window;
        scDesc.SampleDesc.Count = 1;
        scDesc.SampleDesc.Quality = 0;
        scDesc.Windowed = TRUE;
        scDesc.SwapEffect = DXGI_SWAP_EFFECT_SEQUENTIAL;
        if (FAILED(DXGIFactory->CreateSwapChain(Device, &scDesc, &SwapChain))) return (false);

        // Create backbuffer
        if (FAILED(SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&BackBuffer)))
            return (false);
        if (FAILED(Device->CreateRenderTargetView(BackBuffer, NULL, &BackBufferRT))) return (false);

        // Set max frame latency to 1
        IDXGIDevice1Ptr DXGIDevice1 = NULL;
        HRESULT hr = Device->QueryInterface(__uuidof(IDXGIDevice1), (void**)&DXGIDevice1);
        if (FAILED(hr) | (DXGIDevice1 == NULL)) return (false);
        DXGIDevice1->SetMaximumFrameLatency(1);

        return (true);
    }

    bool HandleMessages(void) {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        return Running;
    }

    void ReleaseWindow(HINSTANCE hinst) {
        DestroyWindow(Window);
        UnregisterClassW(L"OVRAppWindow", hinst);
    }
};

//------------------------------------------------------------
// ovrSwapTextureSet wrapper class that also maintains the render target views
// needed for D3D11 rendering.
struct OculusTexture {
    ovrSwapTextureSet* TextureSet;
    ID3D11RenderTargetView* TexRtv[3];

    OculusTexture(const ovrHmd hmd, const ovrSizei size, ID3D11Device* const device, const DXGI_FORMAT texFormat) {
        D3D11_TEXTURE2D_DESC dsDesc;
        dsDesc.Width = size.w;
        dsDesc.Height = size.h;
        dsDesc.MipLevels = 1;
        dsDesc.ArraySize = 1;
        dsDesc.Format = texFormat;
        dsDesc.SampleDesc.Count = 1;  // No multi-sampling allowed
        dsDesc.SampleDesc.Quality = 0;
        dsDesc.Usage = D3D11_USAGE_DEFAULT;
        dsDesc.CPUAccessFlags = 0;
        dsDesc.MiscFlags = 0;
        dsDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

        ovrHmd_CreateSwapTextureSetD3D11(hmd, device, &dsDesc, &TextureSet);
    }

    void AdvanceToNextTexture() {
        TextureSet->CurrentIndex = (TextureSet->CurrentIndex + 1) % TextureSet->TextureCount;
    }
    void Release(ovrHmd hmd) { ovrHmd_DestroySwapTextureSet(hmd, TextureSet); }
};

//-------------------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hinst, HINSTANCE, LPSTR args, int) {
    auto argMap = parseArgs(args);
    const auto sRGB = argMap["sRGB"] != "false";
    const auto texFormat = [&argMap, sRGB] {
        return sRGB ? DXGI_FORMAT_B8G8R8A8_UNORM_SRGB : DXGI_FORMAT_B8G8R8A8_UNORM;
    }();

    // Initializes LibOVR, and the Rift
    ovrResult result = ovr_Initialize(nullptr);
    VALIDATE(result == ovrSuccess, "Failed to initialize libOVR.");

    ovrHmd HMD;
    result = ovrHmd_Create(0, &HMD);
    if (result != ovrSuccess)
        result =
            ovrHmd_CreateDebug(ovrHmd_DK2, &HMD);  // Use debug one, if no genuine Rift available
    VALIDATE(result == ovrSuccess, "Oculus Rift not detected.");
    VALIDATE(HMD->ProductName[0] != '\0', "Rift detected, display not enabled.");

    // Setup Window and Graphics
    auto dx11 = make_unique<DirectX11>();
    ovrSizei winSize = {HMD->Resolution.w, HMD->Resolution.h};
    bool initialized = dx11->InitWindowAndDevice(hinst, ovrRecti{ovrVector2i{0, 0}, winSize},
                                                   texFormat, L"Oculus Room Tiny (DX11)");
    VALIDATE(initialized, "Unable to initialize window and D3D11 device.");

    ovrHmd_SetEnabledCaps(HMD, ovrHmdCap_LowPersistence | ovrHmdCap_DynamicPrediction);

    // Start the sensor which informs of the Rift's pose and motion
    result = ovrHmd_ConfigureTracking(
        HMD, ovrTrackingCap_Orientation | ovrTrackingCap_MagYawCorrection | ovrTrackingCap_Position,
        0);
    VALIDATE(result == ovrSuccess, "Failed to configure tracking.");

    // Make the eye render buffers (caution if actual size < requested due to HW limits).
    unique_ptr<OculusTexture> pEyeRenderTexture[2];
    ovrRecti eyeRenderViewport[2];

    for (int eye = 0; eye < 2; eye++) {
        ovrSizei idealSize{winSize.w / 2, winSize.h};
        pEyeRenderTexture[eye] = make_unique<OculusTexture>(HMD, idealSize, dx11->Device, texFormat);
        eyeRenderViewport[eye].Pos = ovrVector2i{0, 0};
        eyeRenderViewport[eye].Size = idealSize;
    }

    // Create a mirror to see on the monitor.
    ovrTexture* mirrorTexture = nullptr;
    D3D11_TEXTURE2D_DESC td = {};
    td.ArraySize = 1;
    td.Format = texFormat;
    td.Width = dx11->WinSize.w;
    td.Height = dx11->WinSize.h;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.SampleDesc.Count = 1;
    td.MipLevels = 1;
    ovrHmd_CreateMirrorTextureD3D11(HMD, dx11->Device, &td, &mirrorTexture);

    ID3D11ResourcePtr gammaTestTex;
    ID3D11ShaderResourceViewPtr gammaTestSRV;
    DirectX::CreateDDSTextureFromFileEx(
        dx11->Device,
        LR"(E:\Users\Matt\Documents\Dropbox2\Dropbox\Projects\Oculus_SDK_0_6_Gamma_Test\Oculus_SDK_0_6_Gamma_Test\gamma-test.dds)",
        0, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0, sRGB, &gammaTestTex,
        &gammaTestSRV, nullptr);

    // Main loop
    while (dx11->HandleMessages()) {
        // Render Scene to Eye Buffers
        for (int eye = 0; eye < 2; eye++) {
            // Increment to use next texture, just before writing
            pEyeRenderTexture[eye]->AdvanceToNextTexture();

            int texIndex = pEyeRenderTexture[eye]->TextureSet->CurrentIndex;
            dx11->Context->CopyResource(
                reinterpret_cast<ovrD3D11Texture*>(
                    &pEyeRenderTexture[eye]->TextureSet->Textures[texIndex])
                    ->D3D11.pTexture,
                gammaTestTex);
        }

        // Initialize our single full screen direct layer.
        ovrLayerDirect ld;
        ld.Header.Type = ovrLayerType_Direct;
        ld.Header.Flags = 0;

        for (int eye = 0; eye < 2; eye++) {
            ld.ColorTexture[eye] = pEyeRenderTexture[eye]->TextureSet;
            ld.Viewport[eye] = eyeRenderViewport[eye];
        }

        ovrLayerHeader* layers = &ld.Header;
        ovrResult result = ovrHmd_SubmitFrame(HMD, 0, nullptr, &layers, 1);

        // Render mirror
        ovrD3D11Texture* tex = (ovrD3D11Texture*)mirrorTexture;
        dx11->Context->CopyResource(dx11->BackBuffer, tex->D3D11.pTexture);
        dx11->SwapChain->Present(0, 0);
    }

    // Release
    ovrHmd_DestroyMirrorTexture(HMD, mirrorTexture);
    pEyeRenderTexture[0]->Release(HMD);
    pEyeRenderTexture[1]->Release(HMD);
    ovrHmd_Destroy(HMD);
    ovr_Shutdown();
    dx11->ReleaseWindow(hinst);
    return (0);
}
